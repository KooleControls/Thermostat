#include "OpenThermManager.h"
#include "OtFrame.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "RoomTemperatureManager.h"
#include "JsonScope.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>

using F = OtFrame::MsgType;

namespace
{
    // OpenTherm data-IDs (subset used here; mirrors gateway OTHThermostatProps).
    constexpr uint8_t ID_STATUS   = 0;
    constexpr uint8_t ID_TSET     = 1;
    constexpr uint8_t ID_SCONFIG  = 3;
    constexpr uint8_t ID_OEMFAULT = 5;
    constexpr uint8_t ID_TROVRD   = 9;
    constexpr uint8_t ID_TRSET    = 16;
    constexpr uint8_t ID_RELMOD   = 17;
    constexpr uint8_t ID_CHPRESS  = 18;
    constexpr uint8_t ID_TROOM    = 24;
    constexpr uint8_t ID_TBOILER  = 25;
    constexpr uint8_t ID_TDHW     = 26;
    constexpr uint8_t ID_TOUTSIDE = 27;
    constexpr uint8_t ID_TRET     = 28;
    constexpr uint8_t ID_TDHWSET  = 56;
    constexpr uint8_t ID_MAXTSET_BOUNDS = 49;  // s8/s8 max-CH-setpoint bounds (OT 2.2)
    constexpr uint8_t ID_OEMDIAG  = 115;

    // Fixed rotation: writes interleaved with reads. One slot per cycle.
    struct Slot { uint8_t id; bool write; };
    constexpr Slot kRotation[] = {
        { ID_TSET,     true  }, { ID_RELMOD,   false },
        { ID_TRSET,    true  }, { ID_TBOILER,  false },
        { ID_TROOM,    true  }, { ID_TDHW,     false },
        { ID_TDHWSET,  true  }, { ID_TRET,     false },
        { ID_CHPRESS,  false }, { ID_TOUTSIDE, false },
        { ID_OEMFAULT, false }, { ID_OEMDIAG,  false },
        { ID_MAXTSET_BOUNDS, false }, { ID_SCONFIG, false },
    };
    constexpr size_t kSlots = sizeof(kRotation) / sizeof(kRotation[0]);
    // Keeps OpenThermManager.h's fixed-size unsupported_[14] member honest.
    static_assert(kSlots == 14, "OpenThermManager::unsupported_ is sized for kSlots == 14");

}

OpenThermManager::OpenThermManager(ServiceProvider &serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void OpenThermManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    serviceProvider_.getCommandManager().Register(this, commands_);

    task_.Init("opentherm", 5, 8192);
    task_.SetHandler([this]() { Loop(); });
    task_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (OpenTherm master, link %s)",
             serviceProvider_.getBoard().GetOtLink().Ready() ? "ready" : "not ready");
}

OtBoilerState OpenThermManager::GetState() const { LOCK(mutex_); return state_; }
OtDemand      OpenThermManager::GetDemand() const { LOCK(mutex_); return demand_; }

void OpenThermManager::SetHeatingDemand(bool chEnable, bool coolEnable, float roomSetpoint, float tSet)
{
    LOCK(mutex_);
    bool changed = demand_.chEnable != chEnable ||
                   demand_.coolEnable != coolEnable ||
                   fabsf(demand_.roomSetpoint - roomSetpoint) > 0.01f ||
                   fabsf(demand_.tSet - tSet) > 0.01f;
    if (changed)
    {
        demand_.chEnable   = chEnable;
        demand_.coolEnable = coolEnable;
        demand_.roomSetpoint = roomSetpoint;
        demand_.tSet       = tSet;
        demandDirty_ = true;
    }
}

void OpenThermManager::SetDhwDemand(bool dhwEnable, float dhwSetpoint)
{
    LOCK(mutex_);
    bool changed = demand_.dhwEnable != dhwEnable ||
                   fabsf(demand_.dhwSetpoint - dhwSetpoint) > 0.01f;
    if (changed)
    {
        demand_.dhwEnable   = dhwEnable;
        demand_.dhwSetpoint = dhwSetpoint;
        demandDirty_ = true;
    }
}

// ── the master loop ───────────────────────────────────────────

void OpenThermManager::Loop()
{
    OtLink &link = serviceProvider_.getBoard().GetOtLink();

    while (true)
    {
        if (link.Ready())
            ServiceLink(link);
        else
            RecoverLink(link);

        LogLinkTransition();
        cycle_++;
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
}

// One 500 ms cycle while the link is up: Status keepalive, ID 9 override
// read every other cycle, one slot of the write/read rotation.
void OpenThermManager::ServiceLink(OtLink &link)
{
    if (!DoStatus(link))
    {
        if (++failStreak_ >= LinkFailLimit)
            MarkLinkDown();
        return;
    }

    failStreak_ = 0;
    recoverBackoffS_ = 5;
    if ((cycle_ & 1) == 0)
        DoOverrideRead(link);
    DoRotationSlot(link);
}

// Link down: safe state (no frames = no demand). Retry Recover with backoff.
void OpenThermManager::RecoverLink(OtLink &link)
{
    MarkLinkDown();

    int64_t now = esp_timer_get_time();
    if (now < nextRecoverUs_) return;

    if (link.Recover())
    {
        recoverBackoffS_ = 5;
        return;
    }
    nextRecoverUs_ = now + (int64_t)recoverBackoffS_ * 1000000;
    recoverBackoffS_ = (recoverBackoffS_ * 2 > 30) ? 30 : recoverBackoffS_ * 2;
}

void OpenThermManager::LogLinkTransition()
{
    LOCK(mutex_);
    if (state_.linked != lastLoggedLinked_)
    {
        ESP_LOGI(TAG, "OT link %s", state_.linked ? "up" : "down");
        lastLoggedLinked_ = state_.linked;
    }
}

// ── one validated OT exchange ─────────────────────────────────

// Build + Transaction + full reply validation in one place. The STM32 can
// deliver late replies from timed-out requests, so a reply is only trusted
// when parity holds AND the data-ID matches the request. The msg type then
// splits the outcome: the expected ack -> Ok, UNKNOWN-DATAID -> Unsupported,
// any other valid frame (e.g. DataInvalid) -> Rejected.
OpenThermManager::OtResult OpenThermManager::Exchange(OtLink &link, bool write, uint8_t id,
                                                      uint16_t requestValue, uint16_t &replyValue)
{
    uint32_t req = OtFrame::Build(write ? F::WriteData : F::ReadData, id, requestValue);
    uint32_t reply = 0;
    if (!link.Transaction(req, reply) || !OtFrame::ParityOk(reply))
        return OtResult::Fail;
    if (OtFrame::Id(reply) != id)
        return OtResult::Fail;

    F type = OtFrame::Type(reply);
    if (type == F::UnknownDataId)
        return OtResult::Unsupported;
    if (type != (write ? F::WriteAck : F::ReadAck))
        return OtResult::Rejected;

    replyValue = OtFrame::Value(reply);
    return OtResult::Ok;
}

OpenThermManager::OtResult OpenThermManager::Read(OtLink &link, uint8_t id,
                                                  uint16_t &value, uint16_t requestValue)
{
    return Exchange(link, false, id, requestValue, value);
}

OpenThermManager::OtResult OpenThermManager::Write(OtLink &link, uint8_t id, uint16_t value)
{
    uint16_t echo = 0;
    return Exchange(link, true, id, value, echo);
}

// ── the scheduled messages ────────────────────────────────────

bool OpenThermManager::DoStatus(OtLink &link)
{
    uint16_t slave = 0;
    if (Read(link, ID_STATUS, slave, MasterStatusBits()) != OtResult::Ok)
        return false;
    StoreSlaveStatus(slave & 0xFF);
    return true;
}

void OpenThermManager::DoOverrideRead(OtLink &link)
{
    uint16_t raw = 0;
    if (Read(link, ID_TROVRD, raw) != OtResult::Ok)
        return;

    float ovr = OtFrame::FromF88(raw);
    if (ovr < 0.0f) ovr = 0.0f;               // negative is meaningless
    if (ovr > 0.0f)                            // clamp a real override; 0 = none
    {
        if (ovr < 5.0f) ovr = 5.0f;
        else if (ovr > 30.0f) ovr = 30.0f;
    }
    LOCK(mutex_);
    state_.overrideSetpoint = ovr;             // ClimateManager adopts it
}

void OpenThermManager::DoRotationSlot(OtLink &link)
{
    size_t idx = NextSlot();
    const Slot s = kRotation[idx];

    OtResult r;
    uint16_t value = 0;
    if (s.write)
    {
        float v;
        if (!GetWriteValue(s.id, v)) return;   // no valid value to send this slot
        r = Write(link, s.id, OtFrame::F88(v));
    }
    else
    {
        r = Read(link, s.id, value);
    }

    switch (r)
    {
    case OtResult::Ok:
        unsupported_[idx] = false;
        if (!s.write) StoreRead(s.id, value);
        break;
    case OtResult::Unsupported:
        if (!unsupported_[idx])
            ESP_LOGI(TAG, "Data-ID %u not supported by slave", s.id);
        unsupported_[idx] = true;
        break;
    case OtResult::Rejected:
        // DataInvalid etc.: the slave knows the ID, the value wasn't usable.
        // Reads clear the unsupported flag; a rejected write must not.
        if (!s.write) unsupported_[idx] = false;
        break;
    case OtResult::Fail:
        break;
    }
}

// Pick this cycle's rotation slot: a dirty demand jumps the queue (restart
// at slot 0 so the writes go out within ~2 s of the change), unsupported IDs
// are skipped except on the rare retry pass.
size_t OpenThermManager::NextSlot()
{
    {
        LOCK(mutex_);
        if (demandDirty_) { slot_ = 0; demandDirty_ = false; }
    }

    size_t tries = 0;
    while (unsupported_[slot_] && (pass_ % RetryUnsupportedEvery) != 0 && tries++ < kSlots)
        AdvanceSlot();

    size_t idx = slot_;
    AdvanceSlot();
    return idx;
}

void OpenThermManager::AdvanceSlot()
{
    if (++slot_ >= kSlots) { slot_ = 0; pass_++; }
}

// The demand value behind a write slot; false = nothing valid to send.
bool OpenThermManager::GetWriteValue(uint8_t id, float &v)
{
    if (id == ID_TROOM)
    {
        // RoomTemperatureManager owns the measured value (validity-checked);
        // false -> the rotation slot is skipped, gateway keeps its last value.
        return serviceProvider_.getRoomTemperatureManager().GetRoomTemperature(v);
    }

    LOCK(mutex_);
    switch (id)
    {
    case ID_TSET:
        v = demand_.tSet;
        if (v < state_.maxTSetLower) v = (demand_.chEnable && v > 0) ? state_.maxTSetLower : 0;
        if (v > state_.maxTSetUpper) v = state_.maxTSetUpper;
        return true;
    case ID_TRSET:   v = demand_.roomSetpoint; return true;
    case ID_TDHWSET: v = demand_.dhwSetpoint;  return true;
    default:         return false;
    }
}

// Decode a read-slot reply into state_.
void OpenThermManager::StoreRead(uint8_t id, uint16_t value)
{
    LOCK(mutex_);
    switch (id)
    {
    case ID_RELMOD:   state_.modulation  = OtFrame::FromF88(value); break;
    case ID_TBOILER:  state_.boilerTemp  = OtFrame::FromF88(value); break;
    case ID_TDHW:     state_.dhwTemp     = OtFrame::FromF88(value); break;
    case ID_TRET:     state_.returnTemp  = OtFrame::FromF88(value); break;
    case ID_CHPRESS:  state_.chPressure  = OtFrame::FromF88(value); break;
    case ID_TOUTSIDE: state_.outsideTemp = OtFrame::FromF88(value); break;
    case ID_OEMFAULT: state_.oemFaultCode = value; break;
    case ID_OEMDIAG:  state_.oemDiagCode  = value; break;
    case ID_MAXTSET_BOUNDS:
        state_.maxTSetUpper = (int8_t)(value >> 8);
        state_.maxTSetLower = (int8_t)(value & 0xFF);
        break;
    case ID_SCONFIG:
    {
        uint8_t cfg = value >> 8;
        state_.dhwPresent       = cfg & 0x01;
        state_.coolingSupported = cfg & 0x04;
        break;
    }
    }
}

// ── locked leaf helpers (take mutex_; callers hold no lock) ───

uint16_t OpenThermManager::MasterStatusBits()
{
    LOCK(mutex_);
    return (uint16_t)(((demand_.chEnable   ? 1 : 0) << 0 |
                       (demand_.dhwEnable  ? 1 : 0) << 1 |
                       (demand_.coolEnable ? 1 : 0) << 2) << 8);
}

void OpenThermManager::StoreSlaveStatus(uint8_t bits)
{
    LOCK(mutex_);
    state_.linked        = true;
    state_.fault         = bits & 0x01;
    state_.chActive      = bits & 0x02;
    state_.dhwActive     = bits & 0x04;
    state_.flame         = bits & 0x08;
    state_.coolingActive = bits & 0x10;
}

void OpenThermManager::MarkLinkDown()
{
    LOCK(mutex_);
    state_.linked = false;
}

// ── commands (web-UI console / WebSocket) ─────────────────────

void OpenThermManager::Cmd_Status(Stream &, Stream &out)
{
    OtBoilerState s;
    OtDemand d;
    {
        LOCK(mutex_);
        s = state_;
        d = demand_;
    }
    JsonObject resp(out);
    resp.field("linked", s.linked);
    resp.field("fault", s.fault);
    resp.field("chActive", s.chActive);
    resp.field("dhwActive", s.dhwActive);
    resp.field("flame", s.flame);
    resp.field("coolingActive", s.coolingActive);
    resp.field("dhwPresent", s.dhwPresent);
    resp.field("coolingSupported", s.coolingSupported);
    resp.field("boilerTemp", s.boilerTemp);
    resp.field("returnTemp", s.returnTemp);
    resp.field("dhwTemp", s.dhwTemp);
    resp.field("modulation", s.modulation);
    resp.field("chPressure", s.chPressure);
    resp.field("outsideTemp", s.outsideTemp);
    resp.field("oemFaultCode", (uint32_t)s.oemFaultCode);
    resp.field("oemDiagCode", (uint32_t)s.oemDiagCode);
    resp.field("maxTSetUpper", s.maxTSetUpper);
    resp.field("maxTSetLower", s.maxTSetLower);
    resp.field("overrideSetpoint", s.overrideSetpoint);
    resp.field("chEnable", d.chEnable);
    resp.field("dhwEnable", d.dhwEnable);
    resp.field("coolEnable", d.coolEnable);
    resp.field("roomSetpoint", d.roomSetpoint);
    resp.field("dhwSetpoint", d.dhwSetpoint);
    resp.field("tSet", d.tSet);
}

