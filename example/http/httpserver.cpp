#include <ilias/fs/file.hpp>
#include <ilias/platform.hpp>
#include <ilias/signal.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
#include <filesystem>
#include <iostream>

using namespace ILIAS_NAMESPACE;
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

auto sendReply(BufStream<TcpStream> &stream, int statusCode, std::span<const std::byte> content) -> IoTask<void> {
    char headers[1024] {0};
    ::sprintf(headers, 
        "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: keep-alive\r\nServer: ILIAS\r\n\r\n", 
        statusCode, 
        statusString(statusCode), 
        int(content.size())
    );
    auto buffer = std::string_view(headers);
    if (auto ret = co_await stream.writeAll(makeBuffer(buffer)); !ret) {
        co_return Err(ret.error());
    }
    if (auto ret = co_await stream.writeAll(content); !ret) {
        co_return Err(ret.error());
    }
    co_return {};
}

auto sendReply(BufStream<TcpStream> &stream, int statusCode, std::string_view content) {
    return sendReply(stream, statusCode, makeBuffer(content));
}

auto handleHelloPage(BufStream<TcpStream> &stream) -> IoTask<void> {
    return sendReply(stream, 200, "<html>Hello World</html>");
}

auto handle404(BufStream<TcpStream> &stream) -> IoTask<void> {
    return sendReply(stream, 404, "<html>Not Found</html>");
}

auto handleMainPage(BufStream<TcpStream> &stream) -> IoTask<void> {
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
    co_return co_await sendReply(stream, 200, std::string_view(buffer));
}

auto handleFilesytem(BufStream<TcpStream> &client, std::string_view pathString) -> IoTask<void> {
    pathString.remove_prefix(3);
    std::u8string u8((const char8_t *) pathString.data(), pathString.size());
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
            html += (const char*) entry.path().filename().u8string().c_str();
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

auto handleConnection(BufStream<TcpStream> stream) -> Task<void> {
    while (true) {
        // Get First line METHOD PATH HTTP/1.1
        auto query = co_await stream.getline("\r\n");
        if (!query) {
            std::cerr << "Failed to read request, maybe per close? err => " << query.error().message() << std::endl;
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
            auto line = co_await stream.getline("\r\n");
            if (!line) {
                std::cerr << "Failed to read line: " << line.error().message() << std::endl;
                co_return;
            }
            if (line->empty()) {
                break;
            }
            std::cerr << "Header: " << *line << std::endl;
        }

        if (method != "GET") {
            if (auto ret = co_await sendReply(stream, 405, "<html>Method Not Allowed</html>"); !ret) {
                std::cerr << "Failed to send reply: " << ret.error().message() << std::endl;
                co_return;
            }
        }
        // Route
        IoTask<void> task;
        if (path.starts_with("/fs")) {
            // Do filesystem stuff
            task = handleFilesytem(stream, path);
        }
        else if (path == "/") {
            // Main page
            task = handleMainPage(stream);
        }
        else if (path == "/hello") {
            // Send hello page
            task = handleHelloPage(stream);
        }
        else {
            task = handle404(stream);
        }
        if (auto val = co_await std::move(task); !val) {
            std::cerr << "Failed to handle request: " << val.error().message() << std::endl;
            co_return;
        }
        if (auto val = co_await stream.flush(); !val) {
            std::cerr << "Failed to flush stream: " << val.error().message() << std::endl;
            co_return;
        }
        // Send hello echo
        std::cerr << "Waiting for next request" << std::endl;
    }
    co_return;
}

void ilias_main() {
    auto main = TaskScope::enter([](TaskScope &scope) -> Task<void> {
        auto client = (co_await TcpListener::bind("127.0.0.1:25565")).value();
        std::cerr << "Listening on " << client.localEndpoint().value().toString() << std::endl;
        while (true) {
            auto [conn, endpoint] = (co_await client.accept()).value();
            scope.spawn(handleConnection(std::move(conn)));
        }
    });
    auto [done, stop] = co_await(std::move(main) || signal::ctrlC());
    if (stop) {
        std::cout << "Received Ctrl+C, shutting down..." << std::endl;
    }
}