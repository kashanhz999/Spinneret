#include <filesystem>

#include <gtest/gtest.h>

#include "spinneret/config/config_service.h"
#include "spinneret/config/template_service.h"
#include "spinneret/util/files.h"
#include "test_support.h"

using namespace spinneret;
using config::ConfigError;
using config::ConfigService;
using json = nlohmann::json;

namespace {

ConfigError::Kind error_kind(auto&& fn) {
    try {
        fn();
    } catch (const ConfigError& e) {
        return e.kind();
    }
    ADD_FAILURE() << "expected ConfigError";
    return ConfigError::Kind::Io;
}

}  // namespace

TEST(ConfigService, UsesDefaultsWhenNoFileExists) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();
    EXPECT_TRUE(config.get_bool("adapters.input.rest.enabled"));
    EXPECT_FALSE(config.get_bool("adapters.output.rest.enabled"));
    EXPECT_EQ(config.get_string("adapters.input.ibmmq.queue"), "AODB.FLIGHT.UPDATE");
    EXPECT_EQ(config.get_int("adapters.output.rest.timeoutMs"), 5000);
    EXPECT_TRUE(config.get("no.such.key").is_null());
}

TEST(ConfigService, UpdatePersistsAndReportsChangedKeys) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();

    auto changed = config.update({{"adapters.output.rest.enabled", true},
                                  {"adapters.input.rest.enabled", true},  // unchanged
                                  {"adapters.output.rest.url", "http://example:9/hook"}});
    EXPECT_EQ(changed, (std::set<std::string>{"adapters.output.rest.enabled", "adapters.output.rest.url"}));

    ConfigService reloaded(dir.path());
    reloaded.load();
    EXPECT_TRUE(reloaded.get_bool("adapters.output.rest.enabled"));
    EXPECT_EQ(reloaded.get_string("adapters.output.rest.url"), "http://example:9/hook");
}

TEST(ConfigService, RejectsUnknownKeysAndWrongTypes) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();

    EXPECT_EQ(error_kind([&] { config.update({{"adapters.input.rest.enable", true}}); }), ConfigError::Kind::Invalid);
    EXPECT_EQ(error_kind([&] { config.update({{"adapters.input.rest.enabled", "yes"}}); }), ConfigError::Kind::Invalid);
    EXPECT_EQ(error_kind([&] { config.update({{"adapters.output.rest.timeoutMs", -1}}); }), ConfigError::Kind::Invalid);
    EXPECT_EQ(error_kind([&] { config.update(json::array()); }), ConfigError::Kind::Invalid);
    EXPECT_TRUE(config.overrides().empty()) << "a rejected update must change nothing";
}

TEST(ConfigService, NullResetsToDefault) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();
    config.update({{"adapters.input.ibmmq.queue", "OTHER"}});
    auto changed = config.update({{"adapters.input.ibmmq.queue", nullptr}});
    EXPECT_EQ(changed, (std::set<std::string>{"adapters.input.ibmmq.queue"}));
    EXPECT_EQ(config.get_string("adapters.input.ibmmq.queue"), "AODB.FLIGHT.UPDATE");
}

TEST(ConfigService, RestoreSwapsWithBackup) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();

    EXPECT_EQ(error_kind([&] { config.restore_backup(); }), ConfigError::Kind::NotFound);

    config.update({{"adapters.input.ibmmq.queue", "FIRST"}});
    config.update({{"adapters.input.ibmmq.queue", "SECOND"}});

    config.restore_backup();
    EXPECT_EQ(config.get_string("adapters.input.ibmmq.queue"), "FIRST");
    config.restore_backup();  // undo the restore
    EXPECT_EQ(config.get_string("adapters.input.ibmmq.queue"), "SECOND");
}

TEST(ConfigService, CorruptFileFailsLoudly) {
    test::TempDir dir;
    util::atomic_write(dir.path() / "runtime-config.json", "{not json");
    ConfigService config(dir.path());
    EXPECT_THROW(config.load(), ConfigError);
}

TEST(ConfigService, MasksSecretsByName) {
    EXPECT_TRUE(ConfigService::is_secret("ibm.mq.password"));
    EXPECT_FALSE(ConfigService::is_secret("ibm.mq.user"));
}

TEST(TemplateService, AppliesBundledTemplates) {
    test::TempDir dir;
    std::filesystem::copy(SPINNERET_TEMPLATES_DIR, dir.path() / "templates");
    ConfigService config(dir.path());
    config.load();
    config::TemplateService templates(config);

    EXPECT_EQ(templates.list(), (std::vector<std::string>{"development", "production", "rest-only"}));

    templates.apply("production");
    EXPECT_EQ(config.get_string("ibm.mq.queueManager"), "QM_PROD");
    EXPECT_EQ(config.get_string("adapters.input.ibmmq.queue"), "PROD.INPUT");

    templates.apply("rest-only");
    EXPECT_TRUE(config.get_bool("adapters.output.rest.enabled"));
    EXPECT_FALSE(config.get_bool("adapters.input.ibmmq.enabled"));
}

TEST(TemplateService, RejectsTraversalAndUnknownNames) {
    test::TempDir dir;
    ConfigService config(dir.path());
    config.load();
    config::TemplateService templates(config);

    EXPECT_EQ(error_kind([&] { templates.apply("../api-keys"); }), ConfigError::Kind::Invalid);
    EXPECT_EQ(error_kind([&] { templates.apply("nope"); }), ConfigError::Kind::NotFound);
}
