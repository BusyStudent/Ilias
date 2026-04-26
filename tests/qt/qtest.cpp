#include <ilias_qt/object.hpp>
#include <ilias_qt/adapter.hpp>
#include <ilias/platform/qt.hpp>
#include <ilias/task.hpp>
#include <ilias/net.hpp>
#include <ilias/io.hpp>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QTimer>
#include <QTest>
#include "qtest.hpp"

using namespace ilias;
using namespace ilias::literals;
using namespace std::literals;

Testing::Testing() {
    mCtxt.install();
}

void Testing::testTask() {
    auto fn = []() -> Task<void> {
        co_return;
    };
    fn().wait();
}

void Testing::testTimer() {
    auto fn = []() -> Task<void> {
        auto now = std::chrono::steady_clock::now();
        co_await ilias::sleep(10ms);  
        auto cost = std::chrono::steady_clock::now() - now;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(cost);
        qDebug() << "cost:" << ms;
    };
    fn().wait();
}

void Testing::testTcp() {
    // Test sending and receiving data
    auto server = [](TcpListener listener) -> IoTask<void> {
        auto [peer, _] = ILIAS_CO_TRY(co_await listener.accept());
        auto buffer = "Hello World"_bin;
        ILIAS_CO_TRY(co_await peer.writeAll(buffer));
        co_return {};

    };
    auto client = [](IPEndpoint endpoint) -> IoTask<void> {
        auto stream = ILIAS_CO_TRY(co_await TcpStream::connect(endpoint));
        auto str = std::string {};
        ILIAS_CO_TRY(co_await stream.readToEnd(str));
        if (str != "Hello World") {
            co_return Err(IoError::Other);
        }
        co_return {};
    };
    auto fn = [&]() -> IoTask<void> {
        // Prepare the listener
        auto listener = ILIAS_CO_TRY(co_await TcpListener::bind("127.0.0.1:0"));
        auto endpoint = ILIAS_CO_TRY(listener.localEndpoint());

        // Connect to it
        auto [c, s] = co_await whenAll(
            client(endpoint),
            server(std::move(listener))
        );
        // Check it
        ILIAS_CO_TRY(c);
        ILIAS_CO_TRY(s);
        co_return {};
    };
    auto result = fn().wait();
    if (!result) {
        qDebug() << "Error: " << result.error().message();
    }
    QVERIFY(result);
}

void Testing::testSignal() {
    // Normal
    QTimer::singleShot(10ms, this, &Testing::notify);
    auto fn = [this]() -> Task<void> {
        co_await ilias_qt::QSignal(this, &Testing::notify);
    };
    fn().wait();

    // Cancel
    auto handle = ilias::spawn(fn());
    handle.stop();
    handle.wait();
}

void Testing::testStream() {
    // Test Read the hello world
    auto fn = [&]() -> IoTask<void> {
        QByteArray array {"Hello World"};
        QBuffer buffer {&array};
        buffer.open(QIODevice::ReadOnly);
        ilias_qt::StreamAdapter stream {&buffer};

        auto str = std::string {};
        ILIAS_CO_TRY(co_await stream.readToEnd(str));
        co_return {};
    };
    QVERIFY(fn().wait());

    // Test write some data
    auto fn2 = [&]() -> IoTask<void> {
        QByteArray array;
        QBuffer buffer {&array};
        buffer.open(QIODevice::WriteOnly);
        ilias_qt::StreamAdapter stream {&buffer};

        ILIAS_CO_TRY(co_await stream.writeAll("Hello World"_bin));
        ILIAS_CO_TRY(co_await stream.flush());
        co_return {};
    };
    QVERIFY(fn2().wait());

    // Test with streaming
    QNetworkAccessManager manager;  
    auto fn3 = [&]() -> IoTask<void> {
        QNetworkRequest request {QUrl {"https://www.baidu.com"}};
        auto reply = manager.get(request);
        if (!reply) {
            co_return Err(IoError::Other);
        }
        struct Guard {
            QNetworkReply *reply;
            ~Guard() {
                reply->deleteLater();
            }
        } guard {reply};

        auto stream = ilias_qt::StreamAdapter {reply};
        auto string = std::string {};
        ILIAS_CO_TRY(co_await stream.readToEnd(string));
        co_return {};
    };
    QVERIFY(fn3().wait());
}

QTEST_MAIN(Testing)