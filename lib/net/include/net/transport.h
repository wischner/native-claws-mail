//
// A byte stream to a server, plus the line discipline every mail
// protocol shares. Claws' Session and SockInfo mix this with GLib main
// loop callbacks; here it is blocking and runs on a worker thread, which
// makes a protocol read like the RFC describes it.
//
// Protocol code sees only the transport interface, so the tests drive
// clients against a scripted server with no sockets at all.
//
// Derived from Claws Mail src/common/session.c and src/common/socket.c,
// Copyright (C) 1999-2026 the Claws Mail team and Hiroyuki Yamamoto.
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace net {

// A network, TLS or protocol failure; the text suits a status bar.
class net_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The server refused our credentials, as distinct from a broken network.
class auth_error : public net_error {
public:
    using net_error::net_error;
};

// How a connection is secured.
enum class tls_mode {
    none,       // Plain text.
    wrapped,    // TLS from the first byte.
    starttls    // Upgrade in the protocol; failure to upgrade is an error.
};

// Where to connect and how.
struct endpoint {
    std::string host;
    int port = 0;
    tls_mode tls = tls_mode::none;
    bool accept_invalid_certificate = false;
    int timeout_seconds = 60;
};

// An open connection.
class transport {
public:
    virtual ~transport() = default;

    // Read up to size bytes; returns 0 at end of stream, throws on error.
    virtual std::size_t read(char *buffer, std::size_t size) = 0;

    // Write all bytes, or throw.
    virtual void write(std::string_view data) = 0;

    // Upgrade to TLS in place, or throw.
    virtual void start_tls() = 0;

    // Report whether the stream is encrypted.
    virtual bool encrypted() const = 0;
};

// Opens a transport; replaced in tests.
using connector = std::function<std::unique_ptr<transport>(const endpoint &)>;

// Return a connector that opens real sockets.
connector socket_connector();

// Report whether this build can speak TLS at all.
bool tls_available();

// A transport with line buffering and protocol logging.
class line_stream {
public:
    // Wrap a transport.
    explicit line_stream(std::unique_ptr<transport> connection);

    //
    // Read one line, without its line end.
    //
    // Notes:
    //      Throws net_error at end of stream, or past 8 MiB, so a server
    //      that never sends a newline cannot exhaust memory.
    //
    std::string read_line();

    // Read exactly size bytes.
    std::string read_bytes(std::size_t size);

    // Write text as is, logging it.
    void write(std::string_view data);

    // Write a command and CRLF; the log shows it, unless secret.
    void write_line(std::string_view line, bool secret = false);

    //
    // Upgrade to TLS.
    //
    // Notes:
    //      Anything already buffered arrived before encryption and could
    //      have been injected in transit, so it is discarded.
    //
    void start_tls();

    // Report whether the stream is encrypted.
    bool encrypted() const { return connection_->encrypted(); }

private:
    bool fill();

    std::unique_ptr<transport> connection_;
    std::string buffer_;
    std::size_t position_ = 0;
};

} // namespace net

#endif
