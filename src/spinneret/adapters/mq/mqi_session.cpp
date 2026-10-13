// IBM MQ implementation of the session interfaces, using the MQI C client.
// Compiled only when SPINNERET_WITH_IBMMQ is ON.

#include <algorithm>
#include <cstring>
#include <format>
#include <unordered_map>

#include <cmqc.h>
#include <cmqxc.h>

#include "spinneret/adapters/mq/mq_session.h"
#include "spinneret/util/logging.h"
#include "spinneret/util/strings.h"

namespace spinneret::adapters::mq {

static_assert(reason::kConnectionBroken == MQRC_CONNECTION_BROKEN);
static_assert(reason::kHconnError == MQRC_HCONN_ERROR);
static_assert(reason::kQMgrNotAvailable == MQRC_Q_MGR_NOT_AVAILABLE);
static_assert(reason::kQMgrQuiescing == MQRC_Q_MGR_QUIESCING);
static_assert(reason::kQMgrStopping == MQRC_Q_MGR_STOPPING);
static_assert(reason::kConnectionQuiescing == MQRC_CONNECTION_QUIESCING);
static_assert(reason::kConnectionStopping == MQRC_CONNECTION_STOPPING);
static_assert(reason::kChannelNotAvailable == MQRC_CHANNEL_NOT_AVAILABLE);
static_assert(reason::kHostNotAvailable == MQRC_HOST_NOT_AVAILABLE);

namespace {

const util::Logger kLog{"mq.mqi"};

constexpr MQLONG kUtf8 = 1208;
constexpr std::size_t kInitialBuffer = 64 * 1024;

/// MQ character fields are fixed-length and blank-padded.
void copy_field(MQCHAR* field, std::size_t length, std::string_view value, const char* what) {
    if (value.size() > length) {
        throw MqError(std::format("{} '{}' is longer than {} characters", what, value, length));
    }
    std::memset(field, ' ', length);
    std::memcpy(field, value.data(), value.size());
}

std::string read_field(const MQCHAR* field, std::size_t length) {
    std::string_view view(field, length);
    if (auto nul = view.find('\0'); nul != std::string_view::npos) view = view.substr(0, nul);
    return std::string(util::trim(view));
}

void check(const char* operation, MQLONG cc, MQLONG rc) {
    if (cc == MQCC_FAILED) throw MqError(operation, static_cast<int>(cc), static_cast<int>(rc));
    if (cc == MQCC_WARNING) kLog.warn("{} completed with warning, Reason={}", operation, rc);
}

class Connection {
public:
    Connection(const MqConnectionSettings& s, bool shared_handle) {
        MQCNO cno = {MQCNO_DEFAULT};
        MQCD cd = {MQCD_CLIENT_CONN_DEFAULT};
        MQCSP csp = {MQCSP_DEFAULT};
        MQSCO sco = {MQSCO_DEFAULT};

        cd.Version = MQCD_CURRENT_VERSION;
        cd.TransportType = MQXPT_TCP;
        copy_field(cd.ChannelName, MQ_CHANNEL_NAME_LENGTH, s.channel, "ibm.mq.channel");
        copy_field(cd.ConnectionName, MQ_CONN_NAME_LENGTH, s.conn_name, "ibm.mq.connName");

        cno.Version = MQCNO_VERSION_5;
        cno.ClientConnPtr = &cd;
        if (shared_handle) cno.Options |= MQCNO_HANDLE_SHARE_BLOCK;

        if (!s.user.empty()) {
            csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
            csp.CSPUserIdPtr = const_cast<char*>(s.user.data());
            csp.CSPUserIdLength = static_cast<MQLONG>(s.user.size());
            csp.CSPPasswordPtr = const_cast<char*>(s.password.data());
            csp.CSPPasswordLength = static_cast<MQLONG>(s.password.size());
            cno.SecurityParmsPtr = &csp;
        }

        if (!s.ssl_cipher_spec.empty()) {
            copy_field(cd.SSLCipherSpec, MQ_SSL_CIPHER_SPEC_LENGTH, s.ssl_cipher_spec, "ibm.mq.sslCipherSpec");
            if (!s.ssl_key_repository.empty()) {
                copy_field(sco.KeyRepository, MQ_SSL_KEY_REPOSITORY_LENGTH, s.ssl_key_repository,
                           "ibm.mq.sslKeyRepository");
                cno.SSLConfigPtr = &sco;
            }
        }

        MQCHAR48 qmgr;
        copy_field(qmgr, MQ_Q_MGR_NAME_LENGTH, s.queue_manager, "ibm.mq.queueManager");

        MQLONG cc = 0;
        MQLONG rc = 0;
        MQCONNX(qmgr, &cno, &hconn_, &cc, &rc);
        check("MQCONNX", cc, rc);
        kLog.info("Connected to queue manager '{}' via {} ({})", s.queue_manager, s.channel, s.conn_name);
    }

    ~Connection() {
        if (hconn_ == MQHC_UNUSABLE_HCONN) return;
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQDISC(&hconn_, &cc, &rc);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    MQHCONN handle() const { return hconn_; }

private:
    MQHCONN hconn_ = MQHC_UNUSABLE_HCONN;
};

/// RAII wrapper around a message handle (MQCRTMH / MQDLTMH).
class MessageHandle {
public:
    explicit MessageHandle(MQHCONN hconn) : hconn_(hconn) {
        MQCMHO cmho = {MQCMHO_DEFAULT};
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQCRTMH(hconn_, &cmho, &hmsg_, &cc, &rc);
        check("MQCRTMH", cc, rc);
    }

    ~MessageHandle() {
        if (hmsg_ == MQHM_UNUSABLE_HMSG) return;
        MQDMHO dmho = {MQDMHO_DEFAULT};
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQDLTMH(hconn_, &hmsg_, &dmho, &cc, &rc);
    }

    MessageHandle(const MessageHandle&) = delete;
    MessageHandle& operator=(const MessageHandle&) = delete;

    MQHMSG handle() const { return hmsg_; }

private:
    MQHCONN hconn_;
    MQHMSG hmsg_ = MQHM_UNUSABLE_HMSG;
};

// ── Input ────────────────────────────────────────────────────────────────────

class MqiInputSession final : public MqInputSession {
public:
    MqiInputSession(const MqConnectionSettings& settings, const MqInputOptions& options)
        : connection_(settings, /*shared_handle=*/false), queue_(options.queue) {
        open_queue();

        threshold_ = options.backout_threshold_override > 0
                         ? options.backout_threshold_override
                         : (queue_threshold_ > 0 ? queue_threshold_ : 3);
        backout_queue_ = !options.backout_queue_override.empty() ? options.backout_queue_override
                                                                 : queue_backout_queue_;

        message_ = std::make_unique<MessageHandle>(connection_.handle());
        buffer_.resize(kInitialBuffer);

        kLog.info("Consuming from '{}' (backout threshold {}, backout queue '{}')", queue_, threshold_,
                  backout_queue_.empty() ? "<none>" : backout_queue_);
    }

    ~MqiInputSession() override {
        MQLONG cc = 0;
        MQLONG rc = 0;
        // Never let MQDISC implicitly commit an open unit of work.
        MQBACK(connection_.handle(), &cc, &rc);
        message_.reset();
        if (hobj_ != MQHO_UNUSABLE_HOBJ) MQCLOSE(connection_.handle(), &hobj_, MQCO_NONE, &cc, &rc);
    }

    std::optional<MqMessage> get(std::chrono::milliseconds wait) override {
        MQMD md = {MQMD_DEFAULT};
        MQGMO gmo = {MQGMO_DEFAULT};
        gmo.Version = MQGMO_VERSION_4;
        gmo.Options = MQGMO_WAIT | MQGMO_SYNCPOINT | MQGMO_FAIL_IF_QUIESCING | MQGMO_CONVERT |
                      MQGMO_PROPERTIES_IN_HANDLE;
        gmo.MatchOptions = MQMO_NONE;
        gmo.WaitInterval = static_cast<MQLONG>(wait.count());
        gmo.MsgHandle = message_->handle();

        for (;;) {
            md.CodedCharSetId = kUtf8;  // request conversion to UTF-8
            md.Encoding = MQENC_NATIVE;

            MQLONG data_length = 0;
            MQLONG cc = 0;
            MQLONG rc = 0;
            MQGET(connection_.handle(), hobj_, &md, &gmo, static_cast<MQLONG>(buffer_.size()), buffer_.data(),
                  &data_length, &cc, &rc);

            if (rc == MQRC_NO_MSG_AVAILABLE) return std::nullopt;
            if (rc == MQRC_TRUNCATED_MSG_FAILED) {
                // Message stays on the queue; retry with room for UTF-8 expansion.
                buffer_.resize(std::max(buffer_.size() * 2, static_cast<std::size_t>(data_length) * 2));
                continue;
            }
            if (cc == MQCC_FAILED) throw MqError("MQGET", static_cast<int>(cc), static_cast<int>(rc));
            if (cc == MQCC_WARNING) {
                kLog.warn("MQGET warning, Reason={} (data may not have been converted to UTF-8)", rc);
            }

            const auto length = std::min(static_cast<std::size_t>(data_length), buffer_.size());
            current_md_ = md;
            current_body_.assign(buffer_.data(), length);
            has_current_ = true;

            return MqMessage{
                .body = current_body_,
                .backout_count = static_cast<int>(md.BackoutCount),
                .trace_id = read_string_property("AODB_TRACE_ID"),
            };
        }
    }

    void commit() override {
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQCMIT(connection_.handle(), &cc, &rc);
        has_current_ = false;
        check("MQCMIT", cc, rc);
    }

    void rollback() override {
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQBACK(connection_.handle(), &cc, &rc);
        has_current_ = false;
        check("MQBACK", cc, rc);
    }

    void move_to_backout() override {
        if (!has_current_) throw MqError("No current message to move to the backout queue");
        if (backout_queue_.empty()) throw MqError("No backout queue is configured for " + queue_);

        MQOD od = {MQOD_DEFAULT};
        copy_field(od.ObjectName, MQ_Q_NAME_LENGTH, backout_queue_, "backout queue");
        MQMD md = current_md_;
        MQPMO pmo = {MQPMO_DEFAULT};
        pmo.Version = MQPMO_VERSION_3;
        pmo.Options = MQPMO_SYNCPOINT | MQPMO_FAIL_IF_QUIESCING;
        pmo.OriginalMsgHandle = message_->handle();  // carry the message properties across

        MQLONG cc = 0;
        MQLONG rc = 0;
        MQPUT1(connection_.handle(), &od, &md, &pmo, static_cast<MQLONG>(current_body_.size()),
               current_body_.data(), &cc, &rc);
        check("MQPUT1 (backout queue)", cc, rc);
    }

    int backout_threshold() const override { return threshold_; }
    std::string backout_queue() const override { return backout_queue_; }

private:
    void open_queue() {
        MQOD od = {MQOD_DEFAULT};
        copy_field(od.ObjectName, MQ_Q_NAME_LENGTH, queue_, "adapters.input.ibmmq.queue");

        MQLONG cc = 0;
        MQLONG rc = 0;
        MQOPEN(connection_.handle(), &od, MQOO_INPUT_AS_Q_DEF | MQOO_INQUIRE | MQOO_FAIL_IF_QUIESCING, &hobj_, &cc,
               &rc);
        bool can_inquire = true;
        if (cc == MQCC_FAILED && rc == MQRC_NOT_AUTHORIZED) {
            kLog.warn("No inquire authority on '{}'; backout settings must come from the configuration", queue_);
            can_inquire = false;
            MQOPEN(connection_.handle(), &od, MQOO_INPUT_AS_Q_DEF | MQOO_FAIL_IF_QUIESCING, &hobj_, &cc, &rc);
        }
        check("MQOPEN", cc, rc);
        if (!can_inquire) return;

        MQLONG selectors[2] = {MQIA_BACKOUT_THRESHOLD, MQCA_BACKOUT_REQ_Q_NAME};
        MQLONG int_attrs[1] = {0};
        MQCHAR char_attrs[MQ_Q_NAME_LENGTH];
        MQINQ(connection_.handle(), hobj_, 2, selectors, 1, int_attrs, MQ_Q_NAME_LENGTH, char_attrs, &cc, &rc);
        if (cc == MQCC_FAILED) {
            kLog.warn("MQINQ on '{}' failed (Reason={}); using configured backout settings", queue_, rc);
            return;
        }
        queue_threshold_ = static_cast<int>(int_attrs[0]);
        queue_backout_queue_ = read_field(char_attrs, MQ_Q_NAME_LENGTH);
    }

    std::optional<std::string> read_string_property(const char* name) {
        MQIMPO impo = {MQIMPO_DEFAULT};
        MQPD pd = {MQPD_DEFAULT};
        MQCHARV property = {MQCHARV_DEFAULT};
        property.VSPtr = const_cast<char*>(name);
        property.VSLength = MQVS_NULL_TERMINATED;
        impo.Options = MQIMPO_CONVERT_VALUE | MQIMPO_CONVERT_TYPE;
        impo.RequestedCCSID = kUtf8;

        MQLONG type = MQTYPE_STRING;
        char value[256];
        MQLONG length = 0;
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQINQMP(connection_.handle(), message_->handle(), &impo, &property, &pd, &type,
                static_cast<MQLONG>(sizeof(value)), value, &length, &cc, &rc);
        if (cc == MQCC_FAILED) {
            if (rc != MQRC_PROPERTY_NOT_AVAILABLE) kLog.debug("MQINQMP({}) failed, Reason={}", name, rc);
            return std::nullopt;
        }
        return std::string(value, std::min(static_cast<std::size_t>(length), sizeof(value)));
    }

    Connection connection_;  // declared first: destroyed last
    std::string queue_;
    MQHOBJ hobj_ = MQHO_UNUSABLE_HOBJ;
    std::unique_ptr<MessageHandle> message_;
    int queue_threshold_ = 0;
    std::string queue_backout_queue_;
    int threshold_ = 3;
    std::string backout_queue_;
    std::vector<char> buffer_;
    MQMD current_md_ = {MQMD_DEFAULT};
    std::string current_body_;
    bool has_current_ = false;
};

// ── Output ───────────────────────────────────────────────────────────────────

class MqiOutputSession final : public MqOutputSession {
public:
    explicit MqiOutputSession(const MqConnectionSettings& settings)
        : connection_(settings, /*shared_handle=*/true) {}

    ~MqiOutputSession() override {
        for (auto& [name, hobj] : queues_) {
            MQLONG cc = 0;
            MQLONG rc = 0;
            MQCLOSE(connection_.handle(), &hobj, MQCO_NONE, &cc, &rc);
        }
    }

    void put(const std::string& queue, std::string_view body,
             const std::vector<std::pair<std::string, std::string>>& properties) override {
        MQHOBJ hobj = open_queue(queue);
        MessageHandle message(connection_.handle());

        for (const auto& [key, value] : properties) {
            MQSMPO smpo = {MQSMPO_DEFAULT};
            smpo.ValueCCSID = kUtf8;
            MQPD pd = {MQPD_DEFAULT};
            MQCHARV property = {MQCHARV_DEFAULT};
            property.VSPtr = const_cast<char*>(key.c_str());
            property.VSLength = MQVS_NULL_TERMINATED;

            MQLONG cc = 0;
            MQLONG rc = 0;
            MQSETMP(connection_.handle(), message.handle(), &smpo, &property, &pd, MQTYPE_STRING,
                    static_cast<MQLONG>(value.size()), const_cast<char*>(value.data()), &cc, &rc);
            check("MQSETMP", cc, rc);
        }

        MQMD md = {MQMD_DEFAULT};
        std::memcpy(md.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
        md.CodedCharSetId = kUtf8;
        md.Persistence = MQPER_PERSISTENCE_AS_Q_DEF;

        MQPMO pmo = {MQPMO_DEFAULT};
        pmo.Version = MQPMO_VERSION_3;
        pmo.Options = MQPMO_NO_SYNCPOINT | MQPMO_FAIL_IF_QUIESCING | MQPMO_NEW_MSG_ID;
        pmo.OriginalMsgHandle = message.handle();

        MQLONG cc = 0;
        MQLONG rc = 0;
        MQPUT(connection_.handle(), hobj, &md, &pmo, static_cast<MQLONG>(body.size()), const_cast<char*>(body.data()),
              &cc, &rc);
        if (cc == MQCC_FAILED) {
            MQLONG close_cc = 0;
            MQLONG close_rc = 0;
            MQCLOSE(connection_.handle(), &hobj, MQCO_NONE, &close_cc, &close_rc);
            queues_.erase(queue);
        }
        check("MQPUT", cc, rc);
    }

private:
    MQHOBJ open_queue(const std::string& queue) {
        if (auto it = queues_.find(queue); it != queues_.end()) return it->second;

        MQOD od = {MQOD_DEFAULT};
        copy_field(od.ObjectName, MQ_Q_NAME_LENGTH, queue, "adapters.output.ibmmq.queue");
        MQHOBJ hobj = MQHO_UNUSABLE_HOBJ;
        MQLONG cc = 0;
        MQLONG rc = 0;
        MQOPEN(connection_.handle(), &od, MQOO_OUTPUT | MQOO_FAIL_IF_QUIESCING, &hobj, &cc, &rc);
        check("MQOPEN", cc, rc);
        queues_[queue] = hobj;
        return hobj;
    }

    Connection connection_;  // declared first: destroyed last
    std::unordered_map<std::string, MQHOBJ> queues_;
};

class MqiFactory final : public MqSessionFactory {
public:
    bool available() const override { return true; }

    std::unique_ptr<MqInputSession> open_input(const MqConnectionSettings& settings,
                                               const MqInputOptions& options) override {
        return std::make_unique<MqiInputSession>(settings, options);
    }

    std::unique_ptr<MqOutputSession> open_output(const MqConnectionSettings& settings) override {
        return std::make_unique<MqiOutputSession>(settings);
    }
};

}  // namespace

std::unique_ptr<MqSessionFactory> make_default_factory() { return std::make_unique<MqiFactory>(); }

}  // namespace spinneret::adapters::mq
