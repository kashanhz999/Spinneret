#pragma once

#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace spinneret::http {

struct ServerOptions {
    std::string host = "0.0.0.0";
    int port = 8080;  ///< 0 picks a free port (useful in tests).
    std::size_t max_body_bytes = 10 * 1024 * 1024;
};

/// Owns the HTTP server and its listener thread. Routes are registered on
/// server() before start().
class HttpServer {
public:
    explicit HttpServer(ServerOptions options);
    ~HttpServer();

    httplib::Server& server() { return *server_; }

    /// Binds and starts listening in the background. Throws on bind failure.
    void start();
    void stop();

    int port() const { return port_; }

private:
    ServerOptions options_;
    std::unique_ptr<httplib::Server> server_;
    std::thread listener_;
    int port_ = 0;
};

}  // namespace spinneret::http
