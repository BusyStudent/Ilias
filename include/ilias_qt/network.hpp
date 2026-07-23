#pragma once

#include <QNetworkRequest>
#include <QNetworkReply>
#include "object.hpp"

namespace ilias_qt {

inline auto reply(Box<QNetworkReply> reply) -> ilias::Task<Box<QNetworkReply> > {
    if (reply && !reply->isFinished()) {
        struct Guard {
            QNetworkReply *reply;
            ~Guard() {
                if (reply) {
                    reply->abort();
                }
            }
        } guard{reply.get()};

        auto _ = (co_await QSignal(reply.get(), &QNetworkReply::finished)).value(); // The reply should't be deleted, we take ownership of it
        guard.reply = nullptr;
    }
    co_return std::move(reply);
}

inline auto reply(QNetworkReply *ptr) {
    return reply(Box<QNetworkReply>{ptr});
}

}

// for impl auto val = co_await manager.get(QNetworkRequest)
template <>
struct ilias::runtime::IntoRawAwaitableTrait<QNetworkReply *> {
    static auto into(QNetworkReply *ptr) {
        return ilias_qt::reply(ptr);
    }
};