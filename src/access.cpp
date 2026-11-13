#include "access.hpp"

#include <cctype>
#include <memory>
#include <mutex>

#include "util/jwt.hpp"
#include "util/jwt_claims.hpp"

namespace docsaccess {

namespace {

std::string bearer_token(const std::string& header) {
    // Схема — без учёта регистра (RFC 7235).
    if (header.size() < 7) return {};
    std::string scheme = header.substr(0, 6);
    for (auto& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (scheme != "bearer" || header[6] != ' ') return {};
    std::size_t start = header.find_first_not_of(' ', 7);
    if (start == std::string::npos) return {};
    std::size_t end = header.find_last_not_of(' ');
    return header.substr(start, end - start + 1);
}

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::string cookie_value(const std::string& header, const std::string& name) {
    std::size_t pos = 0;
    while (pos <= header.size()) {
        std::size_t end = header.find(';', pos);
        std::string part = trim(header.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        std::size_t eq = part.find('=');
        if (eq != std::string::npos && trim(part.substr(0, eq)) == name) return trim(part.substr(eq + 1));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return {};
}

Identity rejected(std::string reason) {
    Identity id;
    id.reason = std::move(reason);
    return id;
}

std::string string_claim(const nlohmann::json& payload, const char* name) {
    auto it = payload.find(name);
    return (it != payload.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

} // namespace

KeyLookup lazy_jwks(const std::string& auth_service_url) {
    struct State {
        std::string url;
        std::once_flag once;
        std::unique_ptr<jwks::Cache> cache;
    };
    auto state = std::make_shared<State>();
    state->url = auth_service_url;
    return [state](const std::string& kid) -> std::optional<jwks::PublicKey> {
        std::call_once(state->once, [&state] { state->cache = std::make_unique<jwks::Cache>(state->url); });
        return state->cache->find(kid);
    };
}

std::string request_token(const std::string& authorization_header, const std::string& cookie_header) {
    std::string t = bearer_token(authorization_header);
    if (!t.empty()) return t;
    return cookie_value(cookie_header, "session");
}

Identity verify(const std::string& token, const KeyLookup& keys) {
    if (token.empty()) return rejected("no_token");
    std::optional<std::string> kid = jwt::peek_kid(token);
    if (!kid) return rejected("malformed");
    std::optional<jwks::PublicKey> key = keys ? keys(*kid) : std::nullopt;
    if (!key) return rejected("unknown_key");

    jwt::VerifyError err = jwt::VerifyError::Malformed;
    std::optional<nlohmann::json> payload = jwt::verify_rs256(token, key->n, key->e, &err);
    if (!payload) {
        switch (err) {
        case jwt::VerifyError::Malformed:
            return rejected("malformed");
        case jwt::VerifyError::BadSignature:
            return rejected("signature");
        case jwt::VerifyError::Expired:
            return rejected("expired");
        }
        return rejected("malformed");
    }

    jwt_claims::Result claims = jwt_claims::check(*payload, kAudience, kScope);
    if (claims != jwt_claims::Result::Ok) return rejected(jwt_claims::to_string(claims));

    Identity id;
    id.sub = string_claim(*payload, "sub");
    if (id.sub.empty()) return rejected("no_sub");
    id.ok = true;
    id.role = string_claim(*payload, "role");
    id.platform = string_claim(*payload, "platform");
    return id;
}

store::Scope scope_for(const std::string& role, const std::string& platform) {
    store::Scope s;
    if (role == "admin") {
        s.all = true;
        s.kinds = {"spec", "change", "archive"};
        return s;
    }
    if (platform == "android") {
        s.repos = {"android-wallet"};
        s.kinds = {"spec"};
    } else if (platform == "web") {
        s.repos = {"wallet-web", "wallet-web-eqdefault"};
        s.kinds = {"spec"};
    }
    return s;
}

} // namespace docsaccess
