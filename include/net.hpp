// ===========================================================================
//  net.hpp -- minimal HTTP POST, no libraries.
//
//  Only one network operation exists in this project: POST a JSON body to a
//  chat-completions endpoint and read a JSON reply. That does not justify a
//  dependency, so it is implemented here.
//
//  TWO TRANSPORTS, and the reason is honest rather than architectural:
//
//    http://   POSIX sockets, HTTP/1.1, written out below. This is the path that
//              matters for a GPU-less box, because that is where a local model
//              server (llama.cpp, Ollama, vLLM) listens -- on plain HTTP over
//              loopback, where TLS would add nothing.
//
//    https://  delegated to the curl binary. Implementing TLS by hand would mean
//              writing a X.509 verifier and a cipher suite, and getting either
//              subtly wrong is a security bug, not a rendering bug. Linking
//              OpenSSL would break the no-dependency rule. Shelling out to curl
//              keeps the build clean and delegates certificate validation to code
//              that is maintained for that purpose.
//
//  So: no dependency is required to talk to a local model, and remote HTTPS needs
//  only a binary that is already on essentially every system.
// ===========================================================================
#ifndef PPM_NET_HPP
#define PPM_NET_HPP

#include "pipe.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace ppm {
namespace net {

struct Url {
    std::string scheme = "http";
    std::string host;
    std::string port;
    std::string path = "/";
    bool tls = false;
};

inline bool parse_url(const std::string &in, Url &out, std::string &error) {
    std::string s = in;
    const size_t sep = s.find("://");
    if (sep == std::string::npos) {
        error = "URL must include a scheme, e.g. http://host/path";
        return false;
    }
    out.scheme = s.substr(0, sep);
    for (char &c : out.scheme) c = (char)tolower((unsigned char)c);
    if (out.scheme != "http" && out.scheme != "https") {
        error = "unsupported scheme '" + out.scheme + "'";
        return false;
    }
    out.tls = (out.scheme == "https");
    s = s.substr(sep + 3);

    const size_t slash = s.find('/');
    std::string hostport = slash == std::string::npos ? s : s.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : s.substr(slash);

    // IPv6 literals are bracketed, so a colon inside brackets is not a port.
    if (!hostport.empty() && hostport[0] == '[') {
        const size_t close = hostport.find(']');
        if (close == std::string::npos) { error = "malformed IPv6 host"; return false; }
        out.host = hostport.substr(1, close - 1);
        const size_t colon = hostport.find(':', close);
        out.port = colon == std::string::npos ? "" : hostport.substr(colon + 1);
    } else {
        const size_t colon = hostport.find(':');
        out.host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
        out.port = colon == std::string::npos ? "" : hostport.substr(colon + 1);
    }
    if (out.host.empty()) { error = "URL has no host"; return false; }
    if (out.port.empty()) out.port = out.tls ? "443" : "80";
    return true;
}

struct Response {
    int status = 0;
    std::string body;
};

// ---------------------------------------------------------------------------
// plain HTTP over sockets
// ---------------------------------------------------------------------------

namespace detail {

inline bool send_all(int fd, const char *data, size_t len, std::string &error) {
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, data + sent, len - sent, 0);
        if (n <= 0) { error = "send failed"; return false; }
        sent += (size_t)n;
    }
    return true;
}

/// Decode chunked transfer encoding.
///
/// Needed because a streaming-capable model server may use it even for a
/// non-streamed reply, and without this the body would arrive with hex length
/// prefixes interleaved and fail to parse as JSON.
inline bool dechunk(const std::string &in, std::string &out, std::string &error) {
    size_t i = 0;
    for (;;) {
        const size_t eol = in.find("\r\n", i);
        if (eol == std::string::npos) { error = "truncated chunk header"; return false; }
        const std::string hex = in.substr(i, eol - i);
        char *end = nullptr;
        const long len = strtol(hex.c_str(), &end, 16);
        if (end == hex.c_str()) { error = "bad chunk length"; return false; }
        i = eol + 2;
        if (len == 0) return true;              // terminating chunk
        if (i + (size_t)len > in.size()) { error = "truncated chunk body"; return false; }
        out.append(in, i, (size_t)len);
        i += (size_t)len + 2;                   // skip the trailing CRLF
    }
}

} // namespace detail

inline bool post_socket(const Url &url, const std::string &body,
                        const std::vector<std::string> &headers,
                        int timeout_sec, Response &out, std::string &error) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    const int rc = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        error = "cannot resolve " + url.host + ": " + gai_strerror(rc);
        return false;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        // Apply the timeout to the socket itself, so a server that accepts the
        // connection and then stalls cannot hang us forever.
        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        error = "cannot connect to " + url.host + ":" + url.port;
        return false;
    }

    std::string req = "POST " + url.path + " HTTP/1.1\r\n";
    req += "Host: " + url.host + ":" + url.port + "\r\n";
    for (const std::string &h : headers) req += h + "\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    // No keep-alive: one request per process, and "close" makes the end of the
    // body unambiguous even if the server omits Content-Length.
    req += "Connection: close\r\n\r\n";
    req += body;

    if (!detail::send_all(fd, req.data(), req.size(), error)) { ::close(fd); return false; }

    std::string raw;
    char buf[16384];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) { error = "receive failed or timed out"; ::close(fd); return false; }
        if (n == 0) break;
        raw.append(buf, (size_t)n);
    }
    ::close(fd);

    const size_t split = raw.find("\r\n\r\n");
    if (split == std::string::npos) { error = "malformed HTTP response"; return false; }
    const std::string head = raw.substr(0, split);
    const std::string rest = raw.substr(split + 4);

    if (sscanf(head.c_str(), "HTTP/%*s %d", &out.status) != 1) {
        error = "cannot parse HTTP status line";
        return false;
    }

    // Header names are case-insensitive.
    std::string lower_head = head;
    for (char &c : lower_head) c = (char)tolower((unsigned char)c);
    if (lower_head.find("transfer-encoding: chunked") != std::string::npos) {
        out.body.clear();
        if (!detail::dechunk(rest, out.body, error)) return false;
    } else {
        out.body = rest;
    }
    return true;
}

// ---------------------------------------------------------------------------
// HTTPS via curl
// ---------------------------------------------------------------------------

inline bool post_curl(const std::string &url_str, const std::string &body,
                      const std::vector<std::string> &headers,
                      int timeout_sec, Response &out, std::string &error) {
    if (find_tool("curl", "", "").empty()) {
        error = "https requires the curl binary, which was not found on PATH.\n"
                "  Either install curl, or point --base-url at a plain-http endpoint\n"
                "  such as a local model server (http://127.0.0.1:11434/v1).";
        return false;
    }

    // The body goes via a file, not the command line: an argv full of JSON would
    // hit ARG_MAX for a large prompt, and the API key must not appear in the
    // process list where any user on the box could read it.
    char tmpl[] = "/tmp/ppm-gen-XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0) { error = "cannot create a temporary file"; return false; }
    const std::string tmp_path = tmpl;
    {
        FILE *tf = fdopen(fd, "wb");
        if (!tf) { ::close(fd); unlink(tmp_path.c_str()); error = "cannot write temporary file"; return false; }
        fwrite(body.data(), 1, body.size(), tf);
        fclose(tf);
    }

    std::string hdr_file_args;
    char htmpl[] = "/tmp/ppm-gen-hdr-XXXXXX";
    const int hfd = mkstemp(htmpl);
    if (hfd < 0) { unlink(tmp_path.c_str()); error = "cannot create a temporary file"; return false; }
    const std::string hdr_path = htmpl;
    {
        FILE *hf = fdopen(hfd, "wb");
        if (!hf) { ::close(hfd); unlink(tmp_path.c_str()); unlink(hdr_path.c_str());
                   error = "cannot write temporary file"; return false; }
        for (const std::string &h : headers) fprintf(hf, "%s\n", h.c_str());
        fclose(hf);
    }
    // -H @file keeps the Authorization header out of argv too.
    hdr_file_args = " -H @" + shq(hdr_path);

    const std::string cmd =
        "curl -s -S --fail-with-body -X POST" + hdr_file_args +
        " --data-binary @" + shq(tmp_path) +
        " --max-time " + std::to_string(timeout_sec) +
        " -w '\\n%{http_code}' " + shq(url_str) + " 2>&1";

    FILE *f = popen(cmd.c_str(), "r");
    if (!f) {
        unlink(tmp_path.c_str());
        unlink(hdr_path.c_str());
        error = "cannot run curl";
        return false;
    }
    std::string all;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    pclose(f);
    unlink(tmp_path.c_str());
    unlink(hdr_path.c_str());

    // The status code was appended on its own final line by -w.
    const size_t nl = all.find_last_of('\n');
    if (nl == std::string::npos) {
        error = "curl produced no parseable output: " + all.substr(0, 300);
        return false;
    }
    out.status = atoi(all.substr(nl + 1).c_str());
    out.body = all.substr(0, nl);
    if (out.status == 0) {
        error = "curl failed: " + out.body.substr(0, 300);
        return false;
    }
    return true;
}

/// POST `body` to `url_str`, choosing the transport from the scheme.
inline bool post_json(const std::string &url_str, const std::string &body,
                      const std::string &bearer, int timeout_sec,
                      Response &out, std::string &error) {
    Url url;
    if (!parse_url(url_str, url, error)) return false;

    std::vector<std::string> headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("Accept: application/json");
    if (!bearer.empty()) headers.push_back("Authorization: Bearer " + bearer);

    if (url.tls) return post_curl(url_str, body, headers, timeout_sec, out, error);
    return post_socket(url, body, headers, timeout_sec, out, error);
}

} // namespace net
} // namespace ppm

#endif // PPM_NET_HPP
