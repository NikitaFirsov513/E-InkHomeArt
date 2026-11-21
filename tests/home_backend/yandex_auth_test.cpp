#include "YandexAuth.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); std::exit(1); } } while (false)
static const std::string code = R"({"device_code":"private-device-code","user_code":"1234567","verification_url":"https://ya.ru/device","interval":5,"expires_in":300})";
static const std::string token = R"({"token_type":"bearer","access_token":"test-access","refresh_token":"refresh:+/=?","expires_in":3600,"scope":"iot:view iot:control"})";
int main() {
    std::string stored, path, body;
    int calls = 0, saves = 0; bool writable = true;
    OAuthReply reply{true, 200, code};
    auto transport = [&](const char *p, const std::string &b) { path = p; body = b; ++calls; return reply; };
    auto save = [&](const std::string &s) { ++saves; if (!writable) return false; stored = s; return true; };
    YandexAuth auth("test-client", "test-secret", "test-device", transport, save);
    CHECK(auth.restore("")); CHECK(!auth.authorized());
    auth.begin(100); CHECK(path == "/device/code" && body.find("scope=iot%3Aview%20iot%3Acontrol") != std::string::npos);
    CHECK(body.find("test-secret") == std::string::npos);
    auto snapshot = auth.snapshot(101); CHECK(snapshot.state == AuthState::Waiting && snapshot.seconds_left == 299);
    CHECK(std::string(snapshot.user_code) == "1234567" && std::string(snapshot.verification_url) == "https://ya.ru/device");
    auth.tick(104, true); CHECK(calls == 1);
    reply = {true, 400, R"({"error":"authorization_pending"})"};
    auth.tick(105, true); CHECK(calls == 2 && path == "/token");
    CHECK(body.find("grant_type=device_code&code=private-device-code") != std::string::npos);
    CHECK(body.find("client_secret=test-secret") != std::string::npos && !auth.authorized());
    reply = {true, 400, R"({"error":"slow_down"})"};
    auth.tick(110, true); CHECK(calls == 3);
    auth.tick(115, true); CHECK(calls == 3);
    reply = {true, 200, token}; auth.tick(120, true);
    CHECK(auth.authorized() && auth.accessToken() == "test-access" && saves == 1);
    CHECK(std::string(auth.snapshot(120).user_code).empty());
    YandexAuth reboot("test-client", "test-secret", "test-device", transport, save);
    CHECK(reboot.restore(stored) && reboot.authorized());
    CHECK(reboot.ensureToken(0)); CHECK(body.find("grant_type=refresh_token") != std::string::npos);
    CHECK(body.find("refresh%3A%2B%2F%3D%3F") != std::string::npos);
    const int renewed_calls = calls; CHECK(reboot.ensureToken(100) && calls == renewed_calls);
    reply = {false, 0, ""}; CHECK(!reboot.ensureToken(110, true));
    CHECK(!reboot.ensureToken(111) && calls == renewed_calls + 1); // Rejected token cannot be reused during backoff.
    reply = {true, 400, R"({"error":"invalid_grant"})"};
    CHECK(!reboot.ensureToken(141) && !reboot.authorized() && stored.empty());
    reply = {true, 200, code}; auth.logout(); auth.begin(0);
    const int offline_calls = calls; auth.tick(100, false); CHECK(calls == offline_calls);
    auth.tick(300, false); CHECK(auth.snapshot(300).state == AuthState::Error);
    CHECK(std::string(auth.snapshot(300).user_code).empty());
    auth.begin(400); auth.cancel(); const int cancelled_calls = calls; auth.tick(410, true); CHECK(calls == cancelled_calls);
    auth.begin(500); reply = {true, 400, R"({"error":"access_denied"})"}; auth.tick(505, true);
    CHECK(!auth.authorized() && auth.snapshot(505).state == AuthState::Error);
    reply = {true, 200, code}; auth.begin(600);
    reply = {true, 200, R"({"token_type":"bearer","access_token":"access","refresh_token":"refresh","expires_in":3600,"scope":"iot:view"})"};
    auth.tick(605, true); CHECK(!auth.authorized());
    reply = {true, 200, code}; auth.begin(700); reply = {true, 200, token}; writable = false; auth.tick(705, true);
    CHECK(!auth.authorized() && auth.snapshot(705).state == AuthState::Error);
    writable = true; reply = {true, 200, code}; auth.begin(800); reply = {true, 200, token}; auth.tick(805, true);
    CHECK(auth.authorized()); writable = false; CHECK(!auth.logout() && auth.authorized());
    writable = true; CHECK(auth.logout() && !auth.authorized() && stored.empty());
    CHECK(path == "/revoke_token" && body.find("access_token=test-access") != std::string::npos);
    reply = {true, 200, code}; auth.begin(810); reply = {true, 200, token}; auth.tick(815, true);
    CHECK(auth.authorized() && !stored.empty());
    reply = {true, 200, code}; writable = false; auth.restart(820);
    CHECK(auth.authorized() && !stored.empty()); // Never pretend storage was cleared when it failed.
    writable = true; auth.restart(825);
    CHECK(!auth.authorized() && stored.empty() && auth.snapshot(825).state == AuthState::Waiting);
    CHECK(!auth.ensureToken(826));
    YandexAuth after_reset("test-client", "test-secret", "test-device", transport, save);
    CHECK(after_reset.restore(stored) && !after_reset.authorized());
    auth.cancel();
    CHECK(!auth.restore(R"({"version":1,"client_id":"another","access_token":"access","refresh_token":"refresh"})"));
    reply = {true, 200, R"({"device_code":"code","user_code":"1234567","verification_url":"https://example.invalid/device","interval":5,"expires_in":300})"};
    auth.begin(900); CHECK(auth.snapshot(900).state == AuthState::Error);
    reply = {false, 0, "", "ESP_ERR_HTTP_CONNECT"}; auth.begin(910);
    CHECK(std::string(auth.snapshot(910).message).find("ESP_ERR_HTTP_CONNECT") != std::string::npos);
    CHECK(std::string(auth.snapshot(910).user_code).empty());
    CHECK(YandexAuth::encode("a b&c=:+/\r\n") == "a%20b%26c%3D%3A%2B%2F%0D%0A");
    puts("Yandex OAuth device flow tests passed");
}
