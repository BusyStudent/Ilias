#pragma once

#include <QNetworkRequest>
#include <QNetworkReply>
#include "object.hpp"

namespace ilias_qt {

// for impl auto val = co_await manager.get(QNetworkRequest)
inline auto toAwaitable(QNetworkReply *ptr) -> ilias::Task<Box<QNetworkReply> > {
    auto reply = Box<QNetworkReply>(ptr);
    if (reply && !reply->isFinished()) {
        struct Guard {
            QNetworkReply *reply;
            ~Guard() {
                if (reply) {
                    reply->abort();
                }
            }
        } guard{ptr};

        auto _ = (co_await QSignal(reply.get(), &QNetworkReply::finished)).value(); // The reply should't be deleted, we take ownership of it
        guard.reply = nullptr;
    }
    co_return std::move(reply);
}

inline auto reply(QNetworkReply *ptr) {
    return toAwaitable(ptr);
}

}

// Report to the global for the ADL, QNetworkReply is on the global namespace
using ilias_qt::toAwaitable;