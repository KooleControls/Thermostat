#pragma once

#include <esp_http_server.h>
#include <WebAssets.h>
#include <cstddef>

// The frontend is part of the application image (components/web_assets), not a
// filesystem: there is nothing to mount, and a file is a pointer into
// flash-mapped rodata. Serving one costs no RAM and no read — the bytes are
// handed to the socket where they lie.
class StaticFileHandler {
public:
    // A resolved static file: the stored bytes, plus the two HTTP facts a route
    // layer needs to serve them. `file.gzipped` must reach the client as
    // Content-Encoding, or it receives gzip bytes labelled as JavaScript.
    struct Resolved {
        WebFile     file;
        const char* contentType;
    };

    // Logical path → embedded file. Shared by the local HTTP route and the
    // `web read` command that serves the relay, so both agree on MIME type and
    // on what "/" means. Accepts an optional query string and a path with or
    // without a leading '/'.
    //
    // Deliberately does NOT do SPA fallback — a missing path returns false.
    // Falling back to index.html is an HTTP decision that belongs to each route
    // layer, which keeps a mistyped asset a real 404 instead of HTML with status
    // 200 (which a browser rejects as a MIME error).
    //
    // No path-traversal check either, and none is needed: this is an exact-match
    // lookup in a fixed table, so "../../etc/passwd" is simply a name that is not
    // in it. There is no directory to escape from.
    static bool Resolve(const char* uri, Resolved& out);

    void RegisterRoute(httpd_handle_t server);

private:
    static esp_err_t Handle(httpd_req_t* req);
    static const char* GetContentType(const char* ext);
};
