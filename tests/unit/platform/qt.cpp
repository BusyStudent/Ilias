#include <ilias/platform/qt.hpp>
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
            spawn(sendHttpRequest());
        });

        connect(ui.addrinfoButton, &QPushButton::clicked, this, [this]() {
            spawn(sendGetAddrInfo());
        });

        connect(ui.httpSaveButton, &QPushButton::clicked, this, [this]() {
            if (mContent.empty()) {
                QMessageBox::information(this, "No content", "No content to save");
                return;
            }
            auto filename = QFileDialog::getSaveFileName(this, "Save file", "", "All Files (*)");
            if (filename.isEmpty()) {
                return;
            }
            QFile file(filename);
            if (!file.open(QIODevice::WriteOnly)) {
                QMessageBox::critical(this, "Error", "Could not open file for writing");
                return;
            }
            file.write(reinterpret_cast<const char*>(mContent.data()), mContent.size());
            file.close();
        });

        connect(ui.httpProxyButton, &QPushButton::clicked, this, [this]() {
            auto prevProxy = mSession.proxy();
            auto proxy = QInputDialog::getText(this, "Proxy", "Proxy URL:", QLineEdit::Normal, QString::fromUtf8(prevProxy.toString()));
            mSession.setProxy(proxy.toUtf8().data());
        });
    } 

    auto sendHttpRequest() -> Task<void> {
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

    auto sendGetAddrInfo() -> Task<void> {
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
private:
    QIoContext mCtxt;
    HttpCookieJar mCookieJar;
    HttpSession mSession {mCtxt};
    Ui::MainWindow ui;
    std::vector<std::byte> mContent;
};

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    QApplication app(argc, argv);
    App win;
    win.show();
    return app.exec();
}
