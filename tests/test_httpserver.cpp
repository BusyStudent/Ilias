#include "../include/ilias/networking.hpp"
#include <filesystem>
#include <iostream>

using namespace ILIAS_NAMESPACE;

const char *statusString(int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "A";
    }
}

Task<> sendResponse(BufferedStream<TcpClient> &client, int statusCode, const void *body, int64_t n = -1) {
    if (n == -1) {
        n = ::strlen((char*)body);
    }
    char headers[1024] {0};
    ::sprintf(headers, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n", statusCode, statusString(statusCode), int(n));

    auto num = co_await client.sendAll(headers, strlen(headers));
    if (!num) {
        co_return Result<>();
    }
    while (n > 0) {
        num = co_await client.sendAll(body, n);
        if (!num) {
            std::cout << "Client send error " << num.error().message() << std::endl;
            co_return Result<>();
        }
        if (*num == 0) {
            break;
        }
        n -= *num;
        body = ((uint8_t*) body) + *num;
    }
    co_return Result<>();
}

Task<> runClient(ByteStream<TcpClient> &client) {

while (true) {
    auto line = co_await client.getline("\r\n");
    if (!line || line->empty()) {
        co_return {};
    }
    char requestFile[1024];
    if (::sscanf(line->c_str(), "GET %s", requestFile) != 1) {
        co_await sendResponse(client, 500, "<html>Internal Server Error!</html>");
        co_return Result<>();
    }
    while (!line->empty()) {
        line = co_await client.getline("\r\n");
        if (!line) {
            co_return {};
        }
    }

#if 0
    auto path = std::filesystem::current_path();
    path = path.generic_u8string();
    path += reinterpret_cast<const char8_t*>(requestFile);
    if (!std::filesystem::exists(path)) {
        if (!co_await sendResponse(client, 404, "<html>File not found!</html>")) {
            co_return {};
        }
        continue;
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
        if (!co_await sendResponse(client, 200, response.c_str(), response.size())) {
            co_return {};
        }
        continue;
    }
    FILE *file = ::fopen(path.string().c_str(), "rb");
    if (!file) {
        if (!co_await sendResponse(client, 500, "<html>Internal Server Error!</html>")) {
            co_return {};
        }
        continue;
    }
    ::fseek(file, 0, SEEK_END);
    auto size = ::ftell(file);
    ::fseek(file, 0, SEEK_SET);

    std::vector<char> buffer(size);
    if (::fread(buffer.data(), 1, size, file) != size) {
        ::fclose(file);
        if (!co_await sendResponse(client, 500, "<html>Internal Server Error!</html>")) {
            co_return {};
        }
        continue;
    }
    ::fclose(file);
    if (!co_await sendResponse(client, 200, buffer.data(), buffer.size())) {
        co_return {};
    }
#else
    if (!co_await sendResponse(client, 200, "<html> Hello world from the echo server </html>")) {
        co_return {};
    }
#endif
}
}
Task<> handleClient(BufferedStream<TcpClient> client) {
    auto err = co_await runClient(client);
    if (!err) {
        std::cout << "Closeing by " << err.error().toString() << std::endl;
    }
    co_return {};
}

int main() {
    PlatformIoContext ctxt;
    ctxt.runTask([&]() -> Task<> {
        TcpListener listener(ctxt, AF_INET);
        if (auto ret = listener.bind("127.0.0.1:0", 100); !ret) {
            std::cout << ret.error().message() << std::endl;
            co_return Result<>();
        }
        std::cout << "Server bound to " << listener.localEndpoint()->toString() << std::endl;
        while (true) {
            auto ret = co_await listener.accept();
            if (!ret) {
                std::cout << ret.error().message() << std::endl;
                co_return Result<>();
            }
            auto &[client, addr] = *ret;
            std::cout << "New client from " << addr.toString() << std::endl;

            // Handle this
            ctxt.spawn(handleClient, std::move(client));
        }
        co_return Result<>();
    }());
}