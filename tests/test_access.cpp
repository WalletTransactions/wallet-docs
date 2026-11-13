#include "access.hpp"
#include "test_framework.hpp"
#include "test_keys.hpp"

TEST(scope_table) {
    auto admin = docsaccess::scope_for("admin", "web");
    CHECK(admin.all);
    CHECK_EQ(admin.kinds.size(), 3u);
    auto admin_no_platform = docsaccess::scope_for("admin", "");
    CHECK(admin_no_platform.all);

    auto android = docsaccess::scope_for("user", "android");
    CHECK(!android.all);
    REQUIRE(android.repos.size() == 1);
    CHECK_EQ(android.repos[0], std::string("android-wallet"));
    REQUIRE(android.kinds.size() == 1);
    CHECK_EQ(android.kinds[0], std::string("spec"));

    auto web = docsaccess::scope_for("user", "web");
    REQUIRE(web.repos.size() == 2);
    CHECK_EQ(web.repos[0], std::string("wallet-web"));
    CHECK_EQ(web.repos[1], std::string("wallet-web-eqdefault"));
    CHECK_EQ(web.kinds, std::vector<std::string>{"spec"});

    CHECK(docsaccess::scope_for("user", "other").empty());
    CHECK(docsaccess::scope_for("user", "telegram").empty());
    CHECK(docsaccess::scope_for("user", "mcp").empty());
    CHECK(docsaccess::scope_for("user", "").empty());
    CHECK(docsaccess::scope_for("", "").empty());
}

TEST(request_token_bearer_then_cookie) {
    CHECK_EQ(docsaccess::request_token("Bearer abc", "session=zzz"), std::string("abc"));
    CHECK_EQ(docsaccess::request_token("", "theme=dark; session=zzz; x=1"), std::string("zzz"));
    CHECK_EQ(docsaccess::request_token("Basic xyz", "sessions=no"), std::string(""));
}

TEST(verify_ok_takes_role_and_platform) {
    auto id = docsaccess::verify(testkeys::token("user", "android"), testkeys::jwks_lookup());
    REQUIRE(id.ok);
    CHECK_EQ(id.sub, std::string(testkeys::kSub));
    CHECK_EQ(id.role, std::string("user"));
    CHECK_EQ(id.platform, std::string("android"));
}

TEST(verify_rejects_without_audience) {
    auto claims = testkeys::user_claims("user", "android");
    claims["aud"] = nlohmann::json::array({"wallet-api", "wallet-report"});
    auto id = docsaccess::verify(testkeys::google_auth().sign(claims), testkeys::jwks_lookup());
    CHECK(!id.ok);
    CHECK_EQ(id.reason, std::string("wrong_audience"));
}

TEST(verify_rejects_without_scope) {
    auto claims = testkeys::user_claims("user", "android");
    claims["scope"] = "reports:read";
    auto id = docsaccess::verify(testkeys::google_auth().sign(claims), testkeys::jwks_lookup());
    CHECK(!id.ok);
    CHECK_EQ(id.reason, std::string("insufficient_scope"));
}

TEST(verify_rejects_legacy_foreign_expired) {
    auto legacy = testkeys::user_claims("user", "android");
    legacy.erase("aud");
    CHECK(!docsaccess::verify(testkeys::google_auth().sign(legacy), testkeys::jwks_lookup()).ok);
    CHECK(!docsaccess::verify(testkeys::foreign().sign(testkeys::user_claims("admin", "web")), testkeys::jwks_lookup()).ok);
    CHECK(!docsaccess::verify(testkeys::google_auth().sign(testkeys::user_claims("admin", "web", -10)),
                          testkeys::jwks_lookup())
               .ok);
    CHECK(!docsaccess::verify("garbage", testkeys::jwks_lookup()).ok);
    CHECK(!docsaccess::verify("", testkeys::jwks_lookup()).ok);
}
