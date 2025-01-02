/**
 * @file mysql.hpp
 * @author llhsdmd (llhsdmd@gmail.com)
 * @brief mysql I/O
 * @version 0.1
 * @date 2024-12-31
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <ilias/net/sockfd.hpp>
#include <ilias/task/when_any.hpp>
#include <mariadb/mysql.h>
#include <mariadb/mysqld_error.h>

ILIAS_NS_BEGIN

class MySql final {
public:
    MySql();
    MySql(const MySql &) = delete;
    ~MySql();

    auto connect(const std::string &host, const std::string &user, const std::string &passwd, const std::string &db)
        -> IoTask<void>;
    auto query(const std::string &sql) -> IoTask<void>;
    auto disconnect() -> void;

private:
    IoDescriptor *mDesc = nullptr;
    IoContext    *mCtxt = nullptr;
    MYSQL         mMysql;
};

inline MySql::MySql() {
    if (mysql_init(&mMysql) == nullptr) {
        ILIAS_ERROR("SQL", "mysql init failed");
    }
    mysql_options(&mMysql, MYSQL_OPT_NONBLOCK, 0);
}

inline MySql::~MySql() {
    mysql_close(&mMysql);
}

inline auto MySql::connect(const std::string &host, const std::string &user, const std::string &passwd,
                           const std::string &db) -> IoTask<void> {
    auto fd = mysql_get_socket(&mMysql);
    if (fd == MARIADB_INVALID_SOCKET) {
        ILIAS_ERROR("SQL", "mysql get socket failed");
        co_return Unexpected(Error::Unknown);
    }
    auto &&ctxt = co_await currentIoContext();
    auto   desc = ctxt.addDescriptor((fd_t)fd, IoDescriptor::Socket);
    if (!desc) {
        ILIAS_ERROR("SQL", "mysql add descriptor failed");
        co_return Unexpected(desc.error());
    }
    mDesc = desc.value();
    mCtxt = &ctxt;
    MYSQL *ret;
    auto   status =
        mysql_real_connect_start(&ret, &mMysql, host.c_str(), user.c_str(), passwd.c_str(), db.c_str(), 0, nullptr, 0);
    auto timeOut = mysql_get_timeout_value_ms(&mMysql);
    while (status) {
        if (status == MYSQL_WAIT_TIMEOUT) {
            ILIAS_ERROR("SQL", "mysql wait timeout");
            co_return Unexpected(Error::TimedOut);
        }
        auto [pret, tret] = co_await whenAny(ctxt.poll(mDesc, status), sleep(std::chrono::milliseconds(timeOut)));
        if (tret && *tret) {
            co_return Unexpected(Error::TimedOut);
        }
        if (!pret || !(*pret)) {
            ILIAS_ERROR("SQL", "mysql poll failed");
            co_return Unexpected(pret->error());
        }
        status = mysql_real_connect_cont(&ret, &mMysql, status);
    }
    if (ret == nullptr) {
        ILIAS_ERROR("SQL", "mysql connect failed");
        co_return Unexpected(Error::Unknown);
    }
    co_return {};
}

inline auto MySql::query(const std::string &sql) -> IoTask<void> {
    int  res;
    auto status  = mysql_real_query_start(&res, &mMysql, sql.c_str(), sql.size());
    auto timeOut = mysql_get_timeout_value_ms(&mMysql);
    while (status) {
        if (status == MYSQL_WAIT_TIMEOUT) {
            ILIAS_ERROR("SQL", "mysql wait timeout");
            co_return Unexpected(Error::TimedOut);
        }
        auto [pret, tret] = co_await whenAny(mCtxt->poll(mDesc, status), sleep(std::chrono::milliseconds(timeOut)));
        if (tret && *tret) {
            co_return Unexpected(Error::TimedOut);
        }
        if (!pret || !(*pret)) {
            ILIAS_ERROR("SQL", "mysql poll failed");
            co_return Unexpected(pret->error());
        }
        status = mysql_real_query_cont(&res, &mMysql, status);
    }
    if (res != 0) {
        ILIAS_ERROR("SQL", "mysql query failed");
        co_return Unexpected(Error::Unknown);
    }
    co_return {};
}

inline auto MySql::disconnect() -> void {
}

ILIAS_NS_END