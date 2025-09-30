#include <ilias_qt/network.hpp>
#include <ilias_qt/dialog.hpp>
#include <ilias/platform/qt.hpp>
#include <ilias/fs/file.hpp>
#include <ilias/task.hpp>
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

using namespace ilias;
using namespace ilias_qt;

class App final : public QMainWindow {
public:
    App() {
        ui.setupUi(this);

        // Prep signals
        connect(ui.httpSendButton, &QPushButton::clicked, this, &App::onHttpSendButtonClicked);

        connect(ui.addrinfoButton, &QPushButton::clicked, this, &App::onAddrinfoButtonClicked);

        connect(ui.httpSaveButton, &QPushButton::clicked, this, &App::onHttpSaveButtonClicked);
    } 

    ~App() {

    }

    auto onHttpSaveButtonClicked() -> QAsyncSlot<void> {
        if (mContent.isEmpty()) {
            QMessageBox::information(this, "No content", "No content to save");
            co_return;
        }
        QFileDialog dialog(this);
        dialog.setWindowTitle("Save file");
        dialog.setDirectory("");
        dialog.setNameFilter("All Files (*)");
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.open();

        if ((co_await dialog) != QDialog::Accepted) {
            co_return;
        }
        auto filename = dialog.selectedFiles().first();
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
        co_return;
    }

    auto onHttpSendButtonClicked() -> QAsyncSlot<void> {
        ui.httpSendButton->setEnabled(false);
        co_await sendHttpRequest();
        ui.httpSendButton->setEnabled(true);
    }

    auto onAddrinfoButtonClicked() -> QAsyncSlot<void> {
        ui.addrinfoListWidget->clear();
        ui.statusbar->clearMessage();
        auto addrinfo = co_await AddressInfo::fromHostname(ui.addrinfoEdit->text().toUtf8().data());
        if (!addrinfo) {
            ui.statusbar->showMessage(QString::fromUtf8(addrinfo.error().message()));
            co_return;
        }
        for (const auto &endpoint : addrinfo->endpoints()) {
            ui.addrinfoListWidget->addItem(QString::fromUtf8(endpoint.toString()));
        }
    }

    auto sendHttpRequest() -> Task<void> {
        auto url = ui.httpUrlEdit->text();
        if (url.isEmpty()) {
            co_return;
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

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.150 Safari/537.36");
        auto reply = co_await mManager.get(QNetworkRequest(url));
        if (reply->error() != QNetworkReply::NoError) {
            ui.statusbar->showMessage(
                QString("HTTP %1 %2").arg(reply->error()).arg(reply->errorString())
            );
            co_return;
        }

        for (const auto &[key, value] : reply->rawHeaderPairs()) {
            ui.httpReplyHeadersWidget->addItem(QString("%1: %2").arg(
                QString::fromUtf8(key)).arg(QString::fromUtf8(value))
            );
        }

        auto contentType = reply->headers().value("Content-Type");
        mContent = reply->readAll();
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
        co_return;
    }

private:
    Ui::MainWindow ui;
    QNetworkAccessManager mManager;
    QByteArray mContent;
};

auto main(int argc, char **argv) -> int {
    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);
    QIoContext ctxt;
    QApplication app(argc, argv);
    App win;
    ctxt.install();
    win.show();
    return app.exec();
}