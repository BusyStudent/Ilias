#include "../include/ilias_resolver.hpp"
#include "../include/ilias_async.hpp"
#include "../include/ilias_co.hpp"
#include <iostream>

#ifdef _WIN32
    #include "../include/ilias_iocp.hpp"
    #include "../include/ilias_iocp.cpp"
#else
    #include "../include/ilias_poll.hpp"
#endif

using namespace ILIAS_NAMESPACE;

int main() {
#ifdef _WIN32
    IOCPContext ctxt;
#else
    PollContext ctxt;
#endif
    Resolver resolver(ctxt);
    auto showResult = [&](std::string_view host) -> Task<> {
        std::cout << "resolveing: " << host << std::endl;
        auto response = co_await resolver.resolve(host);
        if (!response) {
            std::cout << "DNS query failed: " << response.error().message() << std::endl;
            co_return Result<>();
        }
        for (auto &addr : response.value()) {
            std::cout << addr.toString() << std::endl;
        }
        co_return Result<>();
    };

    ctxt.runTask([&]() -> Task<> {
        co_await showResult("www.baidu.com");
        co_await showResult("pan.baidu.com");
        co_await showResult("google.com");
        co_return Result<>();
    }());
}