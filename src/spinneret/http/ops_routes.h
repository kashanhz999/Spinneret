#pragma once

#include <string_view>

#include "spinneret/metrics/metrics.h"
#include "spinneret/registry/adapter_registry.h"

namespace httplib {
class Server;
}

namespace spinneret::http {

/// The OpenAPI document, embedded at build time from resources/openapi.json.
std::string_view openapi_spec();

/// Unauthenticated operational endpoints:
///
///  GET /health        200 UP, or 503 DEGRADED if an enabled adapter is unhealthy
///  GET /health/live   200 while the process is serving
///  GET /metrics       Prometheus text format
///  GET /info          name, version and build features
///  GET /openapi.json  API description
///  GET /docs          Swagger UI
void register_ops_routes(httplib::Server& server, const registry::AdapterRegistry& registry,
                         const metrics::Metrics& metrics);

}  // namespace spinneret::http
