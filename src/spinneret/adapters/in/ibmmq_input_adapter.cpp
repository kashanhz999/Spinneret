#include "spinneret/adapters/in/ibmmq_input_adapter.h"

#include <algorithm>
#include <condition_variable>

#include "spinneret/util/logging.h"

namespace spinneret::adapters {

namespace {

using namespace std::chrono_literals;

const util::Logger kLog{"adapter.ibmmq.in"};

constexpr auto kGetWait = 1000ms;
constexpr auto kMaxReconnectDelay = std::chrono::milliseconds(60s);
constexpr auto kMaxRetryDelay = std::chrono::milliseconds(30s);

/// Sleeps for `duration` unless a stop is requested. Returns false if stopped.
bool interruptible_sleep(std::stop_token stop, std::chrono::milliseconds duration) {
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock(mutex);
    cv.wait_for(lock, stop, duration, [] { return false; });
    return !stop.stop_requested();
}

}  // namespace

IbmMqInputAdapter::IbmMqInputAdapter(const pipeline::PipelineEngine& engine, const config::ConfigService& config,
                                     mq::MqSessionFactory& factory, metrics::Metrics& metrics)
    : engine_(engine), config_(config), factory_(factory), metrics_(metrics) {}

IbmMqInputAdapter::~IbmMqInputAdapter() { stop(); }

bool IbmMqInputAdapter::enabled() const { return config_.get_bool("adapters.input.ibmmq.enabled"); }

bool IbmMqInputAdapter::healthy() const {
    std::lock_guard lock(status_mutex_);
    return running_ && connected_;
}

std::string IbmMqInputAdapter::status_detail() const {
    std::lock_guard lock(status_mutex_);
    return detail_;
}

void IbmMqInputAdapter::set_status(bool connected, std::string detail) {
    std::lock_guard lock(status_mutex_);
    connected_ = connected;
    detail_ = std::move(detail);
}

mq::MqInputOptions IbmMqInputAdapter::input_options() const {
    return mq::MqInputOptions{
        .queue = config_.get_string("adapters.input.ibmmq.queue"),
        .backout_threshold_override = static_cast<int>(config_.get_int("adapters.input.ibmmq.backoutThreshold")),
        .backout_queue_override = config_.get_string("adapters.input.ibmmq.backoutQueue"),
    };
}

ports::StartResult IbmMqInputAdapter::start() {
    std::lock_guard lock(lifecycle_mutex_);
    if (!enabled()) {
        kLog.info("IbmMqInput is disabled in the configuration");
        return ports::StartResult::Disabled;
    }
    if (worker_.joinable()) return ports::StartResult::AlreadyRunning;
    if (!factory_.available()) {
        set_status(false, "Spinneret was built without IBM MQ support");
        kLog.error("Cannot start IbmMqInput: Spinneret was built without IBM MQ support");
        return ports::StartResult::Failed;
    }

    running_ = true;
    set_status(false, "connecting");
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
    kLog.info("IbmMqInput STARTED");
    return ports::StartResult::Started;
}

void IbmMqInputAdapter::stop() {
    std::lock_guard lock(lifecycle_mutex_);
    if (!worker_.joinable()) return;
    worker_.request_stop();
    worker_.join();
    worker_ = std::jthread();
    running_ = false;
    set_status(false, "stopped");
    kLog.info("IbmMqInput STOPPED");
}

void IbmMqInputAdapter::run(std::stop_token stop) {
    std::chrono::milliseconds reconnect_delay = 1s;

    while (!stop.stop_requested()) {
        std::unique_ptr<mq::MqInputSession> session;
        auto options = input_options();
        try {
            session = factory_.open_input(mq::MqConnectionSettings::from_config(config_), options);
            set_status(true, "consuming from " + options.queue);
            reconnect_delay = 1s;
        } catch (const std::exception& e) {
            set_status(false, e.what());
            kLog.error("Cannot open MQ input on '{}': {} (retrying in {}s)", options.queue, e.what(),
                       std::chrono::duration_cast<std::chrono::seconds>(reconnect_delay).count());
            if (!interruptible_sleep(stop, reconnect_delay)) break;
            reconnect_delay = std::min(reconnect_delay * 2, kMaxReconnectDelay);
            continue;
        }

        try {
            while (!stop.stop_requested()) {
                auto message = session->get(kGetWait);
                if (!message) continue;
                Outcome outcome = handle_message(*session, *message);
                if (outcome.retry_delay.count() > 0 && !interruptible_sleep(stop, outcome.retry_delay)) break;
            }
        } catch (const std::exception& e) {
            set_status(false, e.what());
            kLog.error("MQ input error: {} (reconnecting)", e.what());
            session.reset();  // backs out any open unit of work
            if (!interruptible_sleep(stop, 1s)) break;
        }
    }
}

IbmMqInputAdapter::Outcome IbmMqInputAdapter::handle_message(mq::MqInputSession& session,
                                                             const mq::MqMessage& message) {
    metrics_.record_received(name());

    const int attempt = message.backout_count + 1;
    const int threshold = std::max(1, session.backout_threshold());
    const bool last_attempt = attempt >= threshold;

    pipeline::PipelineContext context(message.body,
                                      domain::CorrelationMetadata::create(message.trace_id, "IBM_MQ"));
    engine_.ingest(context, {.publish = true,
                             .error_events = last_attempt ? pipeline::ErrorEventPolicy::Always
                                                          : pipeline::ErrorEventPolicy::PermanentOnly});

    util::TraceScope trace(context.correlation().trace_id);

    if (!context.error) {
        session.commit();
        return {Disposition::Committed};
    }

    const bool permanent = pipeline::is_permanent(context.error->kind());
    if (!permanent && !last_attempt) {
        session.rollback();
        auto delay = std::min(std::chrono::milliseconds(2s) * attempt, kMaxRetryDelay);
        kLog.warn("Attempt {}/{} failed ({}); rolled back for redelivery", attempt, threshold,
                  context.error->what());
        return {Disposition::RolledBack, delay};
    }

    // Giving up on this message.
    if (!session.backout_queue().empty()) {
        try {
            session.move_to_backout();
            session.commit();
            kLog.warn("Moved message to backout queue '{}' after attempt {}/{}", session.backout_queue(), attempt,
                      threshold);
            return {Disposition::MovedToBackout};
        } catch (const mq::MqError& e) {
            if (e.connection_lost()) throw;
            kLog.error("Could not move message to backout queue '{}': {}", session.backout_queue(), e.what());
        }
    }

    if (context.error_event_delivered) {
        session.commit();
        kLog.warn("Discarded message after attempt {}/{}; its XML was published in a PIPELINE_ERROR event",
                  attempt, threshold);
        return {Disposition::Discarded};
    }

    if (!last_attempt) {
        session.rollback();
        return {Disposition::RolledBack, std::min(std::chrono::milliseconds(2s) * attempt, kMaxRetryDelay)};
    }

    kLog.error("Discarding message after attempt {}/{}: no backout queue and no output accepted the "
               "PIPELINE_ERROR event. Original message follows:\n{}",
               attempt, threshold, message.body);
    session.commit();
    return {Disposition::Discarded};
}

}  // namespace spinneret::adapters
