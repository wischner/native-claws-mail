//
// TCP sockets and TLS. TLS is the one thing Blaise does not write itself:
// see docs/notes/TLS.md. Without OpenSSL at build time everything else
// works and a connection that asks for TLS fails with a clear message,
// rather than quietly going out in plain text.
//
// Derived from Claws Mail src/common/socket.c and src/common/ssl.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include "net/transport.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef BLAISE_TLS_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace net {
namespace {

#ifdef _WIN32

using socket_handle = SOCKET;
constexpr socket_handle no_socket = INVALID_SOCKET;
constexpr int send_flags = 0;

// Start Winsock once.
void start_sockets() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started)
        throw net_error("Windows sockets could not start.");
}

void close_socket(socket_handle fd) { ::closesocket(fd); }

int poll_socket(pollfd &entry, int milliseconds) {
    return ::WSAPoll(&entry, 1, milliseconds);
}

// Return why the last socket call failed.
std::string socket_error_text() {
    return "error " + std::to_string(::WSAGetLastError());
}

// Report whether a failed connect is still going.
bool connect_in_progress() { return ::WSAGetLastError() == WSAEWOULDBLOCK; }

// Report whether a failed wait should simply be repeated.
bool interrupted() { return false; }

void set_blocking(socket_handle fd, bool blocking) {
    u_long mode = blocking ? 0 : 1;
    ::ioctlsocket(fd, FIONBIO, &mode);
}

#else

using socket_handle = int;
constexpr socket_handle no_socket = -1;
constexpr int send_flags = MSG_NOSIGNAL;

void start_sockets() {}

void close_socket(socket_handle fd) { ::close(fd); }

int poll_socket(pollfd &entry, int milliseconds) {
    return ::poll(&entry, 1, milliseconds);
}

// Return why the last socket call failed.
std::string socket_error_text() { return std::strerror(errno); }

// Report whether a failed connect is still going.
bool connect_in_progress() { return errno == EINPROGRESS; }

// Report whether a failed wait should simply be repeated.
bool interrupted() { return errno == EINTR; }

void set_blocking(socket_handle fd, bool blocking) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL,
            blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
}

#endif

// Closes a socket.
class scoped_fd {
public:
    explicit scoped_fd(socket_handle fd = no_socket) : fd_(fd) {}
    ~scoped_fd() {
        if (fd_ != no_socket)
            close_socket(fd_);
    }
    scoped_fd(const scoped_fd &) = delete;
    scoped_fd &operator=(const scoped_fd &) = delete;

    socket_handle get() const { return fd_; }

    // Give the socket up.
    socket_handle release() {
        const socket_handle fd = fd_;
        fd_ = no_socket;
        return fd;
    }

private:
    socket_handle fd_;
};

// Wait for a socket, or throw when the timeout passes.
void wait_ready(socket_handle fd, short events, int timeout_seconds) {
    pollfd entry{fd, events, 0};
    while (true) {
        const int result = poll_socket(entry, timeout_seconds * 1000);
        if (result > 0)
            return;
        if (result == 0)
            throw net_error("The server did not respond in time.");
        if (!interrupted())
            throw net_error("Network error: " + socket_error_text());
    }
}

// Connect one resolved address, honoring the timeout.
socket_handle connect_address(const addrinfo &address, int timeout_seconds) {
    scoped_fd fd(::socket(address.ai_family, address.ai_socktype,
                          address.ai_protocol));
    if (fd.get() == no_socket)
        return no_socket;
    set_blocking(fd.get(), false);
    if (::connect(fd.get(), address.ai_addr,
                  static_cast<socklen_t>(address.ai_addrlen)) != 0) {
        if (!connect_in_progress())
            return no_socket;
        pollfd entry{fd.get(), POLLOUT, 0};
        if (poll_socket(entry, timeout_seconds * 1000) <= 0)
            return no_socket;
        int failure = 0;
        socklen_t length = sizeof failure;
        ::getsockopt(fd.get(), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char *>(&failure), &length);
        if (failure != 0)
            return no_socket;
    }
    set_blocking(fd.get(), true);
    return fd.release();
}

// Resolve and connect to a host.
socket_handle open_socket(const endpoint &target) {
    start_sockets();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    const std::string port = std::to_string(target.port);
    if (const int error = ::getaddrinfo(target.host.c_str(), port.c_str(),
                                        &hints, &found);
        error != 0)
        throw net_error("Cannot resolve " + target.host + ": " +
                        ::gai_strerror(error));
    socket_handle fd = no_socket;
    for (const addrinfo *item = found;
         item != nullptr && fd == no_socket; item = item->ai_next)
        fd = connect_address(*item, target.timeout_seconds);
    ::freeaddrinfo(found);
    if (fd == no_socket)
        throw net_error("Cannot connect to " + target.host + ":" + port +
                        ".");
    return fd;
}

// A TCP connection, with TLS once asked for.
class socket_transport final : public transport {
public:
    explicit socket_transport(const endpoint &target)
        : target_(target), fd_(open_socket(target)) {
        const int enabled = 1;
        ::setsockopt(fd_.get(), IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&enabled),
                     sizeof enabled);
    }

    ~socket_transport() override { shut_down_tls(); }

    std::size_t read(char *buffer, std::size_t size) override {
#ifdef BLAISE_TLS_OPENSSL
        if (ssl_ != nullptr)
            return tls_read(buffer, size);
#endif
        wait_ready(fd_.get(), POLLIN, target_.timeout_seconds);
        const auto got = ::recv(fd_.get(), buffer,
                                static_cast<int>(std::min<std::size_t>(
                                    size, 1 << 20)),
                                0);
        if (got < 0)
            throw net_error("Network error: " + socket_error_text());
        return static_cast<std::size_t>(got);
    }

    void write(std::string_view data) override {
#ifdef BLAISE_TLS_OPENSSL
        if (ssl_ != nullptr) {
            tls_write(data);
            return;
        }
#endif
        while (!data.empty()) {
            wait_ready(fd_.get(), POLLOUT, target_.timeout_seconds);
            const auto sent = ::send(fd_.get(), data.data(),
                                     static_cast<int>(std::min<std::size_t>(
                                         data.size(), 1 << 20)),
                                     send_flags);
            if (sent < 0)
                throw net_error("Network error: " + socket_error_text());
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
    }

    bool encrypted() const override {
#ifdef BLAISE_TLS_OPENSSL
        return ssl_ != nullptr;
#else
        return false;
#endif
    }

    void start_tls() override {
#ifdef BLAISE_TLS_OPENSSL
        tls_handshake();
#else
        throw net_error("This build of Blaise has no TLS support.");
#endif
    }

private:
    void shut_down_tls() {
#ifdef BLAISE_TLS_OPENSSL
        if (ssl_ != nullptr)
            SSL_free(ssl_);
        if (context_ != nullptr)
            SSL_CTX_free(context_);
#endif
    }

#ifdef BLAISE_TLS_OPENSSL
    // Return the queued OpenSSL error as text.
    static std::string last_error() {
        char text[256] = "unknown error";
        if (const unsigned long code = ERR_get_error(); code != 0)
            ERR_error_string_n(code, text, sizeof text);
        return text;
    }

    void tls_handshake() {
        context_ = SSL_CTX_new(TLS_client_method());
        if (context_ == nullptr)
            throw net_error("TLS could not start: " + last_error());
        SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
        SSL_CTX_set_default_verify_paths(context_);
        if (!target_.accept_invalid_certificate)
            SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
        ssl_ = SSL_new(context_);
        if (ssl_ == nullptr)
            throw net_error("TLS could not start: " + last_error());
        SSL_set_fd(ssl_, static_cast<int>(fd_.get()));
        SSL_set_tlsext_host_name(ssl_, target_.host.c_str());
        if (!target_.accept_invalid_certificate)
            SSL_set1_host(ssl_, target_.host.c_str());
        if (SSL_connect(ssl_) != 1) {
            const long verdict = SSL_get_verify_result(ssl_);
            std::string reason =
                verdict != X509_V_OK
                    ? X509_verify_cert_error_string(verdict)
                    : last_error();
            throw net_error("TLS with " + target_.host + " failed: " +
                            reason);
        }
    }

    std::size_t tls_read(char *buffer, std::size_t size) {
        if (SSL_pending(ssl_) == 0)
            wait_ready(fd_.get(), POLLIN, target_.timeout_seconds);
        std::size_t got = 0;
        if (SSL_read_ex(ssl_, buffer, size, &got) == 1)
            return got;
        const int error = SSL_get_error(ssl_, 0);
        if (error == SSL_ERROR_ZERO_RETURN)
            return 0;
        throw net_error("TLS error: " + last_error());
    }

    void tls_write(std::string_view data) {
        while (!data.empty()) {
            std::size_t sent = 0;
            if (SSL_write_ex(ssl_, data.data(), data.size(), &sent) != 1)
                throw net_error("TLS error: " + last_error());
            data.remove_prefix(sent);
        }
    }

    SSL_CTX *context_ = nullptr;
    SSL *ssl_ = nullptr;
#endif

    endpoint target_;
    scoped_fd fd_;
};

} // namespace

bool tls_available() {
#ifdef BLAISE_TLS_OPENSSL
    return true;
#else
    return false;
#endif
}

connector socket_connector() {
    return [](const endpoint &target) -> std::unique_ptr<transport> {
        auto connection = std::make_unique<socket_transport>(target);
        if (target.tls == tls_mode::wrapped)
            connection->start_tls();
        return connection;
    };
}

} // namespace net
