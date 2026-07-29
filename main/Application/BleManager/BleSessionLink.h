#pragma once

#include "SessionLink.h"
#include "SessionProtocol.h"
#include "host/ble_hs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>

// One session chunk as it arrives from the radio. Sized so a chunk is exactly one
// GATT operation: ATT MTU minus the 3-byte ATT header.
struct BleChunk
{
    static constexpr size_t MaxLen = 244;   // 247 (our preferred MTU) - 3
    uint16_t len = 0;
    uint8_t  data[MaxLen] = {};
};

// The BLE half of the transport contract. Same two calls as WsSessionLink, over a
// GATT notify/write pair instead of a WebSocket:
//
//  - SendRaw   → one GATT write to the gateway's outbound characteristic. One
//                chunk per write, so no fragmentation layer exists on either side.
//  - RecvChunk → pop the next chunk from the queue the NimBLE callback fills.
//
// The queue is the whole reason this class differs from the WebSocket one:
// notifications arrive on the NimBLE host task, which must never block, so the
// callback only enqueues and a separate dispatch task does the blocking reads.
class BleSessionLink : public SessionLink
{
    static constexpr const char* TAG = "BleSessionLink";

    // A stalled peer must not wedge the dispatch task forever; a request that
    // goes quiet this long is treated as end-of-stream.
    static constexpr TickType_t kRecvTimeout = pdMS_TO_TICKS(10000);

    uint16_t      conn_;
    uint16_t      outHandle_;
    QueueHandle_t queue_;

public:
    BleSessionLink(uint16_t conn, uint16_t outHandle, QueueHandle_t queue)
        : conn_(conn), outHandle_(outHandle), queue_(queue) {}

    bool SendRaw(const uint8_t* frame, size_t len) override
    {
        if (conn_ == BLE_HS_CONN_HANDLE_NONE || outHandle_ == 0) return false;

        // NimBLE allows one GATT procedure per connection at a time, so a reply
        // that spans several chunks can legitimately come back BUSY. Retry
        // briefly rather than dropping a chunk in the middle of a reply.
        for (int attempt = 0; attempt < 20; attempt++)
        {
            int rc = ble_gattc_write_flat(conn_, outHandle_, frame, len, nullptr, nullptr);
            if (rc == 0) return true;
            if (rc != BLE_HS_EBUSY)
            {
                ESP_LOGE(TAG, "write failed: %d", rc);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGE(TAG, "write still busy after retries, dropping chunk");
        return false;
    }

    int RecvChunk(uint8_t* buf, size_t cap, uint16_t* sid, uint8_t* flags) override
    {
        BleChunk chunk;
        if (xQueueReceive(queue_, &chunk, kRecvTimeout) != pdTRUE)
        {
            ESP_LOGW(TAG, "no further chunk within the timeout, ending the stream");
            return -1;
        }
        if (chunk.len < session::HEADER_LEN || chunk.len > cap) return -1;

        memcpy(buf, chunk.data, chunk.len);
        *sid = session::readU16(buf);
        *flags = buf[2];
        return static_cast<int>(chunk.len - session::HEADER_LEN);
    }
};
