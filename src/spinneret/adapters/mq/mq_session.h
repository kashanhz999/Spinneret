#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spinneret::config {
class ConfigService;
}

namespace spinneret::adapters::mq {

/// Queue manager connection settings, read from the `ibm.mq.*` config keys.
struct MqConnectionSettings {
    std::string queue_manager;
    std::string channel;
    std::string conn_name;  ///< "host(port)" or a comma-separated list for multi-instance QMs.
    std::string user;
    std::string password;   ///< SPINNERET_MQ_PASSWORD overrides ibm.mq.password.
    std::string ssl_cipher_spec;
    std::string ssl_key_repository;

    static MqConnectionSettings from_config(const config::ConfigService& config);
};

struct MqInputOptions {
    std::string queue;
    int backout_threshold_override = 0;  ///< 0 = use the queue's BOTHRESH.
    std::string backout_queue_override;  ///< empty = use the queue's BOQNAME.
};

struct MqMessage {
    std::string body;
    int backout_count = 0;                ///< Times this message was previously rolled back.
    std::optional<std::string> trace_id;  ///< AODB_TRACE_ID message property, if set.
};

/// MQRC values that mean the connection is gone. Kept free of cmqc.h so the
/// rest of the code builds without the MQ SDK; mqi_session.cpp checks them
/// against cmqc.h at compile time.
namespace reason {
inline constexpr int kConnectionBroken = 2009;
inline constexpr int kHconnError = 2018;
inline constexpr int kQMgrNotAvailable = 2059;
inline constexpr int kQMgrQuiescing = 2161;
inline constexpr int kQMgrStopping = 2162;
inline constexpr int kConnectionQuiescing = 2202;
inline constexpr int kConnectionStopping = 2203;
inline constexpr int kChannelNotAvailable = 2537;
inline constexpr int kHostNotAvailable = 2538;
}  // namespace reason

/// An MQI failure. `reason` is the MQRC code.
class MqError : public std::runtime_error {
public:
    MqError(const std::string& operation, int completion_code, int reason);
    explicit MqError(const std::string& message) : std::runtime_error(message) {}

    int reason() const noexcept { return reason_; }

    /// True if the connection is unusable and must be re-established.
    bool connection_lost() const noexcept;

private:
    int completion_code_ = 0;
    int reason_ = 0;
};

/// A consuming connection to one queue. All gets happen under syncpoint; the
/// caller ends each unit of work with commit() or rollback().
class MqInputSession {
public:
    virtual ~MqInputSession() = default;

    /// Waits up to `wait` for a message. Returns nullopt on timeout.
    virtual std::optional<MqMessage> get(std::chrono::milliseconds wait) = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    /// Puts the current message (with its properties) on the backout queue in
    /// the same unit of work. The caller then commits.
    virtual void move_to_backout() = 0;

    /// Deliveries after which a message is given up on (always >= 1).
    virtual int backout_threshold() const = 0;
    virtual std::string backout_queue() const = 0;  ///< Empty if none is configured.
};

/// A producing connection. Puts are outside syncpoint. Not thread-safe.
class MqOutputSession {
public:
    virtual ~MqOutputSession() = default;

    /// Puts a UTF-8 string message with the given string properties.
    virtual void put(const std::string& queue, std::string_view body,
                     const std::vector<std::pair<std::string, std::string>>& properties) = 0;
};

class MqSessionFactory {
public:
    virtual ~MqSessionFactory() = default;

    /// False when Spinneret was built without the IBM MQ client.
    virtual bool available() const = 0;

    virtual std::unique_ptr<MqInputSession> open_input(const MqConnectionSettings& settings,
                                                       const MqInputOptions& options) = 0;
    virtual std::unique_ptr<MqOutputSession> open_output(const MqConnectionSettings& settings) = 0;
};

/// The real MQI factory when built with SPINNERET_WITH_IBMMQ, otherwise a
/// factory whose sessions fail with an explanatory error.
std::unique_ptr<MqSessionFactory> make_default_factory();

}  // namespace spinneret::adapters::mq
