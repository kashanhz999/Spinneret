#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include "spinneret/adapters/mq/mq_session.h"
#include "spinneret/config/config_service.h"
#include "spinneret/metrics/metrics.h"
#include "spinneret/pipeline/pipeline_engine.h"
#include "spinneret/ports/input_adapter.h"

namespace spinneret::adapters {

/// Consumes XML from an IBM MQ queue on a dedicated thread.
///
/// Every message is processed in a unit of work:
///
///  - success                      → commit
///  - transient failure, retries left → roll back (redelivered after a short pause)
///  - permanent failure, or last attempt → give up: move to the backout queue
///    and commit. With no backout queue, commit only if the PIPELINE_ERROR
///    event (which carries the original XML) reached an output; otherwise log
///    the XML and discard on the last attempt.
///
/// A PIPELINE_ERROR event is published once per message: for permanent
/// failures immediately, for transient ones only on the last attempt.
class IbmMqInputAdapter final : public ports::InputAdapter {
public:
    enum class Disposition { Committed, RolledBack, MovedToBackout, Discarded };

    struct Outcome {
        Disposition disposition;
        std::chrono::milliseconds retry_delay{0};
    };

    IbmMqInputAdapter(const pipeline::PipelineEngine& engine, const config::ConfigService& config,
                      mq::MqSessionFactory& factory, metrics::Metrics& metrics);
    ~IbmMqInputAdapter() override;

    std::string name() const override { return "IbmMqInput"; }
    std::string type() const override { return "IbmMq"; }
    ports::StartResult start() override;
    void stop() override;
    bool enabled() const override;
    bool running() const override { return running_; }
    bool healthy() const override;
    std::string status_detail() const override;

    /// Processes one received message and settles its unit of work. Exposed
    /// for tests.
    Outcome handle_message(mq::MqInputSession& session, const mq::MqMessage& message);

private:
    void run(std::stop_token stop);
    void set_status(bool connected, std::string detail);
    mq::MqInputOptions input_options() const;

    const pipeline::PipelineEngine& engine_;
    const config::ConfigService& config_;
    mq::MqSessionFactory& factory_;
    metrics::Metrics& metrics_;

    std::mutex lifecycle_mutex_;
    std::jthread worker_;
    std::atomic<bool> running_{false};

    mutable std::mutex status_mutex_;
    bool connected_ = false;
    std::string detail_ = "stopped";
};

}  // namespace spinneret::adapters
