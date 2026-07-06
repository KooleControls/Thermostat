#include "OpenThermManager.h"
#include "OtFrame.h"
#include "CommandManager/CommandManager.h"
#include "Board.h"
#include "JsonScope.h"
#include "JsonReader.h"
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

void OpenThermManager::SetDemand(const OtDemand &d)
{
    LOCK(mutex_);
    bool changed = demand_.chEnable != d.chEnable ||
                   demand_.dhwEnable != d.dhwEnable ||
                   demand_.coolEnable != d.coolEnable ||
                   fabsf(demand_.roomSetpoint - d.roomSetpoint) > 0.01f ||
                   fabsf(demand_.dhwSetpoint - d.dhwSetpoint) > 0.01f ||
                   fabsf(demand_.tSet - d.tSet) > 0.01f;
    if (changed)
    {
        demand_ = d;
        demandDirty_ = true;
    }
}

// ── the master loop ───────────────────────────────────────────

void OpenThermManager::Loop()
{
    OtLink &link = serviceProvider_.getBoard().GetOtLink();
    bool lastLinked = false;

    while (true)
    {
        if (link.Ready())
        {
            bool ok = DoStatus(link);
            if (ok)
            {
                failStreak_ = 0;
                recoverBackoffS_ = 5;
                if ((cycle_ & 1) == 0)
                    DoOverrideRead(link);
                DoRotationSlot(link);
            }
            else if (++failStreak_ >= LinkFailLimit)
            {
                LOCK(mutex_);
                state_.linked = false;
            }
        }
        else
        {
            // Link down: safe state (no frames = no demand). Recover with backoff.
            { LOCK(mutex_); state_.linked = false; }
            int64_t now = esp_timer_get_time();
            if (now >= nextRecoverUs_)
            {
                if (!link.Recover())
                {
                    nextRecoverUs_ = now + (int64_t)recoverBackoffS_ * 1000000;
                    recoverBackoffS_ = (recoverBackoffS_ * 2 > 30) ? 30 : recoverBackoffS_ * 2;
                }
                else
                {
                    recoverBackoffS_ = 5;
                }
            }
        }

        // Log link transitions once.
        {
            LOCK(mutex_);
            if (state_.linked != lastLinked)
            {
                ESP_LOGI(TAG, "OT link %s", state_.linked ? "up" : "down");
                lastLinked = state_.linked;
            }
        }

        cycle_++;
        vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
    }
}

bool OpenThermManager::DoStatus(OtLink &link)
{
    uint16_t master;
    {
        LOCK(mutex_);
        master = (uint16_t)(((demand_.chEnable   ? 1 : 0) << 0 |
                             (demand_.dhwEnable  ? 1 : 0) << 1 |
                             (demand_.coolEnable ? 1 : 0) << 2) << 8);
    }
    uint32_t reply = 0;
    if (!link.Transaction(OtFrame::Build(F::ReadData, ID_STATUS, master), reply) ||
        OtFrame::Type(reply) != F::ReadAck ||
        OtFrame::Id(reply) != ID_STATUS)
        return false;

    uint8_t slave = OtFrame::Value(reply) & 0xFF;
    LOCK(mutex_);
    state_.linked        = true;
    state_.fault         = slave & 0x01;
    state_.chActive      = slave & 0x02;
    state_.dhwActive     = slave & 0x04;
    state_.flame         = slave & 0x08;
    state_.coolingActive = slave & 0x10;
    return true;
}

void OpenThermManager::DoOverrideRead(OtLink &link)
{
    uint32_t reply = 0;
    if (!link.Transaction(OtFrame::Build(F::ReadData, ID_TROVRD, 0), reply) ||
        OtFrame::Type(reply) != F::ReadAck ||
        OtFrame::Id(reply) != ID_TROVRD)
        return;

    float ovr = OtFrame::FromF88(OtFrame::Value(reply));
    if (ovr <= 0.0f) return;   // 0 = no override pending
    if (ovr < 5.0f) ovr = 5.0f;      // same sanity clamp as otSet
    if (ovr > 30.0f) ovr = 30.0f;

    LOCK(mutex_);
    if (fabsf(ovr - demand_.roomSetpoint) > 0.05f)
    {
        ESP_LOGI(TAG, "Remote override: setpoint %.1f -> %.1f", demand_.roomSetpoint, ovr);
        demand_.roomSetpoint = ovr;   // ID 16 echoes it from the next cycles
        demandDirty_ = true;
    }
}

void OpenThermManager::DoRotationSlot(OtLink &link)
{
    // A dirty demand jumps the queue: restart at slot 0 so the writes
    // (t_set, setpoints) go out within ~2 s of the change.
    {
        LOCK(mutex_);
        if (demandDirty_) { slot_ = 0; demandDirty_ = false; }
    }

    // Skip unsupported IDs except on the rare retry pass.
    size_t tries = 0;
    while (unsupported_[slot_] && (pass_ % RetryUnsupportedEvery) != 0 && tries++ < kSlots)
        if (++slot_ >= kSlots) { slot_ = 0; pass_++; }

    const Slot s = kRotation[slot_];
    size_t idx = slot_;
    if (++slot_ >= kSlots) { slot_ = 0; pass_++; }

    uint32_t req;
    if (s.write)
    {
        float v;
        {
            LOCK(mutex_);
            switch (s.id)
            {
            case ID_TSET:
                v = demand_.tSet;
                if (v < state_.maxTSetLower) v = (demand_.chEnable && v > 0) ? state_.maxTSetLower : 0;
                if (v > state_.maxTSetUpper) v = state_.maxTSetUpper;
                break;
            case ID_TRSET:   v = demand_.roomSetpoint; break;
            case ID_TDHWSET: v = demand_.dhwSetpoint;  break;
            case ID_TROOM:
            default:         v = NAN; break;   // filled below without the lock
            }
        }
        if (s.id == ID_TROOM)
        {
            // Temporary feed until ClimateManager owns the (calibrated) value.
            float t = 0;
            if (!serviceProvider_.getBoard().GetTemperatureSensor().ReadTemperature(t))
                return;   // no valid sample -> skip this slot
            v = t;
        }
        if (std::isnan(v)) return;   // no valid value to send this slot
        req = OtFrame::Build(F::WriteData, s.id, OtFrame::F88(v));
    }
    else
    {
        req = OtFrame::Build(F::ReadData, s.id, 0);
    }

    uint32_t reply = 0;
    if (!link.Transaction(req, reply)) return;
    // Discard stale/late replies from a previously timed-out request before
    // touching anything below — a mismatched data-ID must never be attributed
    // to this slot's request.
    if (OtFrame::Id(reply) != s.id) return;

    if (OtFrame::Type(reply) == F::UnknownDataId)
    {
        if (!unsupported_[idx])
            ESP_LOGI(TAG, "Data-ID %u not supported by slave", s.id);
        unsupported_[idx] = true;
        return;
    }

    if (s.write)
    {
        // A WriteAck confirms the slave accepted it; anything else (e.g.
        // DataInvalid) must not clear the unsupported flag — just bail.
        if (OtFrame::Type(reply) != F::WriteAck) return;
        unsupported_[idx] = false;
        return;
    }

    unsupported_[idx] = false;
    if (OtFrame::Type(reply) != F::ReadAck) return;

    uint16_t val = OtFrame::Value(reply);
    LOCK(mutex_);
    switch (s.id)
    {
    case ID_RELMOD:   state_.modulation  = OtFrame::FromF88(val); break;
    case ID_TBOILER:  state_.boilerTemp  = OtFrame::FromF88(val); break;
    case ID_TDHW:     state_.dhwTemp     = OtFrame::FromF88(val); break;
    case ID_TRET:     state_.returnTemp  = OtFrame::FromF88(val); break;
    case ID_CHPRESS:  state_.chPressure  = OtFrame::FromF88(val); break;
    case ID_TOUTSIDE: state_.outsideTemp = OtFrame::FromF88(val); break;
    case ID_OEMFAULT: state_.oemFaultCode = val; break;
    case ID_OEMDIAG:  state_.oemDiagCode  = val; break;
    case ID_MAXTSET_BOUNDS:
        state_.maxTSetUpper = (int8_t)(val >> 8);
        state_.maxTSetLower = (int8_t)(val & 0xFF);
        break;
    case ID_SCONFIG:
    {
        uint8_t cfg = val >> 8;
        state_.dhwPresent       = cfg & 0x01;
        state_.coolingSupported = cfg & 0x04;
        break;
    }
    }
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
    resp.field("chEnable", d.chEnable);
    resp.field("dhwEnable", d.dhwEnable);
    resp.field("coolEnable", d.coolEnable);
    resp.field("roomSetpoint", d.roomSetpoint);
    resp.field("dhwSetpoint", d.dhwSetpoint);
    resp.field("tSet", d.tSet);
}

void OpenThermManager::Cmd_Set(Stream &in, Stream &out)
{
    JsonReader<256> json(in);
    bool changed = false;
    {
        LOCK(mutex_);
        float f;
        int   i;
        if (!std::isnan(f = json.GetFloat("setpoint", NAN)))
        {
            if (f < 5.0f) f = 5.0f;
            if (f > 30.0f) f = 30.0f;
            if (fabsf(demand_.roomSetpoint - f) > 0.01f) { demand_.roomSetpoint = f; changed = true; }
        }
        if (!std::isnan(f = json.GetFloat("dhwSetpoint", NAN)))
        {
            if (f < 30.0f) f = 30.0f;
            if (f > 80.0f) f = 80.0f;
            if (fabsf(demand_.dhwSetpoint - f) > 0.01f) { demand_.dhwSetpoint = f; changed = true; }
        }
        if (!std::isnan(f = json.GetFloat("tset", NAN)))
        {
            if (f < 0.0f) f = 0.0f;
            if (f > 90.0f) f = 90.0f;
            if (fabsf(demand_.tSet - f) > 0.01f) { demand_.tSet = f; changed = true; }
        }
        if ((i = json.GetInt("ch", -1)) >= 0)
        {
            bool v = i != 0;
            if (demand_.chEnable != v) { demand_.chEnable = v; changed = true; }
        }
        if ((i = json.GetInt("dhw", -1)) >= 0)
        {
            bool v = i != 0;
            if (demand_.dhwEnable != v) { demand_.dhwEnable = v; changed = true; }
        }
        if ((i = json.GetInt("cool", -1)) >= 0)
        {
            bool v = i != 0;
            if (demand_.coolEnable != v) { demand_.coolEnable = v; changed = true; }
        }
        if (changed) demandDirty_ = true;
    }
    Cmd_Status(in, out);   // reply with the resulting full state
}
