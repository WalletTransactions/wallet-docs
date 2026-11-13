// RSA-ключи для тестов JWT (как в wallet-mcp/tests/test_keys.hpp):
// генерируются при первом обращении, PEM — во временный файл для
// crypto::RsaSigningKey. Подписанные ими JWT проверяются тем же кодом, что и
// в сервисе (jwt::verify_rs256 по n/e).
#pragma once

#include <cstdio>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "access.hpp"
#include "nlohmann/json.hpp"
#include "util/crypto.hpp"
#include "util/jwt.hpp"

namespace testkeys {

class Key {
public:
    Key() {
        EVP_PKEY* pkey = EVP_RSA_gen(2048);
        if (pkey == nullptr) throw std::runtime_error("EVP_RSA_gen failed");
        char path[] = "/tmp/wallet-docs-test-key-XXXXXX";
        int fd = mkstemp(path);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        FILE* f = fdopen(fd, "w");
        PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        fclose(f);
        EVP_PKEY_free(pkey);
        key_ = std::make_unique<crypto::RsaSigningKey>(path);
        std::remove(path);
    }

    std::string sign(const nlohmann::json& payload) const { return jwt::sign_rs256(payload, *key_); }
    const std::string& kid() const { return key_->kid(); }
    jwks::PublicKey public_key() const { return {key_->modulus(), key_->exponent()}; }

private:
    std::unique_ptr<crypto::RsaSigningKey> key_;
};

inline const Key& google_auth() {
    static Key k;
    return k;
}
inline const Key& foreign() {
    static Key k;
    return k;
}

inline docsaccess::KeyLookup jwks_lookup() {
    return [](const std::string& kid) -> std::optional<jwks::PublicKey> {
        if (kid != google_auth().kid()) return std::nullopt;
        return google_auth().public_key();
    };
}

inline constexpr const char* kSub = "22222222-2222-4222-8222-222222222222";

// Пользовательский токен google-auth после docs-audience: aud ∋ wallet-docs,
// scope ∋ docs:ask.
inline nlohmann::json user_claims(const std::string& role, const std::string& platform, long ttl = 900) {
    std::time_t now = std::time(nullptr);
    nlohmann::json j = {{"iss", "google-auth"},
                        {"sub", kSub},
                        {"role", role},
                        {"aud", nlohmann::json::array({"wallet-api", "wallet-report", "wallet-ai", "wallet-docs"})},
                        {"scope", "reports:read expense:write ai:use docs:ask"},
                        {"iat", now},
                        {"exp", now + ttl}};
    if (!platform.empty()) {
        j["platform"] = platform;
        j["device_id"] = "d-1";
    }
    return j;
}

inline std::string token(const std::string& role, const std::string& platform) {
    return google_auth().sign(user_claims(role, platform));
}

} // namespace testkeys
