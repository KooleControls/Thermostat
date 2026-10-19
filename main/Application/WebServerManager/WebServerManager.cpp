#include "WebServerManager.h"
#include "ConsoleManager.h"
#include "SettingsManager.h"
#include "CommandManager.h"
#include "JsonHelpers.h"
#include "JsonScope.h"
#include "SessionTable.h"

#include <WebAssets.h>

#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

static constexpr const char* TAG = "WebServerManager";
static WebServerManager* s_instance_ = nullptr;

WebServerManager::WebServerManager(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void WebServerManager::Init()
{
    auto initAttempt = initState.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    s_instance_ = this;

    wsHandler_.SetCommandManager(serviceProvider_.getCommandManager());

    serviceProvider_.getSettingsManager().Register({ &webPassword_ });
    auth_.Init();   // snapshot the stored password (after registration)
    wsHandler_.SetAuth(auth_);

    LogWebAssets();
    StartServer();
    RegisterRoutes();

    serviceProvider_.getCommandManager().Register(this, commands_);

    // Wire console broadcast to WS clients
    serviceProvider_.getConsoleManager().SetBroadcastCallback(
        [](const char* json, int32_t len, void* ctx) {
            static_cast<WebServerManager*>(ctx)->Broadcast(json, len);
        },
        this);

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

// The frontend is packed into a blob and embedded in this image (see
// components/web_assets). Reporting it at boot is what proves the whole chain —
// pnpm, packer, linker — actually landed, and the bundle hash is how you tell two
// builds apart at a glance.
void WebServerManager::LogWebAssets()
{
    const WebAssetTable& assets = WebAssets();
    if (!assets.Valid())
    {
        ESP_LOGE(TAG, "Web assets: unavailable — the UI will not be served");
        return;
    }

    const uint8_t* hash = assets.BundleHash();
    ESP_LOGI(TAG, "Web assets: %lu file(s), %lu bytes, bundle %02x%02x%02x%02x%02x%02x%02x%02x",
             static_cast<unsigned long>(assets.Count()),
             static_cast<unsigned long>(assets.StoredBytes()),
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7]);

    for (const WebFile& file : assets)
    {
        ESP_LOGD(TAG, "  %s (%lu bytes%s)", file.name,
                 static_cast<unsigned long>(file.size), file.gzipped ? ", gzip" : "");
    }
}

void WebServerManager::StartServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_uri_handlers = 20;
    config.close_fn = [](httpd_handle_t, int fd) {
        if (s_instance_)
            s_instance_->wsHandler_.OnClientDisconnected(fd);
        close(fd);
    };
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}

void WebServerManager::RegisterRoutes()
{
    if (!server_) return;

    // HTTP serves two things only: the WebSocket upgrade (which carries ALL
    // device interaction — commands, uploads, downloads, auth) and the static
    // app that bootstraps the page. No /api command route, no CORS: every
    // device interaction is a session on the one socket.
    wsHandler_.RegisterRoute(server_);
    staticFileHandler_.RegisterRoute(server_);
}

void WebServerManager::Broadcast(const char* json, int len)
{
    if (server_)
        wsHandler_.Broadcast(server_, json, len);
}

void WebServerManager::BroadcastBinary(const uint8_t* data, size_t len)
{
    if (server_)
        wsHandler_.BroadcastBinary(server_, data, len);
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError WebServerManager::Cmd_GetWebFile(CommandContext& ctx)
{
    // First handler on the pull contract: no envelope handling, no JsonReader, and
    // it will keep working unchanged when the request format stops being JSON.
    char path[192] = {};
    RETURN_IF_ERROR(ctx.readArgs(Required("path", path)));

    StaticFileHandler::Resolved resolved;
    if (!StaticFileHandler::Resolve(path, resolved))
    {
        // A real 404 — SPA fallback is the asking route layer's decision, not
        // ours (see StaticFileHandler::Resolve).
        static constexpr const char* notFound = "{\"ok\":true,\"status\":404}\n";
        ctx.out.write(notFound, strlen(notFound));
        return RequestError::Ok;   // the request was fine; the file simply is not there
    }

    char header[256];
    int n = snprintf(header, sizeof(header),
                     "{\"ok\":true,\"status\":200,\"contentType\":\"%s\"%s}\n",
                     resolved.contentType,
                     resolved.file.gzipped ? ",\"contentEncoding\":\"gzip\"" : "");
    ctx.out.write(header, static_cast<size_t>(n));

    // One write of a flash pointer: Session::write chunks it through the session
    // window itself, so a 200 KB bundle still needs no buffer here.
    ctx.out.write(resolved.file.data, resolved.file.size);
    return RequestError::Ok;
}

// ──────────────────────────────────────────────────────────────
// auth — the handshake as ordinary commands. Nothing here frames its own reply or
// parses its own wire format any more; it is a handler like every other.
// ──────────────────────────────────────────────────────────────

RequestError WebServerManager::Cmd_AuthHello(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    JsonObject resp(ctx.out);
    resp.field("authRequired", auth_.AuthRequired());
    return RequestError::Ok;
}

RequestError WebServerManager::Cmd_AuthLogin(CommandContext& ctx)
{
    char password[64] = {};
    RETURN_IF_ERROR(ctx.readArgs(Optional("password", password)));

    JsonObject resp(ctx.out);

    // A wrong password is MEANING, not form: the request was perfectly well made, the
    // answer is no. So it is a reply, not a refusal.
    if (!auth_.CheckPassword(password))
    {
        resp.field("ok", false);
        return RequestError::Ok;
    }

    char key[SessionTable::TOKEN_LEN] = {};
    auth_.MintKey(key);
    if (ctx.connection) ctx.connection->authenticate(key);

    resp.field("ok", true);
    resp.field("key", key);
    return RequestError::Ok;
}

RequestError WebServerManager::Cmd_AuthResume(CommandContext& ctx)
{
    char key[SessionTable::TOKEN_LEN] = {};
    RETURN_IF_ERROR(ctx.readArgs(Required("key", key)));

    JsonObject resp(ctx.out);

    if (!auth_.ValidateKey(key))
    {
        resp.field("ok", false);
        return RequestError::Ok;
    }

    if (ctx.connection) ctx.connection->authenticate(key);
    resp.field("ok", true);
    return RequestError::Ok;
}
