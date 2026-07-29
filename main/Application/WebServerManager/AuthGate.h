#pragma once
#include <esp_http_server.h>
#include <cstdint>
#include <cstddef>
#include "WsSessionLink.h"

class Authenticator;
struct WsConnection;

// The pre-auth handshake + authed/not routing decision for the WebSocket,
// self-contained. Depends only on the Authenticator. Owns the reply framing.
//
// Deliberately typed on WsSessionLink, not the transport-agnostic SessionLink:
// authentication is a property of the physical link, not of the session layer.
// A password + bearer token is what a *browser over WiFi* needs; BLE proves the
// peer with pairing (the install-code passkey) and bonding at the link layer, so
// it has no login handshake and never routes through here. See
// docs/reasoning/2026-07-29-11h29-authentication-belongs-to-the-transport.md.
class AuthGate {
public:
    explicit AuthGate(Authenticator& auth) : auth_(auth) {}

    enum class Disposition { Handled, PassToMux, Rejected };

    // Parse the first chunk's `type`. hello/login/auth → handled here (reply via
    // `link`, flip conn.authed on success), returns Handled. An authed non-verb
    // → PassToMux. An unauthenticated non-verb → REJECT reply, Rejected.
    Disposition Handle(WsConnection& conn, WsSessionLink& link,
                       uint16_t sid, const uint8_t* payload, size_t len);

private:
    Authenticator& auth_;
    void SendReply(WsSessionLink& link, uint16_t sid, const char* json);
    void SendReject(WsSessionLink& link, uint16_t sid, const char* reason);
};
