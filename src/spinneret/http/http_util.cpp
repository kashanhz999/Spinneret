#include "spinneret/http/http_util.h"

#include "spinneret/util/logging.h"

namespace spinneret::http {

namespace {
const util::Logger kLog{"http.auth"};
}

void send_json(httplib::Response& res, int status, const nlohmann::ordered_json& body) {
    res.status = status;
    res.set_content(body.dump(2), "application/json");
}

void send_error(httplib::Response& res, int status, std::string_view message) {
    send_json(res, status, {{"error", message}});
}

std::optional<std::string> header(const httplib::Request& req, const char* name) {
    if (!req.has_header(name)) return std::nullopt;
    return req.get_header_value(name);
}

bool authorize(const httplib::Request& req, httplib::Response& res, const security::ApiKeyService& keys) {
    auto key = header(req, kApiKeyHeader);
    if (key && keys.is_valid(*key)) return true;

    kLog.warn("Unauthorized request {} {} from {}", req.method, req.path, req.remote_addr);
    send_error(res, 401, "Missing or invalid API key");
    return false;
}

int status_for(pipeline::ErrorKind kind) {
    switch (kind) {
        case pipeline::ErrorKind::InvalidXml: return 400;
        case pipeline::ErrorKind::UnknownMessageType: return 422;
        case pipeline::ErrorKind::PublishFailed: return 502;
        case pipeline::ErrorKind::TransformFailed:
        case pipeline::ErrorKind::Internal: return 500;
    }
    return 500;
}

}  // namespace spinneret::http
