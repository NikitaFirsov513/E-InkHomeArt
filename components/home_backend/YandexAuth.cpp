#include "YandexAuth.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const std::string &s) { return Json(cJSON_ParseWithLength(s.data(), s.size()), cJSON_Delete); }
std::string str(const cJSON *j, const char *key) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
uint64_t seconds(const cJSON *j, const char *key, uint64_t max) {
    const auto *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) && v->valuedouble >= 1 &&
           v->valuedouble <= static_cast<double>(max) && std::floor(v->valuedouble) == v->valuedouble ?
           static_cast<uint64_t>(v->valuedouble) : 0;
}
bool safe(const std::string &s, size_t max) {
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 33 && c <= 126; });
}
bool scopeOk(const std::string &scope) {
    const auto padded = " " + scope + " ";
    return scope.empty() || (padded.find(" iot:view ") != std::string::npos && padded.find(" iot:control ") != std::string::npos);
}
}
YandexAuth::YandexAuth(std::string id, std::string secret, std::string device_id, Transport transport, Save save)
    : id_(std::move(id)), secret_(std::move(secret)), device_id_(std::move(device_id)),
      transport_(std::move(transport)), save_(std::move(save)) {}
std::string YandexAuth::encode(const std::string &s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string YandexAuth::credentials() const { return "client_id=" + encode(id_) + "&client_secret=" + encode(secret_); }
void YandexAuth::fail(const char *text) { state_ = AuthState::Error; message_ = text; device_code_.clear(); user_code_.clear(); url_.clear(); }
bool YandexAuth::restore(const std::string &json) {
    if (json.empty()) return true;
    const auto j = parse(json);
    const auto access = str(j.get(), "access_token"), refresh = str(j.get(), "refresh_token");
    if (seconds(j.get(), "version", 1) != 1 || str(j.get(), "client_id") != id_ ||
        !safe(access, 1000) || (!refresh.empty() && !safe(refresh, 2048))) {
        fail("Сохранённый вход недоступен. Войдите заново."); return false;
    }
    access_ = access; refresh_ = refresh; restored_ = true;
    state_ = AuthState::Authorized; message_ = "Аккаунт подключён";
    return true;
}
void YandexAuth::begin(uint64_t now) {
    if (authorized()) return;
    cancel();
    if (!safe(id_, 128) || !safe(secret_, 256)) { fail("Заполните secrets/yandex.json и пересоберите прошивку"); return; }
    const auto reply = transport_("/device/code", "client_id=" + encode(id_) + "&scope=iot%3Aview%20iot%3Acontrol&device_id=" +
                                 encode(device_id_) + "&device_name=EInkHomeArt%20ESP32-S3");
    const auto j = parse(reply.body);
    if (!reply.transport_ok) {
        char text[256];
        snprintf(text, sizeof(text), "Ошибка HTTPS: %s, HTTP %d. Проверьте сеть и сертификаты.",
                 reply.transport_error.empty() ? "нет соединения" : reply.transport_error.c_str(), reply.status);
        fail(text); return;
    }
    if (reply.status >= 500 || reply.status == 429) {
        char text[160]; snprintf(text, sizeof(text), "Яндекс временно недоступен: HTTP %d. Повторите вход позже.", reply.status);
        fail(text); return;
    }
    if (reply.status != 200) { fail("Яндекс отклонил запрос входа. Проверьте настройки OAuth-приложения."); return; }
    const auto device_code = str(j.get(), "device_code"), code = str(j.get(), "user_code"), url = str(j.get(), "verification_url");
    const auto lifetime = seconds(j.get(), "expires_in", 3600), interval = seconds(j.get(), "interval", 300);
    const bool trusted_url = url == "https://oauth.yandex.ru/device" || url == "https://ya.ru/device";
    if (!safe(device_code, 256) || !safe(code, 39) || !trusted_url || !lifetime || !interval) {
        fail("Некорректный ответ авторизации Яндекса"); return;
    }
    device_code_ = device_code; user_code_ = code; url_ = url;
    code_expires_ = now + lifetime; interval_ = static_cast<uint32_t>(interval); next_poll_ = now + interval_;
    state_ = AuthState::Waiting; message_ = "Откройте oauth.yandex.ru/device и введите код";
}
bool YandexAuth::accept(const OAuthReply &reply, uint64_t now) {
    const auto j = parse(reply.body);
    const auto access = str(j.get(), "access_token"), refresh = str(j.get(), "refresh_token");
    const auto lifetime = seconds(j.get(), "expires_in", 1000000000000ULL);
    if (reply.status != 200 || !safe(access, 1000) || !safe(refresh, 2048) || str(j.get(), "token_type") != "bearer" || !lifetime ||
        !scopeOk(str(j.get(), "scope"))) { fail("Яндекс не выдал токен с правами iot:view и iot:control"); return false; }
    Json stored(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddNumberToObject(stored.get(), "version", 1);
    cJSON_AddStringToObject(stored.get(), "client_id", id_.c_str());
    cJSON_AddStringToObject(stored.get(), "access_token", access.c_str());
    cJSON_AddStringToObject(stored.get(), "refresh_token", refresh.c_str());
    char *raw = cJSON_PrintUnformatted(stored.get());
    const bool saved = raw && save_(raw); cJSON_free(raw);
    if (!saved) { access_.clear(); refresh_.clear(); fail("Не удалось сохранить вход. Проверьте хранилище панели."); return false; }
    access_ = access; refresh_ = refresh; expires_at_ = now + lifetime - std::min<uint64_t>(60, lifetime / 10);
    restored_ = false; retry_refresh_ = 0; cancel(); state_ = AuthState::Authorized; message_ = "Аккаунт подключён";
    return true;
}
void YandexAuth::restart(uint64_t now) {
    if (logout()) begin(now);
}
void YandexAuth::tick(uint64_t now, bool online) {
    if (state_ != AuthState::Waiting) return;
    if (now >= code_expires_) { fail("Код устарел. Запросите новый код входа."); return; }
    if (!online) { message_ = "Нет сети. Проверьте подключение, пока код не истёк."; return; }
    if (now < next_poll_) return;
    next_poll_ = now + interval_;
    const auto reply = transport_("/token", credentials() + "&grant_type=device_code&code=" + encode(device_code_));
    if (!reply.transport_ok || reply.status >= 500) { message_ = "Временная ошибка сети. Ожидаем подтверждение..."; return; }
    if (reply.status == 200) { accept(reply, now); return; }
    const auto j = parse(reply.body); const auto error = str(j.get(), "error");
    if (error == "authorization_pending") { message_ = "Ожидаем подтверждение на телефоне..."; return; }
    if (error == "slow_down" || reply.status == 429) { interval_ = std::min<uint32_t>(300, interval_ + 5); next_poll_ = now + interval_; return; }
    if (error == "access_denied") fail("Доступ отклонён. Можно начать вход заново.");
    else if (error == "invalid_grant" || error == "expired_token" || error == "bad_verification_code") fail("Код устарел. Начните вход заново.");
    else fail("Ошибка OAuth. Проверьте Client ID, секрет и права приложения.");
}
void YandexAuth::cancel() {
    device_code_.clear(); user_code_.clear(); url_.clear(); code_expires_ = next_poll_ = 0;
    state_ = authorized() ? AuthState::Authorized : AuthState::SignedOut;
    message_ = authorized() ? "Аккаунт подключён" : "Войдите в Яндекс для управления устройствами";
}
bool YandexAuth::logout() {
    if (!save_("")) { message_ = "Не удалось удалить токены. Повторите выход."; return false; }
    const auto previous_access = access_;
    access_.clear(); refresh_.clear(); restored_ = false; cancel();
    // Local logout succeeds offline too; revoke this device's token when reachable.
    if (!previous_access.empty()) transport_("/revoke_token", credentials() + "&access_token=" + encode(previous_access));
    return true;
}
bool YandexAuth::ensureToken(uint64_t now, bool force) {
    if (!authorized()) return false;
    if (force) restored_ = true; // A rejected token stays unusable until renewal succeeds.
    if (!force && !restored_ && now < expires_at_) return true;
    if (now < retry_refresh_) return false;
    retry_refresh_ = now + 30;
    if (refresh_.empty()) { access_.clear(); save_(""); fail("Срок входа истёк. Войдите по коду заново."); return false; }
    const auto reply = transport_("/token", credentials() + "&grant_type=refresh_token&refresh_token=" + encode(refresh_));
    if (!reply.transport_ok || reply.status >= 500 || reply.status == 429) { message_ = "Не удалось обновить вход. Повторим после восстановления сети."; return false; }
    if (reply.status == 200) return accept(reply, now);
    const auto j = parse(reply.body);
    if (str(j.get(), "error") == "invalid_grant") { access_.clear(); refresh_.clear(); save_(""); fail("Вход отозван или истёк. Войдите по коду заново."); }
    else message_ = "Не удалось обновить вход. Проверьте настройки OAuth-приложения.";
    return false;
}
AuthSnapshot YandexAuth::snapshot(uint64_t now) const {
    AuthSnapshot out; out.state = state_; out.authorized = authorized();
    out.seconds_left = state_ == AuthState::Waiting && code_expires_ > now ? static_cast<uint32_t>(code_expires_ - now) : 0;
    snprintf(out.user_code, sizeof(out.user_code), "%s", user_code_.c_str());
    snprintf(out.verification_url, sizeof(out.verification_url), "%s", url_.c_str());
    snprintf(out.message, sizeof(out.message), "%s", message_.c_str());
    return out;
}
