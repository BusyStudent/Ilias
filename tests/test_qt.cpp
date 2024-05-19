#include <QMainWindow>
#include <QApplication>
#include <QInputDialog>
#include <QUrl>
#include <QFile>
#include "../include/ilias_qt.hpp"
#include "../include/ilias_async.hpp"
#include "../include/ilias_resolver.hpp"
#include "../include/ilias_http.hpp"
#include "ui_test_qt.h"
#include <iostream>

#pragma comment(linker, "/subsystem:console")


using namespace ILIAS_NAMESPACE;

class App : public QMainWindow {
public:
    App(QIoContext *ctxt) : mCtxt(ctxt), mSession(*ctxt), mResolver(*ctxt) {
        mSession.setCookieJar(&mJar);
        ui.setupUi(this);
        ui.imageLabel->setVisible(false);
        connect(ui.pushButton, &QPushButton::clicked, this, &App::onButtonClicked);
        connect(ui.hostnameEdit, &QLineEdit::returnPressed, this, &App::onQueryHost);
        connect(ui.actionProxy, &QAction::triggered, this, &App::onProxy);
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
        request.setHeader(HttpHeaders::UserAgent, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/88.0.4324.150 Safari/537.36");
        auto reply = co_await mSession.get(request);
        if (!reply) {
            ui.statusbar->showMessage(QString::fromUtf8(reply.error().message()));
            co_return Result<>();
        }
        for (const auto &item : reply->headers()) {
            ui.listWidget->addItem(QString::fromUtf8(item.first + ": " + item.second));
        }
        if (reply->headers().value(HttpHeaders::ContentType) == "image/png" || 
            reply->headers().value(HttpHeaders::ContentType) == "image/jpeg" || 
            reply->headers().value(HttpHeaders::ContentType) == "image/gif" ||
            reply->headers().value(HttpHeaders::ContentType) == "image/svg+xml" ||
            reply->headers().value(HttpHeaders::ContentType) == "image/webp" )
        {
            auto data = co_await reply->content();
            auto image = QImage::fromData(data.value());
            if (image.isNull()) {
                ui.statusbar->showMessage("BAD IMAGE");
                co_return Result<>();
            }
            ui.textBrowser->setVisible(false);
            ui.imageLabel->setVisible(true);
            ui.imageLabel->setPixmap(QPixmap::fromImage(image));
        }
        else {
            ui.textBrowser->setPlainText(
                QString::fromUtf8((co_await reply->text()).value_or("BAD TEXT"))
            );
        }
        ui.statusbar->showMessage(QString::number(reply->statusCode()) + " " + QString::fromUtf8(reply->status()));
        
#if 0
        QFile f("text_gzip");
        f.open(QIODevice::WriteOnly);
        auto data = co_await reply->text();
        f.write(data->c_str(), data->size());
        f.close();
#endif
        co_return Result<>();
    }
    auto doGet() -> Task<> {
        ui.imageLabel->setVisible(false);
        ui.textBrowser->setVisible(true);
        ui.listWidget->clear();
        ui.textBrowser->clear();
        ui.pushButton->setEnabled(false);
        ui.statusbar->clearMessage();
        co_await doGetTask();
        ui.pushButton->setEnabled(true);
        updateCookies();
        co_return Result<>();
    }
    auto doQueryHost() -> Task<> {
        ui.endpointsWidget->clear();
        auto v = co_await mResolver.resolve(ui.hostnameEdit->text().toUtf8().constData());
        if (!v) {
            ui.statusbar->showMessage(QString::fromUtf8(v.error().message()));
            co_return Result<>();
        }
        for (const auto &i : *v) {
            ui.endpointsWidget->addItem(QString::fromUtf8(i.toString()));
        }
        co_return Result<>();
    }
    auto onButtonClicked() -> void {
        ilias_go doGet();
    }
    auto onQueryHost() -> void {
        ilias_go doQueryHost();
    }
    auto onProxy() -> void {
        auto text = QInputDialog::getText(this, "Proxy", "Proxy", QLineEdit::Normal, "socks5h://127.0.0.1:7890");
        if (!text.isEmpty()) {
            mSession.setProxy(Url(text.toUtf8().constData()));
        }
    }
    auto updateCookies() -> void {
        auto cookies = mJar.allCookies();
        auto wi = ui.treeWidget;
        wi->clear();
        for (const auto &cookie : cookies) {
            auto item = new QTreeWidgetItem(wi);
            item->setText(0, QString::fromUtf8(cookie.domain()));
            item->setText(1, QString::fromUtf8(cookie.name()));
            item->setText(2, QString::fromUtf8(cookie.value()));
            item->setText(3, QString::fromUtf8(cookie.path()));
        }
    }

private:
    QIoContext *mCtxt;
    HttpCookieJar mJar;
    HttpSession mSession;
    Resolver mResolver;
    Ui::MainWindow ui;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QIoContext ctxt(&app);
    
    App a(&ctxt);
    a.show();
    return app.exec();
}