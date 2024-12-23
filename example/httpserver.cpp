#include <ilias/task/spawn.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/fs/file.hpp>
#include <ilias/platform.hpp>
#include <ilias/buffer.hpp>
#include <ilias/net.hpp>
#include <ilias/url.hpp>
#include <unordered_map>
#include <filesystem>
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

auto sendReply(BufferedStream<TcpClient> &client, int statusCode, std::span<const std::byte> content) -> IoTask<void> {
    char headers[1024] {0};
    ::sprintf(headers, 
        "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: keep-alive\r\nServer: ILIAS\r\n\r\n", 
        statusCode, 
        statusString(statusCode), 
        int(content.size())
    );
    auto buffer = std::string_view(headers);
    if (auto ret = co_await client.writeAll(makeBuffer(buffer)); !ret || *ret != buffer.size()) {
        co_return Unexpected(ret.error_or(Error::Unknown));
    }
    if (auto ret = co_await client.writeAll(content); !ret || *ret != content.size()) {
        co_return Unexpected(ret.error_or(Error::Unknown));
    }
    co_return {};
}

auto sendReply(BufferedStream<TcpClient> &client, int statusCode, std::string_view content) {
    return sendReply(client, statusCode, makeBuffer(content));
}

auto handleHelloPage(BufferedStream<TcpClient> &client) -> IoTask<void> {
    return sendReply(client, 200, "<html>Hello World</html>");
}

auto handle404(BufferedStream<TcpClient> &client) -> IoTask<void> {
    return sendReply(client, 404, "<html>Not Found</html>");
}

auto handleMainPage(BufferedStream<TcpClient> &client) -> IoTask<void> {
    char buffer[1024] {0};
    ::sprintf(
        buffer,
        R"(
            <html>
            <h1>Test Server</h1>
            <p>Current Runtime Version: %s</p>
            <a href="/hello">Hello Page</a><br>
            <a href="/fs">Filesystem</a><br>
            </html>
        )",
        ILIAS_VERSION_STRING
    );
    co_return co_await sendReply(client, 200, std::string_view(buffer));
}

auto handleFilesytem(BufferedStream<TcpClient> &client, std::string_view pathString) -> IoTask<void> {
    pathString.remove_prefix(3);
    std::u8string u8((const char8_t *) Url::decodeComponent(pathString).c_str());
    if (u8.empty()) {
        u8 = u8"/";
    }
    std::filesystem::path path(u8);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        co_return co_await sendReply(client, 404, "<html>File Not Found</html>");
    }
    // Check is directory ?
    if (std::filesystem::is_directory(path)) {
        // List it
        std::string html;
        html += R"(<html><meta charset="utf-8" /><h1>Directory Listing</h1>)";
        auto iter = std::filesystem::directory_iterator(path, ec);
        if (ec) {
            co_return co_await sendReply(client, 500, "<html>Internal Server Error</html>");
        }
        for (auto &entry : iter) {
            html += "<a href=\"/fs";
            html += pathString;
            if (!html.ends_with('/')) {
                html += "/";
            }
            html += Url::encodeComponent((const char*) entry.path().filename().u8string().c_str());
            html += "\">";
            html += (const char*) entry.path().filename().u8string().c_str();
            html += "</a><br>";
        }
        html += "</html>";
        co_return co_await sendReply(client, 200, html);
    }
    else {
        // Read file
        auto file = co_await File::open(path.u8string(), "rb");
        if (!file) {
            co_return co_await sendReply(client, 500, "<html>Internal Server Error</html>");
        }
        auto size = (co_await file->size()).value();
        auto buffer = std::vector<std::byte>(size, std::byte{0});
        if (auto val = co_await file->readAll(buffer); !val || *val != buffer.size()) {
            co_return co_await sendReply(client, 500, "<html>Internal Server Error</html>");
        }
        co_return co_await sendReply(client, 200, buffer);
    }
}

auto handleConnection(BufferedStream<TcpClient> client) -> Task<void> {
    while (true) {
        // Get First line METHOD PATH HTTP/1.1
        auto query = co_await client.getline("\r\n");
        if (!query) {
            std::cerr << "Failed to read request, maybe per close? err => " << query.error().toString() << std::endl;
            co_return;
        }
        std::cerr << "Query: " << *query << std::endl;
        // Separate into parts
        auto [method, path, version] = splitQuery(*query); 
        if (method.empty() || path.empty() || version.empty()) {
            std::cerr << "Invalid query: " << *query << std::endl;
            co_return;
        }

        // Read all headers
        while (true) {
            auto line = co_await client.getline("\r\n");
            if (!line) {
                std::cerr << "Failed to read line: " << line.error().toString() << std::endl;
                co_return;
            }
            if (line->empty()) {
                break;
            }
            std::cerr << "Header: " << *line << std::endl;
        }

        if (method != "GET") {
            if (auto ret = co_await sendReply(client, 405, "<html>Method Not Allowed</html>"); !ret) {
                std::cerr << "Failed to send reply: " << ret.error().toString() << std::endl;
                co_return;
            }
        }
        // Route
        IoTask<void> task;
        if (path.starts_with("/fs")) {
            // Do filesystem stuff
            task = handleFilesytem(client, path);
        }
        else if (path == "/") {
            // Main page
            task = handleMainPage(client);
        }
        else if (path == "/hello") {
            // Send hello page
            task = handleHelloPage(client);
        }
        else {
            task = handle404(client);
        }
        if (auto val = co_await std::move(task); !val) {
            std::cerr << "Failed to handle request: " << val.error().toString() << std::endl;
        }
        // Send hello echo
        std::cerr << "Waiting for next request" << std::endl;
    }
    co_return;
}

auto main(int argc, char **argv) -> int {

#if defined(_WIN32)
    ::SetConsoleCP(65001);
    ::SetConsoleOutputCP(65001);
    std::setlocale(LC_ALL, ".utf-8");
#endif
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    PlatformContext ctxt;

    auto fn = []() -> Task<void> {
        auto &&ctxt = co_await currentIoContext();
        TcpListener listener(ctxt, AF_INET);
        listener.setOption(sockopt::ReuseAddress(true)).value();
        if (auto ret = listener.bind(IPEndpoint("127.0.0.1", 25565)); !ret) {
            std::cerr << "Failed to bind: " << ret.error().toString() << std::endl;
            co_return;
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
