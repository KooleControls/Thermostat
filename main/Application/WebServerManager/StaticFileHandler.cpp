#include "StaticFileHandler.h"

#include <cstring>
#include <esp_log.h>

static constexpr const char* TAG = "StaticFileHandler";

void StaticFileHandler::RegisterRoute(httpd_handle_t server)
{
    const httpd_uri_t route = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = Handle,
        .user_ctx = nullptr,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    httpd_register_uri_handler(server, &route);
}

const char* StaticFileHandler::GetContentType(const char* ext)
{
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    return "application/octet-stream";
}

bool StaticFileHandler::Resolve(const char* uri, Resolved& out)
{
    // Strip query string
    char clean[256];
    if (const char* query = strchr(uri, '?'))
    {
        size_t len = static_cast<size_t>(query - uri);
        if (len >= sizeof(clean)) len = sizeof(clean) - 1;
        memcpy(clean, uri, len);
        clean[len] = '\0';
        uri = clean;
    }

    if (uri[0] == '\0' || strcmp(uri, "/") == 0) uri = "/index.html";

    // Blob names are relative to www/ ("index.html", "assets/index-abc.js"),
    // while a URI arrives rooted. Callers over the wire may omit the slash.
    if (uri[0] == '/') uri++;

    out.contentType = "application/octet-stream";
    if (const char* ext = strrchr(uri, '.')) out.contentType = GetContentType(ext);

    return WebAssets().Find(uri, out.file);
}

esp_err_t StaticFileHandler::Handle(httpd_req_t* req)
{
    Resolved resolved;
    if (!Resolve(req->uri, resolved))
    {
        // SPA fallback lives here, in the route layer — not in Resolve(), which
        // stays "give me this exact file or nothing".
        if (!Resolve("/index.html", resolved))
        {
            ESP_LOGW(TAG, "no index.html in the embedded bundle (%lu file(s))",
                     static_cast<unsigned long>(WebAssets().Count()));
            httpd_resp_send_404(req);
            return ESP_OK;
        }
        resolved.contentType = "text/html";
    }

    httpd_resp_set_type(req, resolved.contentType);
    if (resolved.file.gzipped)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    // One send, straight from flash: no read buffer, no chunked transfer, and the
    // response carries a real Content-Length.
    return httpd_resp_send(req, reinterpret_cast<const char*>(resolved.file.data),
                           resolved.file.size);
}
