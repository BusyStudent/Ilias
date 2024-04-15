#include <QMainWindow>
#include <QApplication>
#include <QUrl>
#include "../include/ilias_qt.hpp"
#include "../include/ilias_async.hpp"
#include "ui_test_qt.h"
#include <iostream>

#pragma comment(linker, "/subsystem:console")


using namespace ILIAS_NAMESPACE;

class App : public QMainWindow {
public:
    App(QIoContext *ctxt) : mCtxt(ctxt) {
        ui.setupUi(this);
        connect(ui.pushButton, &QPushButton::clicked, this, &App::onButtonClicked);
    }
    auto doGetTask() -> Task<> {
        auto editText = ui.lineEdit->text();
        if (!editText.startsWith("http")) {
            editText.prepend("http://");
        }
        auto url = QUrl(editText);
        if (!url.isValid()) {
            ui.statusbar->showMessage("BAD URL");
            co_return Result<>();
        }
        std::cout << url.path().toUtf8().constData() << std::endl;
        std::cout << url.host().toUtf8().constData() << std::endl;

        TcpClient client(*mCtxt, AF_INET);
        auto endpoint = IPEndpoint(IPAddress::fromHostname(url.host().toUtf8().data()), 80);
        auto ret = co_await client.connect(endpoint);
        if (!ret) {
            ui.statusbar->showMessage(
                QString("Connect Error: %1").arg(QString::fromUtf8(ret.error().message()))
            );
            std::cout << ret.error().message() << std::endl;
            co_return Unexpected(ret.error());
        }
        auto path = url.path().isEmpty() ? QString("/") : url.path();
        auto request = QString("GET %1 HTTP/1.1\r\nConnection: close\r\n\r\n").arg(path);
        auto requestData = request.toUtf8();
        if (auto result = co_await client.send(requestData.data(), requestData.size()); !result) {
            ui.statusbar->showMessage(
                QString("Send Error: %1").arg(QString::fromUtf8(result.error().message()))
            );
            std::cout << result.error().message() << std::endl;
            co_return Unexpected(ret.error());
        }
        QString text;
        do {
            char buffer[1024];
            auto readed = co_await client.recv(buffer, sizeof(buffer));
            if (!readed) {
                break;
            }
            if (*readed == 0) {
                break;
            }
            text.append(QUtf8StringView(buffer + 0, buffer + *readed));
        }
        while (true);
        responseData = text;
        co_return Result<>();
    }
    auto doGet() -> Task<> {
        responseData.clear();
        ui.listWidget->clear();
        ui.textBrowser->clear();
        ui.pushButton->setEnabled(false);
        co_await doGetTask();
        ui.pushButton->setEnabled(true);

        if (responseData.isEmpty()) {
            co_return Result<>();
        }
        auto list = responseData.split("\r\n\r\n");
        assert(list.size() == 2);
        auto header = list[0];
        auto body = list[1];

        for (auto &line : header.split("\r\n")) {
            if (line.isEmpty()) {
                continue;
            }
            ui.listWidget->addItem(line);
        }
        ui.textBrowser->setPlainText(body);
        co_return Result<>();
    }
    auto onButtonClicked() -> void {
        ilias_go doGet();
    }
private:
    QIoContext *mCtxt;
    Ui::MainWindow ui;
    QString responseData;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QIoContext ctxt(&app);

    App a(&ctxt);
    a.show();
    return app.exec();
}