#pragma once

#include <cstdint>
#include <cstddef>

// The transport a session's chunks travel over, abstracted away from *which*
// transport. A Session frames [session|flags|payload] and hands it to SendRaw;
// it pulls further inbound chunks of a streamed request body with RecvChunk.
// Nothing above this line knows about WebSockets, BLE, or sockets at all — which
// is the point: SessionMux, Session and AuthGate are transport-blind, so a second
// transport is an implementation of these two calls and nothing else.
//
// Implemented by WsSessionLink (one WS binary frame per chunk). A BLE link
// implements the same pair over a GATT notify/write pair.
class SessionLink
{
public:
    virtual ~SessionLink() = default;

    // Send one already-framed chunk. `frame` is [session|flags|payload] and
    // `len` is the total (header + payload). False on transport failure.
    virtual bool SendRaw(const uint8_t* frame, size_t len) = 0;

    // Receive the NEXT inbound chunk into `buf` (capacity `cap`, header
    // included), filling *sid / *flags from the 3-byte header and leaving the
    // payload at buf + HEADER_LEN. Returns the payload length (>= 0), or -1 on
    // error / over-long frame / anything the caller should treat as
    // end-of-stream.
    virtual int RecvChunk(uint8_t* buf, size_t cap, uint16_t* sid, uint8_t* flags) = 0;
};
