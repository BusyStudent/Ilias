#include <asio.hpp>
#include <iostream>
#include <string>
#include <memory>

using asio::ip::tcp;

class HTTPSession : public std::enable_shared_from_this<HTTPSession> {
public:
    HTTPSession(tcp::socket socket)
        : socket_(std::move(socket)) {
        response_data_.resize(10 * 1024, 0);
        
        response_header_ = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 10240\r\n"
            "Connection: keep-alive\r\n"
            "Keep-Alive: timeout=5, max=1000\r\n"
            "\r\n";
    }

    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(
            asio::buffer(buffer_),
            [this, self](std::error_code ec, std::size_t) {
                if (!ec) {
                    do_write();
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        
        asio::async_write(
            socket_,
            asio::buffer(response_header_),
            [this, self](std::error_code ec, std::size_t) {
                if (!ec) {
                    asio::async_write(
                        socket_,
                        asio::buffer(response_data_),
                        [this, self](std::error_code ec, std::size_t) {
                            if (!ec) {
                                do_read();
                            }
                        });
                }
            });
    }

    tcp::socket socket_;
    std::array<char, 1024> buffer_;
    std::string response_header_;
    std::string response_data_;
};

class HTTPServer {
public:
    HTTPServer(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        for (int i = 0; i < 32; ++i) {
            do_accept();            
        }
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](std::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<HTTPSession>(std::move(socket))->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main() {
    try {
        asio::io_context ctxt;
        HTTPServer server(ctxt, 8080);
        std::cout << "HTTP server is running on port 8080...\n";
        ctxt.run();
    }
    catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}