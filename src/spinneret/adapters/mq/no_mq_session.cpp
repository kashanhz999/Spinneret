// Used when Spinneret is built without SPINNERET_WITH_IBMMQ.

#include "spinneret/adapters/mq/mq_session.h"

namespace spinneret::adapters::mq {

namespace {

constexpr const char* kUnavailable =
    "Spinneret was built without IBM MQ support (reconfigure with -DSPINNERET_WITH_IBMMQ=ON)";

class UnavailableFactory final : public MqSessionFactory {
public:
    bool available() const override { return false; }

    std::unique_ptr<MqInputSession> open_input(const MqConnectionSettings&, const MqInputOptions&) override {
        throw MqError(kUnavailable);
    }

    std::unique_ptr<MqOutputSession> open_output(const MqConnectionSettings&) override {
        throw MqError(kUnavailable);
    }
};

}  // namespace

std::unique_ptr<MqSessionFactory> make_default_factory() { return std::make_unique<UnavailableFactory>(); }

}  // namespace spinneret::adapters::mq
