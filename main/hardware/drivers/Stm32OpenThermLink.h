#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstdint>
#include <cstring>
#include "interfaces/OtLink.h"

// ──────────────────────────────────────────────────────────────
// Reusable driver — link to the STM32L051 OpenTherm co-processor.
//
// On the DIYLESS Thermostat 3 the ESP32-S3 does NOT bit-bang OpenTherm; a
// small STM32L051 owns the OT PHY and the ESP drives it over a UART using the
// DIYLESS "STM32 app-protocol". This is the thermostat-side equivalent of the
// gateway's OpenThermModule. (Phase 0, RA2-398.)
//
// Wire framing (re-implemented from the DIYLESS esphome-opentherm-t3 component;
// this is original code written to interoperate, not a copy of that GPLv3 source):
//   200000 baud, 8N1.
//   Each PACKET on the wire = START(0xA0) | <nibbles> | STOP(0x60).
//   Every logical byte is sent as two bytes — high nibble then low nibble,
//   each carried in the low 4 bits (so a byte B -> {(B>>4)&0xF, B&0xF}).
//   Logical bytes in order: [typeId] [payload...] [CRC16 lo] [CRC16 hi].
//   CRC is not validated by the current STM32 firmware (sent as 0, ignored on RX).
//
// Message types (typeId):
//   1 CpuStatusRequest   {u8 dummy}
//   2 CpuStatusResponse  {u8 cpuVer, u8 fwVer, u8 boardRev, u32 uptime}
//   3 GenericStatusResponse {u32 boilerStatus, f32 extTemp, u16 lightValue}
//   4 OtCommandRequest   {u32 frame}              (32-bit OpenTherm frame)
//   5 OtCommandResponse  {u32 frame, u8 status}
//   6 LogRequest         {u8 data[25], u8 len}
//
// Phase 0 scope: reset the STM32 into its app, do the CpuStatus handshake
// (a local round-trip that needs no gateway wired), and expose one OT
// transaction so a single OpenTherm frame can be exchanged end to end.
// ──────────────────────────────────────────────────────────────

class Stm32OpenThermLink : public OtLink
{
    static constexpr const char *TAG = "OTLink";

    static constexpr uint8_t START_BYTE = 0xA0;
    static constexpr uint8_t STOP_BYTE  = 0x60;
    static constexpr int     BAUD       = 200000;
    static constexpr int     RX_BUF     = 256;

public:
    enum MsgType : uint8_t
    {
        CpuStatusRequest     = 1,
        CpuStatusResponse    = 2,
        GenericStatusResponse = 3,
        OtCommandRequest     = 4,
        OtCommandResponse    = 5,
        LogRequest           = 6,
    };

    // OpenTherm message types (top 3 bits of the frame).
    enum OtMsgType : uint8_t
    {
        OT_READ_DATA     = 0,
        OT_WRITE_DATA    = 1,
        OT_INVALID_DATA  = 2,
        OT_READ_ACK      = 4,
        OT_WRITE_ACK     = 5,
        OT_DATA_INVALID  = 6,
        OT_UNKNOWN_DATAID = 7,
    };

    struct CpuStatus
    {
        uint8_t  cpuVer   = 0;
        uint8_t  fwVer    = 0;
        uint8_t  boardRev = 0;
        uint32_t uptime   = 0;
    };

    // ── OtLink role implementation ────────────────────────────
    // The STM32 stops servicing OtCommandRequests unless it has seen a
    // recent CpuStatus exchange (bring-up finding, RA2-398). Transaction()
    // refreshes that heartbeat transparently so callers never know.

    bool Ready() const override { return ready_ && handshakeOk_; }

    bool Transaction(uint32_t request, uint32_t &response) override
    {
        if (!ready_) return false;
        int64_t now = esp_timer_get_time();
        if (now - lastHeartbeatUs_ > HeartbeatPeriodUs)
        {
            CpuStatus cpu;
            if (!ReadCpuStatus(cpu)) { handshakeOk_ = false; return false; }
            handshakeOk_ = true;
            lastHeartbeatUs_ = esp_timer_get_time();
        }
        uint8_t status = 0;
        return Transact(request, response, status);
    }

    bool Recover() override
    {
        if (!ready_) return false;
        ResetIntoApp();
        return Handshake();
    }

    // First hello after reset; logs the co-processor versions once.
    bool Handshake()
    {
        CpuStatus cpu;
        handshakeOk_ = ReadCpuStatus(cpu);
        if (handshakeOk_)
        {
            lastHeartbeatUs_ = esp_timer_get_time();
            ESP_LOGI(TAG, "STM32 co-processor cpuVer=%u fwVer=%u boardRev=%u",
                     cpu.cpuVer, cpu.fwVer, cpu.boardRev);
        }
        return handshakeOk_;
    }

    Stm32OpenThermLink() = default;
    Stm32OpenThermLink(const Stm32OpenThermLink &) = delete;
    Stm32OpenThermLink &operator=(const Stm32OpenThermLink &) = delete;

    // Bring up the UART and reset the STM32 into its flash application.
    // port: which UART peripheral to use (the display owns none of them).
    // autoReset: pulse the STM32 reset during Init (set false to control reset
    // timing/polarity yourself, e.g. during bring-up diagnostics).
    bool Init(uart_port_t port, int txPin, int rxPin, int boot0Pin, int nrstPin, bool autoReset = true)
    {
        port_  = port;
        boot0_ = (gpio_num_t)boot0Pin;
        nrst_  = (gpio_num_t)nrstPin;

        uart_config_t cfg = {};
        cfg.baud_rate  = BAUD;
        cfg.data_bits  = UART_DATA_8_BITS;
        cfg.parity     = UART_PARITY_DISABLE;
        cfg.stop_bits  = UART_STOP_BITS_1;
        cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;

        // GPIO11/12 are shared with the ST7701 3-wire-SPI init lines, whose
        // panel-IO is NOT auto-deleted (St7701Panel sets auto_del_panel_io=0),
        // so it still owns these pins. Reset them to plain GPIO before handing
        // them to the UART (matches the ESPHome allow_other_uses on these pins).
        gpio_reset_pin((gpio_num_t)txPin);
        gpio_reset_pin((gpio_num_t)rxPin);

        esp_err_t err = uart_driver_install(port_, RX_BUF, 0, 0, nullptr, 0);
        if (err != ESP_OK) { ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err)); return false; }
        uart_param_config(port_, &cfg);
        err = uart_set_pin(port_, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) { ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err)); return false; }

        // BOOT0/NRST as push-pull outputs.
        gpio_config_t io = {};
        io.mode = GPIO_MODE_OUTPUT;
        io.pin_bit_mask = (1ULL << boot0Pin) | (1ULL << nrstPin);
        gpio_config(&io);

        if (autoReset) ResetIntoApp();
        ready_ = true;
        return true;
    }

    // Pulse NRST with BOOT0 low so the STM32 boots from flash (its OT app),
    // then give it time to come up before we talk to it. `invert` flips the NRST
    // active level in case the board drives reset through an inverter.
    void ResetIntoApp(bool invert = false)
    {
        int assertLvl = invert ? 1 : 0;   // level that holds the STM32 in reset
        gpio_set_level(nrst_, assertLvl);
        gpio_set_level(boot0_, 0);         // BOOT0 low -> boot from main flash
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(nrst_, !assertLvl); // release reset
        // The STM32 app needs ~1 s after reset before it answers requests
        // (verified during bring-up: queries <~950 ms after reset got no reply).
        vTaskDelay(pdMS_TO_TICKS(900));
        uart_flush_input(port_);
    }

    // Local handshake — does NOT need the gateway wired on the OT bus, so this
    // is the right first smoke test that the ESP<->STM32 link works.
    bool ReadCpuStatus(CpuStatus &out, int timeoutMs = 600, int attempts = 3)
    {
        for (int i = 0; i < attempts; i++)
        {
            uart_flush_input(port_);  // drop preamble / stale late replies
            uint8_t dummy = 0;
            if (!SendPacket(CpuStatusRequest, &dummy, 1)) return false;

            uint8_t type = 0, payload[32];
            int len = ReadPacket(type, payload, sizeof(payload), timeoutMs);
            if (len >= 7 && type == CpuStatusResponse)
            {
                out.cpuVer   = payload[0];
                out.fwVer    = payload[1];
                out.boardRev = payload[2];
                out.uptime   = (uint32_t)payload[3] | ((uint32_t)payload[4] << 8) |
                               ((uint32_t)payload[5] << 16) | ((uint32_t)payload[6] << 24);
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGW(TAG, "no CpuStatusResponse after %d attempts", attempts);
        return false;
    }

    // One OpenTherm master transaction: send a request frame, get the reply
    // frame. Returns true on a well-formed response. `respFrame` is the raw
    // 32-bit OT reply; `status` is the STM32's own ResponseStatus byte
    // (0 = ok / response received; non-zero = timeout/error on the OT bus).
    bool Transact(uint32_t reqFrame, uint32_t &respFrame, uint8_t &status,
                  int perTryMs = 400, int attempts = 3)
    {
        uint8_t req[4] = {
            (uint8_t)(reqFrame & 0xFF), (uint8_t)((reqFrame >> 8) & 0xFF),
            (uint8_t)((reqFrame >> 16) & 0xFF), (uint8_t)((reqFrame >> 24) & 0xFF)};

        // Retry like ReadCpuStatus: the STM32 drops/lags the first request after
        // an idle gap, so a single send often gets no reply. Re-sending primes it.
        for (int i = 0; i < attempts; i++)
        {
            uart_flush_input(port_);  // align response with this request
            if (!SendPacket(OtCommandRequest, req, sizeof(req))) return false;

            // Drain frames until the matching OtCommandResponse arrives — the
            // STM32 can interleave status frames (type 2) ahead of the OT reply.
            int64_t deadline = esp_timer_get_time() + (int64_t)perTryMs * 1000;
            while (esp_timer_get_time() < deadline)
            {
                int remMs = (int)((deadline - esp_timer_get_time()) / 1000);
                uint8_t type = 0, payload[32];
                int len = ReadPacket(type, payload, sizeof(payload), remMs);
                if (len < 0) break;  // timeout this try
                if (type == OtCommandResponse && len >= 5)
                {
                    respFrame = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                                ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
                    status = payload[4];
                    return true;
                }
                // else: stray frame (e.g. type 2 CpuStatusResponse) — keep reading
            }
        }
        return false;
    }

    // Build a 32-bit OT frame (parity bit + 3-bit msg type + 8-bit data-id +
    // 16-bit value), computing even parity over bits 0..30.
    static uint32_t BuildFrame(OtMsgType type, uint8_t dataId, uint16_t value)
    {
        uint32_t f = ((uint32_t)(type & 0x07) << 28) | ((uint32_t)dataId << 16) | value;
        if (Parity(f)) f |= 0x80000000u;  // make total number of 1-bits even
        return f;
    }

    static OtMsgType FrameType(uint32_t f) { return (OtMsgType)((f >> 28) & 0x07); }
    static uint8_t   FrameId(uint32_t f)   { return (uint8_t)((f >> 16) & 0xFF); }
    static uint16_t  FrameValue(uint32_t f){ return (uint16_t)(f & 0xFFFF); }

    // Tear down the UART so Init() can be called again (e.g. to try the other
    // pin orientation during bring-up).
    void Deinit()
    {
        if (ready_) { uart_driver_delete(port_); ready_ = false; }
    }

    // Diagnostic: count RAW bytes received within windowMs (ignoring framing),
    // logging a hex dump. If `send` is true, first transmit a CpuStatusRequest
    // (solicited); if false, just listen (catches unsolicited boot/stream data).
    // Does NOT reset — caller controls reset/polarity. Returns the byte count.
    int DebugProbeRaw(bool send = true, int windowMs = 400)
    {
        uart_flush_input(port_);
        if (send)
        {
            uint8_t dummy = 0;
            SendPacket(CpuStatusRequest, &dummy, 1);
        }

        uint8_t raw[64];
        int n = 0;
        int64_t deadline = esp_timer_get_time() + (int64_t)windowMs * 1000;
        while (esp_timer_get_time() < deadline && n < (int)sizeof(raw))
        {
            uint8_t b;
            if (uart_read_bytes(port_, &b, 1, pdMS_TO_TICKS(10)) == 1)
                raw[n++] = b;
        }
        ESP_LOGI(TAG, "DebugProbeRaw: %d raw bytes", n);
        if (n > 0)
            ESP_LOG_BUFFER_HEX(TAG, raw, n);
        return n;
    }

    bool ok() const { return ready_; }

private:
    // Append one logical byte as two low-nibble bytes (high nibble first).
    static void PushByte(uint8_t *buf, int &n, uint8_t b)
    {
        buf[n++] = (b >> 4) & 0x0F;
        buf[n++] = b & 0x0F;
    }

    bool SendPacket(uint8_t type, const uint8_t *payload, int payloadLen)
    {
        // START + (1 type + payload + 2 CRC) logical bytes * 2 nibbles + STOP.
        uint8_t buf[2 + (1 + 32 + 2) * 2 + 1];
        int n = 0;
        buf[n++] = START_BYTE;
        PushByte(buf, n, type);
        for (int i = 0; i < payloadLen; i++)
            PushByte(buf, n, payload[i]);
        PushByte(buf, n, 0x00);  // CRC16 lo — not validated by STM32 fw
        PushByte(buf, n, 0x00);  // CRC16 hi
        buf[n++] = STOP_BYTE;

        int written = uart_write_bytes(port_, (const char *)buf, n);
        return written == n;
    }

    // Read one framed packet. Returns payload length (excluding type + CRC), or
    // -1 on timeout. `type` gets the decoded type id; `payload` excludes CRC.
    int ReadPacket(uint8_t &type, uint8_t *payload, int payloadMax, int timeoutMs)
    {
        int64_t deadline = esp_timer_get_time() + (int64_t)timeoutMs * 1000;
        uint8_t nibbles[(1 + 32 + 2) * 2];
        int count = 0;
        bool inFrame = false;

        while (esp_timer_get_time() < deadline)
        {
            uint8_t b;
            int r = uart_read_bytes(port_, &b, 1, pdMS_TO_TICKS(10));
            if (r != 1) continue;

            if (b == START_BYTE) { inFrame = true; count = 0; continue; }
            if (!inFrame) continue;
            if (b == STOP_BYTE)
            {
                if (count < 2 || (count & 1)) return -1;  // need type + even nibbles
                int bytes = count / 2;                    // reconstructed logical bytes
                uint8_t decoded[1 + 32 + 2];
                for (int i = 0; i < bytes; i++)
                    decoded[i] = ((nibbles[i * 2] & 0x0F) << 4) | (nibbles[i * 2 + 1] & 0x0F);
                type = decoded[0];
                int payloadLen = bytes - 1 - 2;  // drop type + 2 CRC bytes
                if (payloadLen < 0) payloadLen = 0;
                if (payloadLen > payloadMax) payloadLen = payloadMax;
                memcpy(payload, decoded + 1, payloadLen);
                return payloadLen;
            }
            if (count < (int)sizeof(nibbles)) nibbles[count++] = b;
        }
        return -1;  // timeout
    }

    static bool Parity(uint32_t v)
    {
        v &= 0x7FFFFFFFu;  // exclude the parity bit itself
        bool odd = false;
        while (v) { odd = !odd; v &= v - 1; }
        return odd;  // true -> set parity bit to make the count even
    }

    uart_port_t port_ = UART_NUM_1;
    gpio_num_t  boot0_ = GPIO_NUM_NC;
    gpio_num_t  nrst_  = GPIO_NUM_NC;
    bool        ready_ = false;
    bool    handshakeOk_ = false;
    int64_t lastHeartbeatUs_ = 0;
    static constexpr int64_t HeartbeatPeriodUs = 1000000;  // 1 s
};
