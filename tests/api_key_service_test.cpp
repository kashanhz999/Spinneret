#include <gtest/gtest.h>

#include "spinneret/security/api_key_service.h"
#include "spinneret/util/files.h"
#include "test_support.h"

using namespace spinneret;

TEST(ApiKeyService, BootstrapsOneKeyOnlyWhenEmpty) {
    test::TempDir dir;
    security::ApiKeyService keys(dir.path() / "api-keys.json");
    keys.load();

    auto first = keys.ensure_bootstrap_key();
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->starts_with("spn_"));
    EXPECT_TRUE(keys.is_valid(*first));
    EXPECT_FALSE(keys.ensure_bootstrap_key().has_value());
}

TEST(ApiKeyService, StoresOnlyHashes) {
    test::TempDir dir;
    auto file = dir.path() / "api-keys.json";
    security::ApiKeyService keys(file);
    keys.load();
    std::string key = keys.create("ci");

    auto contents = util::read_file(file);
    ASSERT_TRUE(contents.has_value());
    EXPECT_EQ(contents->find(key), std::string::npos) << "plain-text key must not be written to disk";

    security::ApiKeyService reloaded(file);
    reloaded.load();
    EXPECT_TRUE(reloaded.is_valid(key));
    EXPECT_FALSE(reloaded.is_valid(key + "x"));
    EXPECT_FALSE(reloaded.is_valid(""));
}

TEST(ApiKeyService, RevokeById) {
    test::TempDir dir;
    security::ApiKeyService keys(dir.path() / "api-keys.json");
    keys.load();
    std::string a = keys.create("a");
    std::string b = keys.create("b");

    std::string a_id;
    for (const auto& info : keys.list()) {
        if (info.name == "a") a_id = info.id;
    }
    ASSERT_EQ(a_id.size(), 12u);

    EXPECT_TRUE(keys.revoke(a_id));
    EXPECT_FALSE(keys.revoke(a_id));
    EXPECT_FALSE(keys.is_valid(a));
    EXPECT_TRUE(keys.is_valid(b));
}

TEST(ApiKeyService, CorruptFileFailsLoudly) {
    test::TempDir dir;
    util::atomic_write(dir.path() / "api-keys.json", R"({"some-legacy-key": {"name": "x"}})");
    security::ApiKeyService keys(dir.path() / "api-keys.json");
    EXPECT_THROW(keys.load(), std::runtime_error);
}
