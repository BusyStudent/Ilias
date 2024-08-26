#include <ilias/platform/platform.hpp>
#include <ilias/task/spawn.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/buffer.hpp>
#include <iostream>

using namespace ILIAS_NAMESPACE;

/**
 * @brief Split the query into method, path and http version
 * 
 * @param query 
 * @return std::tuple<std::string_view, std::string_view, std::string_view> 
 */
auto splitQuery(std::string_view query) -> std::tuple<std::string_view, std::string_view, std::string_view> {
    auto pos = query.find(' ');
    if (pos == std::string_view::npos) {
        return {};
    }
    auto method = query.substr(0, pos);
    query = query.substr(pos + 1);
    pos = query.find(' ');
    if (pos == std::string_view::npos) {
        return {};
    }
    auto path = query.substr(0, pos);
    query = query.substr(pos + 1);
    return {method, path, query};
}

auto statusString(int statusCode) -> const char * {
    switch (statusCode) {
        case 200: return "OK";
        case 403: return "Forbidden";
        case 404: return "Not Found";

        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";

        default: return "Unknown";
    }
}

auto sendReply(BufferedStream<TcpClient> &client, int statusCode, std::string_view content) -> Task<void> {
    char headers[1024] {0};
    ::sprintf(headers, 
        "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: keep-alive\r\nServer: ILIAS\r\n\r\n", 
        statusCode, 
        statusString(statusCode), 
        int(content.size())
    );
    auto buffer = std::string_view(headers);
    if (auto ret = co_await client.writeAll(as_buffer(buffer)); !ret || *ret != buffer.size()) {
        co_return Unexpected(ret.error_or(Error::Unknown));
    }
    if (auto ret = co_await client.writeAll(as_buffer(content)); !ret || *ret != content.size()) {
        co_return Unexpected(ret.error_or(Error::Unknown));
    }
    co_return {};
}

auto handleConnection(BufferedStream<TcpClient> client) -> Task<void> {
    while (true) {
        // Get First line METHOD PATH HTTP/1.1
        auto query = co_await client.getline("\r\n");
        if (!query) {
            std::cerr << "Failed to read request, maybe per close? err => " << query.error().toString() << std::endl;
            co_return {};
        }
        std::cerr << "Query: " << *query << std::endl;
        // Separate into parts
        auto [method, path, version] = splitQuery(*query); 
        if (method.empty() || path.empty() || version.empty()) {
            std::cerr << "Invalid query: " << *query << std::endl;
            co_return {};
        }

        // Read all headers
        while (true) {
            auto line = co_await client.getline("\r\n");
            if (!line) {
                std::cerr << "Failed to read line: " << line.error().toString() << std::endl;
                co_return {};
            }
            if (line->empty()) {
                break;
            }
            std::cerr << "Header: " << *line << std::endl;
        }

        if (method != "GET") {
            if (auto ret = co_await sendReply(client, 405, "<html>Method Not Allowed</html>"); !ret) {
                std::cerr << "Failed to send reply: " << ret.error().toString() << std::endl;
                co_return {};
            }
        }
        // Send hello echo
        if (auto ret = co_await sendReply(client, 200, "<html>Hello World</html>"); !ret) {
            std::cerr << "Failed to send reply: " << ret.error().toString() << std::endl;
            co_return {};
        }
        std::cerr << "Waiting for next request" << std::endl;
    }
    co_return {};
}

auto main(int argc, char **argv) -> int {

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    PlatformContext ctxt;

    auto fn = [&]() -> Task<void> {
        TcpListener listener(ctxt, AF_INET);
        if (auto ret = listener.bind(IPEndpoint("127.0.0.1", 25565)); !ret) {
            std::cerr << "Failed to bind: " << ret.error().toString() << std::endl;
            co_return {};
        }
        std::cout << "Listening on " << listener.localEndpoint().value().toString() << std::endl;
        while (true) {
            auto ret = co_await listener.accept();
            if (!ret) {
                std::cerr << "Failed to accept: " << ret.error().toString() << std::endl;
                continue;
            }
            auto &[client, endpoint] = *ret;
            std::cout << "Accepted connection from " << endpoint.toString() << std::endl;

            // Start it
            spawn(handleConnection(std::move(client)));
        }
    };
    fn().wait();
}
