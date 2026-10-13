#include "spinneret/http/admin_routes.h"

#include "spinneret/http/http_util.h"

namespace spinneret::http {

namespace {

using json = nlohmann::ordered_json;

int status_for(const config::ConfigError& e) {
    switch (e.kind()) {
        case config::ConfigError::Kind::Invalid: return 400;
        case config::ConfigError::Kind::NotFound: return 404;
        case config::ConfigError::Kind::Io: return 500;
    }
    return 500;
}

json masked(const nlohmann::json& properties) {
    json out = json::object();
    for (const auto& [key, value] : properties.items()) {
        bool hide = config::ConfigService::is_secret(key) && value.is_string() && !value.get<std::string>().empty();
        out[key] = hide ? json("********") : json(value);
    }
    return out;
}

json change_result(const std::set<std::string>& changed, const std::vector<std::string>& touched) {
    return {{"changed", changed}, {"restarted", touched}};
}

}  // namespace

void register_admin_routes(httplib::Server& server, AdminDependencies deps) {
    // ── Configuration ────────────────────────────────────────────────────

    server.Get("/api/admin/config", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        send_json(res, 200, masked(deps.config.effective()));
    });

    server.Put("/api/admin/config", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            send_error(res, 400, "Request body must be a JSON object");
            return;
        }
        try {
            auto changed = deps.config.update(body);
            auto touched = deps.restart.handle_config_change(changed);
            send_json(res, 200, change_result(changed, touched));
        } catch (const config::ConfigError& e) {
            send_error(res, status_for(e), e.what());
        }
    });

    server.Post("/api/admin/config/restore", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        try {
            auto changed = deps.config.restore_backup();
            auto touched = deps.restart.handle_config_change(changed);
            send_json(res, 200, change_result(changed, touched));
        } catch (const config::ConfigError& e) {
            send_error(res, status_for(e), e.what());
        }
    });

    server.Get("/api/admin/config/templates", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        send_json(res, 200, json(deps.templates.list()));
    });

    server.Post("/api/admin/config/template/:name", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        try {
            auto changed = deps.templates.apply(req.path_params.at("name"));
            auto touched = deps.restart.handle_config_change(changed);
            send_json(res, 200, change_result(changed, touched));
        } catch (const config::ConfigError& e) {
            send_error(res, status_for(e), e.what());
        }
    });

    // ── Adapters ─────────────────────────────────────────────────────────

    server.Get("/api/admin/adapters", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        json inputs = json::array();
        for (auto* a : deps.registry.inputs()) {
            inputs.push_back({{"name", a->name()},
                              {"type", a->type()},
                              {"enabled", a->enabled()},
                              {"running", a->running()},
                              {"healthy", a->healthy()},
                              {"detail", a->status_detail()}});
        }
        json outputs = json::array();
        for (auto* o : deps.registry.outputs()) {
            outputs.push_back({{"name", o->name()},
                               {"type", o->type()},
                               {"enabled", o->enabled()},
                               {"required", o->required()},
                               {"healthy", o->healthy()},
                               {"detail", o->status_detail()}});
        }
        send_json(res, 200, {{"inputs", inputs}, {"outputs", outputs}});
    });

    server.Post("/api/admin/adapters/:name/start", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        const auto& name = req.path_params.at("name");
        auto* adapter = deps.registry.find_input(name);
        if (!adapter) {
            send_error(res, 404, "No input adapter named '" + name + "'");
            return;
        }
        auto result = adapter->start();
        json body{{"name", name}, {"result", ports::to_string(result)}};
        switch (result) {
            case ports::StartResult::Started:
            case ports::StartResult::AlreadyRunning: send_json(res, 200, body); break;
            case ports::StartResult::Disabled:
                body["error"] = "Adapter is disabled; set its 'enabled' key to true via PUT /api/admin/config";
                send_json(res, 409, body);
                break;
            case ports::StartResult::Failed:
                body["error"] = adapter->status_detail();
                send_json(res, 500, body);
                break;
        }
    });

    server.Post("/api/admin/adapters/:name/stop", [deps](const httplib::Request& req, httplib::Response& res) {
        if (!authorize(req, res, deps.keys)) return;
        const auto& name = req.path_params.at("name");
        auto* adapter = deps.registry.find_input(name);
        if (!adapter) {
            send_error(res, 404, "No input adapter named '" + name + "'");
            return;
        }
        adapter->stop();
        send_json(res, 200, {{"name", name}, {"result", "STOPPED"}});
    });
}

}  // namespace spinneret::http
