// Spinneret — XML processing middleware for airport integration.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "spinneret/app/application.h"
#include "spinneret/pipeline/diagnostics.h"
#include "spinneret/security/api_key_service.h"
#include "spinneret/util/files.h"
#include "spinneret/util/logging.h"
#include "spinneret/version.h"

namespace {

using namespace spinneret;

volatile std::sig_atomic_t g_shutdown = 0;

void on_signal(int) { g_shutdown = 1; }

constexpr const char* kUsage = R"(Usage:
  spinneret [serve] [options]          Run the service
  spinneret process <file.xml>         Run the pipeline on a file (dry run) and print the report
  spinneret keys create <name>         Create an API key (printed once)
  spinneret keys list                  List API keys
  spinneret keys revoke <id>           Revoke an API key
  spinneret --version

Options (environment variable in brackets):
  --config-dir <dir>   Configuration directory [SPINNERET_CONFIG_DIR] (default: config)
  --host <addr>        HTTP bind address       [SPINNERET_HOST]       (default: 0.0.0.0)
  --port <n>           HTTP port               [SPINNERET_PORT]       (default: 8080)
  --log-level <lvl>    debug|info|warn|error   [SPINNERET_LOG_LEVEL]  (default: info)
)";

struct Cli {
    std::vector<std::string> positional;
    app::AppOptions options;
    util::LogLevel log_level = util::LogLevel::Info;
};

std::string env_or(const char* name, std::string fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::move(fallback);
}

int parse_port(const std::string& text) {
    try {
        std::size_t used = 0;
        int port = std::stoi(text, &used);
        if (used == text.size() && port >= 0 && port <= 65535) return port;
    } catch (const std::exception&) {
    }
    throw std::invalid_argument("Invalid port: " + text);
}

util::LogLevel parse_level(const std::string& text) {
    auto level = util::parse_log_level(text);
    if (!level) throw std::invalid_argument("Invalid log level: " + text);
    return *level;
}

Cli parse_cli(int argc, char** argv) {
    Cli cli;
    cli.options.config_dir = env_or("SPINNERET_CONFIG_DIR", "config");
    cli.options.server.host = env_or("SPINNERET_HOST", "0.0.0.0");
    cli.options.server.port = parse_port(env_or("SPINNERET_PORT", "8080"));
    cli.log_level = parse_level(env_or("SPINNERET_LOG_LEVEL", "info"));

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument(arg + " requires a value");
            return argv[++i];
        };
        if (arg == "--config-dir") {
            cli.options.config_dir = value();
        } else if (arg == "--host") {
            cli.options.server.host = value();
        } else if (arg == "--port") {
            cli.options.server.port = parse_port(value());
        } else if (arg == "--log-level") {
            cli.log_level = parse_level(value());
        } else if (arg.starts_with("--")) {
            throw std::invalid_argument("Unknown option: " + arg);
        } else {
            cli.positional.push_back(arg);
        }
    }
    return cli;
}

int serve(const Cli& cli) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    app::Application application(cli.options);
    application.start();
    while (!g_shutdown) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    application.stop();
    return 0;
}

int process(const Cli& cli) {
    if (cli.positional.size() != 2) throw std::invalid_argument("process requires exactly one file");
    auto xml = util::read_file(cli.positional[1]);
    if (!xml) throw std::invalid_argument("File not found: " + cli.positional[1]);

    util::Log::set_level(util::LogLevel::Warn);  // keep stdout for the report
    metrics::Metrics metrics;
    registry::AdapterRegistry registry;
    pipeline::PipelineEngine engine(pipeline::TransformerRegistry(pipeline::default_transformers()), registry, metrics);

    pipeline::PipelineContext context(*xml, domain::CorrelationMetadata::create(std::nullopt, "CLI"));
    engine.ingest(context, {.publish = false, .error_events = pipeline::ErrorEventPolicy::Never});
    std::cout << pipeline::diagnostic_report(context, false).dump(2) << '\n';
    return context.failed() ? 1 : 0;
}

int keys(const Cli& cli) {
    if (cli.positional.size() < 2) throw std::invalid_argument("keys requires a subcommand");
    const std::string& action = cli.positional[1];

    std::filesystem::create_directories(cli.options.config_dir);
    security::ApiKeyService service(cli.options.config_dir / "api-keys.json");
    service.load();

    if (action == "create" && cli.positional.size() == 3) {
        std::string key = service.create(cli.positional[2]);
        std::cout << key << '\n';
        std::cerr << "Store this key now; it cannot be shown again.\n";
        return 0;
    }
    if (action == "list" && cli.positional.size() == 2) {
        for (const auto& k : service.list()) {
            std::cout << k.id << "  " << k.prefix << "...  " << util::iso8601_utc(k.created_at) << "  "
                      << (k.active ? "active " : "revoked") << "  " << k.name << '\n';
        }
        return 0;
    }
    if (action == "revoke" && cli.positional.size() == 3) {
        if (service.revoke(cli.positional[2])) return 0;
        std::cerr << "No key with id " << cli.positional[2] << '\n';
        return 1;
    }
    throw std::invalid_argument("Unknown keys command");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << kUsage;
                return 0;
            }
            if (arg == "--version") {
                std::cout << kName << ' ' << kVersion << (kWithIbmMq ? " (IBM MQ)" : "") << '\n';
                return 0;
            }
        }

        Cli cli = parse_cli(argc, argv);
        util::Log::set_level(cli.log_level);

        const std::string command = cli.positional.empty() ? "serve" : cli.positional.front();
        if (command == "serve") return serve(cli);
        if (command == "process") return process(cli);
        if (command == "keys") return keys(cli);

        std::cerr << "Unknown command: " << command << "\n\n" << kUsage;
        return 2;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << "\n\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 1;
    }
}
