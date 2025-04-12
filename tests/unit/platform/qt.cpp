#include <ilias/platform/qt_utils.hpp>
#include <ilias/platform/qt.hpp>
#include <ilias/fs/console.hpp>
#include <ilias/fs/file.hpp>
#include <ilias/http.hpp>
#include <ilias/net.hpp>

#include <QApplication>
#include <QMainWindow>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTabWidget>
#include "ui_qt.h"

#if defined(_WIN32)
    #pragma comment(linker, "/subsystem:console") // Windows only
#endif

using namespace ILIAS_NAMESPACE;

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);
        mSession.setCookieJar(&mCookieJar);

        // Prep signals
        connect(ui.httpSendButton, &QPushButton::clicked, this, [this]() {
            spawn([this]() -> Task<> {
                ui.httpSendButton->setEnabled(false);
                co_await sendHttpRequest();
                ui.httpSendButton->setEnabled(true);
            });
        });

        connect(ui.addrinfoButton, &QPushButton::clicked, this, [this]() {
            spawn(sendGetAddrInfo());
        });

        connect(ui.httpSaveButton, &QPushButton::clicked, this, &App::onHttpSaveButtonClicked);

        connect(ui.httpProxyButton, &QPushButton::clicked, this, [this]() {
            auto prevProxy = mSession.proxy();
            auto proxy = QInputDialog::getText(this, "Proxy", "Proxy URL:", QLineEdit::Normal, QString::fromUtf8(prevProxy.toString()));
            mSession.setProxy(proxy.toUtf8().data());
        });

        connect(ui.tcpEchoButton, &QPushButton::clicked, this, [this]() {
            if (mEchoServerHandle) {
                mEchoServerHandle.cancel();
                mEchoServerHandle.wait();
                ui.tcpEchoButton->setText("Start");
            }
            else {
                mEchoServerHandle = spawn(echoServer());
                ui.tcpEchoButton->setText("Stop");
            }
        });

        connect(ui.tcpTestButton, &QPushButton::clicked, this, [this]() {
            spawn(echoTest());
        });

        connect(ui.consoleStartButton, &QPushButton::clicked, this, [this]() {
            if (!mConsoleListenerHandle) {
                ui.consoleStartButton->setText("Stop");
                mConsoleListenerHandle = spawn(consoleListener());
            }
            else {
                ui.consoleStartButton->setText("Start");
                mConsoleListenerHandle.cancel();
                mConsoleListenerHandle.wait();
            }
        });

        connect(ui.wsOpenButton, &QPushButton::clicked, this, [this]() {
            if (!mWsHandle) {
                ui.wsOpenButton->setText("Close");
                mWsHandle = spawn(wsOpen());
            }
            else {
                ui.wsOpenButton->setText("Open");
                mWsHandle.cancel();
                mWsHandle.wait();
            }
        });

        connect(ui.wsSendButton, &QPushButton::clicked, this, [this]() {
            if (mWs) {
                spawn(wsSend());
            }
        });
    } 

    ~App() {
        if (mEchoServerHandle) {
            mEchoServerHandle.cancel();            
            mEchoServerHandle.wait();
        }
        if (mConsoleListenerHandle) {
            mConsoleListenerHandle.cancel();
            mConsoleListenerHandle.wait();
        }
        if (mWsHandle) {
            mWsHandle.cancel();
            mWsHandle.wait();
        }
    }

    auto onHttpSaveButtonClicked() -> QAsyncSlot<void> {
        co_await backtrace();
        if (mContent.empty()) {
            QMessageBox::information(this, "No content", "No content to save");
            co_return;
        }
        auto filename = QFileDialog::getSaveFileName(this, "Save file", "", "All Files (*)");
        if (filename.isEmpty()) {
            co_return;
        }
#if 0
        QFile file(filename);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "Error", "Could not open file for writing");
            co_return;
        }
        file.write(reinterpret_cast<const char*>(mContent.data()), mContent.size());
        file.close();
#else
        auto file = co_await File::open(filename.toStdString(), "wb");
        if (!file) {
            co_return;
        }
        auto n = co_await file->writeAll(makeBuffer(mContent));
#endif
    }

    auto sendHttpRequest() -> IoTask<void> {
        auto url = ui.httpUrlEdit->text();
        if (url.isEmpty()) {
            co_return {};
        }
        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            url = "http://" + url;
        }
        // Clear status
        ui.statusbar->clearMessage();
        ui.httpReplyHeadersWidget->clear();

        ui.httpContentBroswer->clear();
        ui.httpContentBroswer->hide();
        ui.httpImageLabel->hide();

        HttpRequest request;
        request.setUrl(url.toUtf8().data());
        request.setHeader(HttpHeaders::UserAgent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.150 Safari/537.36");
        auto reply = co_await mSession.sendRequest(ui.httpMethodBox->currentText().toUtf8().data(), request);
        if (!reply) {
            ui.statusbar->showMessage(QString::fromUtf8(reply.error().toString()));
            co_return {};
        }
        auto content = co_await reply->content();
        if (!content) {
            ui.statusbar->showMessage(QString::fromUtf8(content.error().toString()));
            co_return {};
        }
        mContent = std::move(*content);
        for (const auto &[key, value] : reply->headers()) {
            ui.httpReplyHeadersWidget->addItem(QString("%1: %2").arg(
                QString::fromUtf8(key)).arg(QString::fromUtf8(value))
            );
        }

        auto contentType = reply->headers().value("Content-Type");
        if (contentType.contains("image/")) {
            ui.httpImageLabel->setPixmap(QPixmap::fromImage(QImage::fromData(mContent)));
            ui.httpImageLabel->show();
        }
        else if (contentType.contains("text/")) {
            ui.httpContentBroswer->setPlainText(QString::fromUtf8(mContent));
            ui.httpContentBroswer->show();
        }
        else {
            ui.httpContentBroswer->setPlainText(QString::fromUtf8(mContent));
            ui.httpContentBroswer->show();
        }

        ui.statusbar->showMessage(
            QString("HTTP %1 %2").arg(reply->statusCode()).arg(QString::fromUtf8(reply->status()))
        );
        updateCookieJar();
        co_return {};
    }

    auto sendGetAddrInfo() -> IoTask<void> {
        ui.addrinfoListWidget->clear();
        ui.statusbar->clearMessage();
        auto addrinfo = co_await AddressInfo::fromHostnameAsync(ui.addrinfoEdit->text().toUtf8().data());
        if (!addrinfo) {
            ui.statusbar->showMessage(QString::fromUtf8(addrinfo.error().toString()));
            co_return {};
        }
        for (const auto &addr : addrinfo->addresses()) {
            ui.addrinfoListWidget->addItem(QString::fromUtf8(addr.toString()));
        }
        co_return {};
    }

    auto updateCookieJar() -> void {
        auto cookies = mCookieJar.allCookies();
        ui.cookieWidget->clear();
        for (const auto &cookie : cookies) {
            auto item = new QTreeWidgetItem(ui.cookieWidget);
            item->setText(0, QString::fromUtf8(cookie.domain()));
            item->setText(1, QString::fromUtf8(cookie.name()));
            item->setText(2, QString::fromUtf8(cookie.value()));
            item->setText(3, QString::fromUtf8(cookie.path()));
        }
    }

    // Tcp Test Here
    /**
     * @brief The packet for the echo server
     * 
     */
    struct EchoPacket {
        uint64_t len; //< The data size
        uint64_t sendedTime; //< The time the client sended
        uint64_t receivedTime; //< The time the echo server was received
    };

    auto echoServer() -> Task<void> {
        auto handle = [](TcpClient client) -> Task<void> {
            EchoPacket packet;
            while (true) {
                auto ret = co_await client.readAll(makeBuffer(&packet, sizeof(packet)));
                if (!ret || *ret != sizeof(packet)) {
                    co_return;
                }
                // Update the packet with the current time and send back
                packet.receivedTime = std::chrono::system_clock::now().time_since_epoch().count();

                // Drop the data after the packet
                std::vector<std::byte> buffer;
                buffer.resize(packet.len);
                ret = co_await client.readAll(buffer);
                if (!ret || *ret != packet.len) {
                    co_return;
                }

                // Do Send back the packet here
                ret = co_await client.writeAll(makeBuffer(&packet, sizeof(packet)));
                if (!ret || *ret != sizeof(packet)) {
                    co_return;
                }
                // Send back the data after the packet
                ret = co_await client.writeAll(buffer);
                if (!ret || *ret != buffer.size()) {
                    co_return;
                }
            }
        };
        IPEndpoint endpoint(ui.tcpEchoEdit->text().toUtf8().data());
        TcpListener listener {mCtxt, endpoint.family()};
        if (auto ret = listener.bind(endpoint); !ret) {
            ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
        }
        while (auto ret = co_await listener.accept()) {
            auto &[client, endpoint] = ret.value();
            spawn(handle(std::move(client)));
        }
    }

    auto echoTest() -> Task<void> {
        ui.tcpLogWidget->clear();
        auto endpoint = IPEndpoint(ui.tcpTestEdit->text().toUtf8().data());
        TcpClient client {mCtxt, endpoint.family()};
        client.setOption(sockopt::TcpNoDelay(true));
        ui.tcpLogWidget->addItem(QString::fromUtf8("Connecting to " + endpoint.toString()));
        if (auto ret = co_await client.connect(endpoint); !ret) {
            ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
            co_return;
        }
        ui.tcpLogWidget->addItem("Connected");
        
        // Sending the data
        for (int i = 0; i < ui.tcpCountBox->value(); ++i) {
            EchoPacket packet {};
            packet.len = ui.tcpDataSizeBox->value();
            packet.sendedTime = std::chrono::system_clock::now().time_since_epoch().count();
            ui.tcpLogWidget->addItem(QString::fromUtf8("idx: " + std::to_string(i) + " Sending " + std::to_string(packet.len) + " bytes"));
            auto ret = co_await client.writeAll(makeBuffer(&packet, sizeof(packet)));
            if (!ret || *ret != sizeof(packet)) {
                ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
                co_return;
            }
            // Send back the data after the packet
            std::vector<std::byte> buffer;
            buffer.resize(packet.len);
            ret = co_await client.writeAll(buffer);
            if (!ret || *ret != buffer.size()) {
                ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
            }
            ui.tcpLogWidget->addItem("Sent");
        }

        // Receiving the data
        for (int i = 0; i < ui.tcpCountBox->value(); ++i) {
            EchoPacket packet {};
            auto ret = co_await client.readAll(makeBuffer(&packet, sizeof(packet)));
            if (!ret || *ret != sizeof(packet)) {
                ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
                co_return;
            }
            ui.tcpLogWidget->addItem(QString::fromUtf8("idx: " + std::to_string(i) + " Received " + std::to_string(packet.len) + " bytes"));
            // Receive the data after the packet
            std::vector<std::byte> buffer;
            buffer.resize(packet.len);
            ret = co_await client.readAll(buffer);
            if (!ret || *ret != buffer.size()) {
                ui.statusbar->showMessage(QString::fromUtf8(ret.error().toString()));
            }
            // Print the diff
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            auto diff = std::chrono::system_clock::duration(now - packet.sendedTime);
            auto diffMs = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
            ui.tcpLogWidget->addItem(
                QString::fromUtf8(
                    "Received in " + std::to_string(diffMs) + " ms" +
                    " with " + std::to_string(packet.len) + " bytes data"
                )
            );
        }
    }

    auto consoleListener() -> Task<void> {
        auto in = co_await Console::fromStdin();
        if (!in) {
            ui.statusbar->showMessage(QString::fromUtf8(in.error().toString()));
            co_return;
        }
        while (true) {
            auto string = co_await in->getline();
            if (!string) {
                co_return;
            }
            ui.consoleListWidget->addItem(QString::fromUtf8(*string));
        }
    }

    auto wsOpen() -> Task<void> {
        WebSocket ws { ui.wsUrlEdit->text().toUtf8().data() };
        if (auto res = co_await ws.open(); !res) {
            ui.statusbar->showMessage(QString::fromUtf8(res.error().toString()));
            co_return;
        }
        mWs = &ws;
        while (true) {
            auto msg = co_await ws.recvMessage();
            if (!msg) {
                break;
            }
            auto [buffer, type] = std::move(*msg);
            if (type == WebSocket::TextMessage) {
                ui.wsReceivedWidget->addItem(QString::fromUtf8(reinterpret_cast<const char *>(buffer.data()), buffer.size()));
            }
            else {
                ui.wsReceivedWidget->addItem(QByteArray::fromRawData(reinterpret_cast<const char *>(buffer.data()), buffer.size()).toHex());
            }
        }
        co_await ws.shutdown();
    }

    auto wsSend() -> Task<void> {
        auto text = ui.wsMessageEdit->text().toUtf8();
        ui.wsMessageEdit->clear();
        if (auto res = co_await mWs->sendMessage(text.data()); !res) {
            ui.statusbar->showMessage(QString::fromUtf8(res.error().toString()));
            co_return;
        }
    }
private:
    QIoContext mCtxt;
    HttpCookieJar mCookieJar;
    HttpSession mSession {mCtxt};
    Ui::MainWindow ui;
    std::vector<std::byte> mContent;
    WebSocket *mWs = nullptr; // WebSocket

    WaitHandle<void> mEchoServerHandle;
    WaitHandle<void> mConsoleListenerHandle;
    WaitHandle<void> mWsHandle; // Handle for ws recving the message
};

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    QApplication app(argc, argv);
    App win;
    win.show();
    return app.exec();
}
