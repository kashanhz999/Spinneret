#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "spinneret/pipeline/pipeline_error.h"
#include "spinneret/security/api_key_service.h"

namespace spinneret::http {

inline constexpr const char* kApiKeyHeader = "X-API-Key";
inline constexpr const char* kTraceHeader = "X-Trace-Id";

void send_json(httplib::Response& res, int status, const nlohmann::ordered_json& body);
void send_error(httplib::Response& res, int status, std::string_view message);

/// Checks the X-API-Key header; on failure writes a 401 response and returns false.
bool authorize(const httplib::Request& req, httplib::Response& res, const security::ApiKeyService& keys);

/// HTTP status for a failed message:
/// 400 invalid XML, 422 unsupported type, 502 delivery failed, 500 otherwise.
int status_for(pipeline::ErrorKind kind);

std::optional<std::string> header(const httplib::Request& req, const char* name);

}  // namespace spinneret::http
