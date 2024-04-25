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

    ctxt.runTask([&]() -> Task<> {
        std::string host = "www.baidu.com";
        Resolver resolver(ctxt);
        auto response = co_await resolver.resolve(host);
        if (!response) {
            std::cout << "DNS query failed: " << response.error().message() << std::endl;
            co_return Result<>();
        }
        for (auto &addr : response.value()) {
            std::cout << addr.toString() << std::endl;
        }
        co_return Result<>();
    }());
}