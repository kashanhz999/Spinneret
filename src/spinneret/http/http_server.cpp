#include "spinneret/http/http_server.h"

#include <stdexcept>

#include "spinneret/http/http_util.h"
#include "spinneret/util/logging.h"

namespace spinneret::http {

namespace {
const util::Logger kLog{"http"};
}

HttpServer::HttpServer(ServerOptions options)
    : options_(std::move(options)), server_(std::make_unique<httplib::Server>()) {
    server_->set_payload_max_length(options_.max_body_bytes);

    server_->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string message = "Internal server error";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            message = e.what();
        } catch (...) {
        }
        kLog.error("Unhandled error on {} {}: {}", req.method, req.path, message);
        send_error(res, 500, message);
    });

    server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.body.empty()) {
            send_error(res, res.status, res.status == 404 ? "Not found" : httplib::status_message(res.status));
        }
    });

    server_->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        kLog.debug("{} {} -> {}", req.method, req.path, res.status);
    });
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
    if (options_.port == 0) {
        port_ = server_->bind_to_any_port(options_.host);
        if (port_ <= 0) throw std::runtime_error("Cannot bind HTTP server to " + options_.host);
    } else {
        if (!server_->bind_to_port(options_.host, options_.port)) {
            throw std::runtime_error("Cannot bind HTTP server to " + options_.host + ":" +
                                     std::to_string(options_.port));
        }
        port_ = options_.port;
    }

    listener_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();
    kLog.info("HTTP server listening on {}:{}", options_.host, port_);
}

void HttpServer::stop() {
    if (!listener_.joinable()) return;
    server_->stop();
    listener_.join();
    kLog.info("HTTP server stopped");
}

}  // namespace spinneret::http
