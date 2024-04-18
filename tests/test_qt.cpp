#include <QMainWindow>
#include <QApplication>
#include <QUrl>
#include "../include/ilias_qt.hpp"
#include "../include/ilias_async.hpp"
#include "../include/ilias_http.hpp"
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

        HttpRequest request(url.toString().toUtf8().constData());
        auto reply = co_await mSession.get(request);
        if (!reply) {
            co_return Result<>();
        }
        for (const auto &item : reply->headers()) {
            ui.listWidget->addItem(QString::fromUtf8(item.first + ": " + item.second));
        }
        ui.textBrowser->setPlainText(
            QString::fromUtf8((co_await reply->text()).value_or("BAD TEXT"))
        );
        co_return Result<>();
    }
    auto doGet() -> Task<> {
        ui.listWidget->clear();
        ui.textBrowser->clear();
        ui.pushButton->setEnabled(false);
        co_await doGetTask();
        ui.pushButton->setEnabled(true);
        co_return Result<>();
    }
    auto onButtonClicked() -> void {
        ilias_go doGet();
    }
private:
    QIoContext *mCtxt;
    HttpSession mSession;
    Ui::MainWindow ui;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QIoContext ctxt(&app);

    App a(&ctxt);
    a.show();
    return app.exec();
}