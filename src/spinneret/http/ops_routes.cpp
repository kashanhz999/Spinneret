#include "spinneret/http/ops_routes.h"

#include "spinneret/http/http_util.h"
#include "spinneret/version.h"

namespace spinneret::http {

namespace {

using json = nlohmann::ordered_json;

constexpr const char* kSwaggerPage = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Spinneret API</title>
  <link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@5.17.14/swagger-ui.css">
</head>
<body>
  <div id="swagger-ui"></div>
  <script src="https://unpkg.com/swagger-ui-dist@5.17.14/swagger-ui-bundle.js"></script>
  <script>
    window.ui = SwaggerUIBundle({ url: "/openapi.json", dom_id: "#swagger-ui", tryItOutEnabled: true });
  </script>
</body>
</html>
)HTML";

}  // namespace

void register_ops_routes(httplib::Server& server, const registry::AdapterRegistry& registry,
                         const metrics::Metrics& metrics) {
    server.Get("/health", [&registry](const httplib::Request&, httplib::Response& res) {
        bool degraded = false;
        json adapters = json::object();
        for (auto* a : registry.inputs()) {
            if (!a->enabled()) continue;
            bool ok = a->healthy();
            degraded |= !ok;
            adapters[a->name()] = {{"healthy", ok}, {"detail", a->status_detail()}};
        }
        for (auto* o : registry.outputs()) {
            if (!o->enabled()) continue;
            bool ok = o->healthy();
            degraded |= !ok;
            adapters[o->name()] = {{"healthy", ok}, {"detail", o->status_detail()}};
        }
        send_json(res, degraded ? 503 : 200,
                  {{"status", degraded ? "DEGRADED" : "UP"}, {"version", kVersion}, {"adapters", adapters}});
    });

    server.Get("/health/live", [](const httplib::Request&, httplib::Response& res) {
        send_json(res, 200, {{"status", "UP"}});
    });

    server.Get("/metrics", [&metrics](const httplib::Request&, httplib::Response& res) {
        res.set_content(metrics.render_prometheus(), "text/plain; version=0.0.4; charset=utf-8");
    });

    server.Get("/info", [](const httplib::Request&, httplib::Response& res) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        constexpr bool kTls = true;
#else
        constexpr bool kTls = false;
#endif
        send_json(res, 200,
                  {{"name", kName}, {"version", kVersion}, {"features", {{"ibmmq", kWithIbmMq}, {"httpsWebhook", kTls}}}});
    });

    server.Get("/openapi.json", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(openapi_spec()), "application/json");
    });

    server.Get("/docs", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kSwaggerPage, "text/html; charset=utf-8");
    });
}

}  // namespace spinneret::http
