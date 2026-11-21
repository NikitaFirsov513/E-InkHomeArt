#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

enum class AuthAction : uint8_t { Begin, Cancel, Logout, Restart };
enum class AuthState : uint8_t { SignedOut, Waiting, Authorized, Error };
struct AuthSnapshot {
    AuthState state = AuthState::SignedOut;
    bool authorized = false;
    uint32_t seconds_left = 0;
    char user_code[40]{};
    char verification_url[256]{};
    char message[256]{};
};
static_assert(std::is_trivially_copyable<AuthSnapshot>::value);
struct OAuthReply {
    bool transport_ok = false;
    int status = 0;
    std::string body;
    std::string transport_error; // Error identifier only, never response/credentials.
};

// Owned exclusively by the network worker. No tokens cross the UI queue.
class YandexAuth {
public:
    using Transport = std::function<OAuthReply(const char *, const std::string &)>;
    using Save = std::function<bool(const std::string &)>;
    YandexAuth(std::string client_id, std::string secret, std::string device_id,
               Transport transport, Save save);
    bool restore(const std::string &json);
    void begin(uint64_t now);
    void restart(uint64_t now);
    void tick(uint64_t now, bool online);
    void cancel();
    bool logout();
    bool ensureToken(uint64_t now, bool force = false);
    const std::string &accessToken() const { return access_; }
    bool authorized() const { return !access_.empty(); }
    AuthSnapshot snapshot(uint64_t now) const;
    static std::string encode(const std::string &value);
private:
    std::string id_, secret_, device_id_, access_, refresh_, device_code_, user_code_, url_, message_;
    Transport transport_;
    Save save_;
    AuthState state_ = AuthState::SignedOut;
    uint64_t expires_at_ = 0, code_expires_ = 0, next_poll_ = 0, retry_refresh_ = 0;
    uint32_t interval_ = 5;
    bool restored_ = false;
    std::string credentials() const;
    bool accept(const OAuthReply &reply, uint64_t now);
    void fail(const char *message);
};
