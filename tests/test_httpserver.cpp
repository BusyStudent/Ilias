#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include <filesystem>
#include <iostream>
#include <regex>

#ifdef _WIN32
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

using namespace ILIAS_NAMESPACE;

const char *statusString(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "A";
    }
}

Task<> sendResponse(IStreamClient &client, int statusCode, const void *body, int64_t n = -1) {
    if (n == -1) {
        n = ::strlen((char*)body);
    }
    char headers[1024] {0};
    ::sprintf(headers, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", statusCode, statusString(statusCode), int(n));

    auto num = co_await client.send(headers, strlen(headers));
    if (!num) {
        co_return;
    }
    while (n > 0) {
        num = co_await client.send(body, n);
        if (!num) {
            std::cout << "Client send error " << num.error().message() << std::endl;
            co_return;
        }
        if (*num == 0) {
            break;
        }
        n -= *num;
        body = ((uint8_t*) body) + *num;
    }
    co_return;
}

Task<> handleClient(IStreamClient client) {
    char headers[1024] {0};
    char requestFile[1024] {0};

    auto n = co_await client.recv(headers, sizeof(headers));
    if (!n) {
        std::cout << "Client recv error " << n.error().message() << std::endl;
        co_return;
    }
    std::cout << "Client said: " << headers << std::endl;
    
    if (::sscanf(headers, "GET %s", requestFile) != 1) {
        co_await sendResponse(client, 500, "<html>Internal Server Error!</html>");
        co_return;
    }
    auto path = std::filesystem::current_path();
    path = path.generic_u8string();
    path += reinterpret_cast<const char8_t*>(requestFile);
    if (!std::filesystem::exists(path)) {
        co_await sendResponse(client, 404, "<html>File not found!</html>");
        co_return;
    }
    if (std::filesystem::is_directory(path)) {
        std::string response = R"(<html><body><meta http-equiv="Content-Type" content="text/html; charset=utf-8" />)";
        for (auto &file : std::filesystem::directory_iterator(path)) {
            if (!file.exists()) {
                continue;
            }
            auto filename = file.path().filename().u8string();
            auto u8name = reinterpret_cast<const char *>(filename.c_str());
            std::string prefix = requestFile; //< Skip begin /
            prefix.erase(prefix.begin());
            if (!prefix.empty()) {
                prefix += '/';
            }
            response += std::string("<a href=\"") + prefix + u8name + "\">" + u8name + "</a><br>";
        }
        response += "</body></html>";
        co_await sendResponse(client, 200, response.c_str(), response.size());
        co_return;
    }
    FILE *file = ::fopen(path.string().c_str(), "rb");
    if (!file) {
        co_await sendResponse(client, 500, "<html>Internal Server Error!</html>");
        co_return;
    }
    ::fseek(file, 0, SEEK_END);
    auto size = ::ftell(file);
    ::fseek(file, 0, SEEK_SET);

    std::vector<char> buffer(size);
    if (::fread(buffer.data(), 1, size, file) != size) {
        ::fclose(file);
        co_await sendResponse(client, 500, "<html>Internal Server Error!</html>");
        co_return;
    }
    ::fclose(file);
    co_await sendResponse(client, 200, buffer.data(), buffer.size());
    co_return;
}

int main() {
    MiniEventLoop loop;
#ifdef _WIN32
    IOCPContext ctxt;
#else
    PollContext ctxt;
#endif


    loop.runTask([&]() -> Task<> {
        TcpListener tcpListener(ctxt, AF_INET);
        IStreamListener listener = std::move(tcpListener);
        if (auto ret = listener.bind("127.0.0.1:0"); !ret) {
            std::cout << ret.error().message() << std::endl;
            co_return;
        }
        std::cout << "Server bound to " << listener.localEndpoint()->toString() << std::endl;
        while (true) {
            auto ret = co_await listener.accept();
            if (!ret) {
                std::cout << ret.error().message() << std::endl;
                co_return;
            }
            auto &[client, addr] = *ret;
            std::cout << "New client from " << addr.toString() << std::endl;

            // Handle this
            loop.spawn(handleClient, std::move(client));
        }
        co_return;
    }());
}