// Доступ к /api/docs/* (спека docs-answering, «Проверка токена», «Область
// документации по токену»). Токен — JWT RS256 google-auth из
// `Authorization: Bearer` или cookie `session`; сервис проверяет его сам по
// JWKS (подпись, exp, iss, aud ∋ wallet-docs, scope ∋ docs:ask) и берёт role
// и platform только из проверенного токена.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "store.hpp"
#include "util/jwks.hpp"

namespace docsaccess {

inline constexpr const char* kAudience = "wallet-docs";
inline constexpr const char* kScope = "docs:ask";

using KeyLookup = std::function<std::optional<jwks::PublicKey>(const std::string& kid)>;

// jwks::Cache, созданный при первой проверке, а не на старте: сервис
// стартует и при недоступном google-auth.
KeyLookup lazy_jwks(const std::string& auth_service_url);

// Токен из `Authorization: Bearer <токен>`; иначе — из cookie `session`.
std::string request_token(const std::string& authorization_header, const std::string& cookie_header);

struct Identity {
    bool ok = false;
    std::string reason; // при отказе — для журнала и метрики, без токена
    std::string sub;
    std::string role;
    std::string platform;
};

Identity verify(const std::string& token, const KeyLookup& keys);

// Таблица области — константа (D5): ошибка в конфиге не должна открыть чужие
// спеки. admin — всё; platform android — android-wallet/spec; web —
// wallet-web и wallet-web-eqdefault/spec; иначе — пусто.
store::Scope scope_for(const std::string& role, const std::string& platform);

} // namespace docsaccess
