#include "spinneret/adapters/mq/mq_session.h"

#include <cstdlib>
#include <format>

#include "spinneret/config/config_service.h"

namespace spinneret::adapters::mq {

MqError::MqError(const std::string& operation, int completion_code, int reason_code)
    : std::runtime_error(std::format("{} failed: CompCode={}, Reason={}", operation, completion_code, reason_code)),
      completion_code_(completion_code),
      reason_(reason_code) {}

bool MqError::connection_lost() const noexcept {
    switch (reason_) {
        case reason::kConnectionBroken:
        case reason::kHconnError:
        case reason::kQMgrNotAvailable:
        case reason::kQMgrQuiescing:
        case reason::kQMgrStopping:
        case reason::kConnectionQuiescing:
        case reason::kConnectionStopping:
        case reason::kChannelNotAvailable:
        case reason::kHostNotAvailable: return true;
        default: return false;
    }
}

MqConnectionSettings MqConnectionSettings::from_config(const config::ConfigService& config) {
    MqConnectionSettings s;
    s.queue_manager = config.get_string("ibm.mq.queueManager");
    s.channel = config.get_string("ibm.mq.channel");
    s.conn_name = config.get_string("ibm.mq.connName");
    s.user = config.get_string("ibm.mq.user");
    s.password = config.get_string("ibm.mq.password");
    if (const char* env = std::getenv("SPINNERET_MQ_PASSWORD"); env && *env) s.password = env;
    s.ssl_cipher_spec = config.get_string("ibm.mq.sslCipherSpec");
    s.ssl_key_repository = config.get_string("ibm.mq.sslKeyRepository");
    return s;
}

}  // namespace spinneret::adapters::mq
