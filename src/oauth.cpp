#include "oauth.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <windows.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    static void platformInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    static void platformCleanup() { WSACleanup(); }
    static bool invalidSocket(socket_t s) { return s == INVALID_SOCKET; }
    static void closeSocket(socket_t s) { closesocket(s); }
    static void setRecvTimeout(socket_t s, int ms) {
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <sys/time.h>
    using socket_t = int;
    static void platformInit() {}
    static void platformCleanup() {}
    static bool invalidSocket(socket_t s) { return s < 0; }
    static void closeSocket(socket_t s) { close(s); }
    static void setRecvTimeout(socket_t s, int ms) {
        struct timeval tv{};
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
#endif

#include <iostream>
#include <string>
#include <cstring>

#include <Geode/utils/web.hpp>
#include <Geode/loader/Event.hpp>

using namespace geode::async;
using namespace geode::utils;

extern std::string CLIENT_ID;
extern std::string CLIENT_SECRET;

namespace helpers {
    extern std::function<void(web::WebResponse)> webHandler;
}

void oauth::serverThread() {
    platformInit();

    socket_t lsock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(lsock, (sockaddr*)&addr, sizeof(addr));
    listen(lsock, 1);

    socket_t csock = accept(lsock, nullptr, nullptr);
    if (invalidSocket(csock)) {
        closeSocket(lsock);
        platformCleanup();
        return;
    }

    setRecvTimeout(csock, 10000);

    char buffer[4096] = {};
    if (recv(csock, buffer, 4095, 0) <= 0) {
        closeSocket(csock);
        closeSocket(lsock);
        platformCleanup();
        return;
    }

    std::string request(buffer);

    const char* response =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<h2 style='font-family:sans-serif'>There may be an error?</h2>"
            "<p style='font-family:sans-serif'>There's no oauth code, but also no error from discord. Something went wrong.</p>";
    size_t pos = request.find("GET /?code=");
    size_t posBad = request.find("GET /?error=");
    if (pos != std::string::npos) {

        auto start = pos + 11;
        auto end = request.find(' ', start);
        std::string oauth_code = request.substr(start, end - start);

        std::string params =
            "client_id=" + CLIENT_ID +
            "&client_secret=" + CLIENT_SECRET +
            "&grant_type=authorization_code"
            "&code=" + oauth_code +
            "&redirect_uri=http://localhost:8000";

        static TaskHolder<web::WebResponse> listener;

        auto req = web::WebRequest();
        req.header("Content-Type", "application/x-www-form-urlencoded");
        req.body(std::vector<uint8_t>(params.begin(), params.end()));

        geode::prelude::log::info("sending auth");

        listener.spawn(
            req.post("https://discord.com/api/oauth2/token"),
            helpers::webHandler
        );

        geode::prelude::log::info("sent auth");
        response =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<h2 style='font-family:sans-serif'>All set!</h2>"
            "<p style='font-family:sans-serif'>You can close this tab and go back to Geometry dash!</p>";

    } else if (posBad != std::string::npos) {
        response =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<h2 style='font-family:sans-serif'>Discord Returned an OAuth error</h2>"
            "<p style='font-family:sans-serif'>Check this page's url for a (somewhat) more detailed description. Try the troubleshooting steps on the tutorial site.</p>";
    }

    send(csock, response, (int)strlen(response), 0);

    closeSocket(csock);
    closeSocket(lsock);
    platformCleanup();
}
