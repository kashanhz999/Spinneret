#pragma once

#include "spinneret/config/config_service.h"
#include "spinneret/config/template_service.h"
#include "spinneret/lifecycle/smart_restart.h"
#include "spinneret/registry/adapter_registry.h"
#include "spinneret/security/api_key_service.h"

namespace httplib {
class Server;
}

namespace spinneret::http {

struct AdminDependencies {
    config::ConfigService& config;
    config::TemplateService& templates;
    registry::AdapterRegistry& registry;
    lifecycle::SmartRestart& restart;
    const security::ApiKeyService& keys;
};

/// `/api/admin/**` — configuration and adapter lifecycle. All routes require
/// an API key.
///
///  GET  /api/admin/config                     effective configuration (secrets masked)
///  PUT  /api/admin/config                     update keys; affected adapters restart
///  POST /api/admin/config/restore             swap with the backup
///  GET  /api/admin/config/templates           available templates
///  POST /api/admin/config/template/{name}     apply a template
///  GET  /api/admin/adapters                   adapter inventory and health
///  POST /api/admin/adapters/{name}/start      start an input adapter
///  POST /api/admin/adapters/{name}/stop       stop an input adapter
void register_admin_routes(httplib::Server& server, AdminDependencies deps);

}  // namespace spinneret::http
