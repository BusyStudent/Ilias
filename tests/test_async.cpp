#include "../include/ilias_async.hpp"
#include "../include/ilias_loop.hpp"
#include <iostream>

using namespace ILIAS_NAMESPACE;

int main() {
    NativeEventLoop loop;
    IOContext ctxt;

    loop.runTask([&]() -> Task<> {
        TcpClient client(ctxt, AF_INET);
        co_return;
    }());
}