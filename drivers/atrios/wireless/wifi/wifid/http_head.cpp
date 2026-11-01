#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/net/resolve.h>
#include <util/system/byteorder.h>

#include "http_head.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>

namespace {
    struct Socket {
        int fd;
        Socket(int f)
            : fd(f)
        {
        }
        ~Socket() {
            if (fd >= 0) {
                shutdown(fd, 2);
                close(fd);
            }
        }
    };

    void waitForConnect(pollfd& pfds) {
        pfds.events = POLLOUT;
        if (poll(&pfds, 1, 2000) != 1) {
            throw std::runtime_error("Failed to connect, timeouted");
        }

        if (pfds.revents & POLLERR) {
            int err = 0;
            socklen_t errsz = sizeof(err);
            if (getsockopt(pfds.fd, SOL_SOCKET, SO_ERROR, &err, &errsz) == 0) {
                throw quasar::ErrnoException(err, "connect()");
            }
            throw std::runtime_error("Failed to connect");
        }
        if (pfds.revents & POLLOUT) {
            return;
        }
        if (pfds.revents & POLLHUP) {
            throw std::runtime_error("Failed to connect, closed");
        }
        throw std::runtime_error("Failed to connect, not read to send");
    }

    bool completedHttpHeader(const std::string& header) {
        return header.rfind("\r\n\r\n") != std::string::npos || header.rfind("\n\n") != std::string::npos;
    }

    std::string readHttpHeader(pollfd& pfds) {
        std::string result;
        do {
            pfds.events = POLLIN;
            if (poll(&pfds, 1, 2000) != 1) {
                throw std::runtime_error("Failed to read, timeouted");
            }
            if (pfds.revents & POLLIN) {
                std::array<char, 1000> buf;
                auto readed = recv(pfds.fd, buf.data(), buf.size(), 0);
                YIO_LOG_TRACE("Readed " << readed << " bytes");
                if (readed > 0) {
                    result.append(buf.data(), readed);
                } else if (readed == 0) { // EOF
                    break;
                }
            } else {
                throw std::runtime_error("Not enough data");
            }
        } while (completedHttpHeader(result));
        return result;
    }

    template <typename T>
    std::string inetNtop(const T& src) {
        std::array<char, 100> buf;
        return std::string(inet_ntop(AF_INET, &src, buf.data(), buf.size()));
    }

    std::string inetNtop(const in6_addr& src) {
        std::array<char, 100> buf;
        return std::string(inet_ntop(AF_INET6, &src, buf.data(), buf.size()));
    }

    std::string inetNtop(const sockaddr_in6& src) {
        return inetNtop(src.sin6_addr);
    }

    std::string inetNtop(const sockaddr_in& src) {
        return inetNtop(src.sin_addr);
    }
} // namespace

namespace quasar {
    HeadResponse parseAnswer(const std::string& header) {
        std::istringstream ss(header);
        std::string version;
        std::string code;
        ss >> version >> code;
        if (code.size() != 3 || !isdigit(code[0]) || !isdigit(code[1]) || !isdigit(code[2])) {
            throw std::runtime_error("wrong http header '" + header + "'");
        }
        return {.responseCode = std::stoi(code)};
    }

    HeadResponse doRequest(int fd, const std::string& path, const std::string& userAgent) {
        struct pollfd pfds {
            .fd = fd,
            .events = 0,
            .revents = 0,
        };
        waitForConnect(pfds);
        const std::string req = "HEAD " + path + " HTTP/1.0\r\nUser-agent: " + userAgent + "\r\n\r\n";
        auto sended = send(fd, req.c_str(), req.length(), 0);
        if (sended == -1) {
            throw ErrnoException(errno, "send()");
        }
        return parseAnswer(readHttpHeader(pfds));
    }

    void doConnect(int fd, auto addr) {
        YIO_LOG_DEBUG("Connecting to " << inetNtop(addr) << ", fd = " << fd);
        if (connect(fd, (const sockaddr*)&addr, sizeof(addr)) && errno != EINPROGRESS) {
            throw ErrnoException(errno, "connect()");
        }
    }

    HeadResponse httpHead(const std::string& host, const std::string& path, const std::string& userAgent) {
        auto resolves = resolve(host);

        int mode = AF_INET;

        // FIXME: get random address
        if (resolves.ipv4.empty()) {
            if (resolves.ipv6.empty()) {
                throw std::runtime_error("Counldnt resolve " + host);
            }
            mode = AF_INET6;
        }

        Socket sock(socket(mode, SOCK_STREAM | SOCK_NONBLOCK, 0));

        if (sock.fd == -1) {
            throw ErrnoException(errno, "socket()");
        }

        auto httpPort = []() { return HostToInet<uint16_t>(80); }; // clang-tidy workarround

        if (mode == AF_INET6) {
            auto addr = resolves.ipv6.front();
            addr.sin6_port = httpPort();
            doConnect(sock.fd, addr);
        } else {
            auto addr = resolves.ipv4.front();
            addr.sin_port = httpPort();
            doConnect(sock.fd, addr);
        }
        return doRequest(sock.fd, path, userAgent);
    }
} // namespace quasar
