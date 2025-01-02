#include <gtest/gtest.h>

#include <mariadb/mysql.h>

class SQLContext {
public:
    SQLContext() : mysql(new MYSQL(), mysql_close) {
        if (mysql_init(mysql.get()) == nullptr) {
            mysql.reset();
        }
    }
    void connect() {
        mysql_options(mysql.get(), mysql_option::MYSQL_OPT_NONBLOCK, 0);
        mysql_real_connect_start(MYSQL * *ret, MYSQL * mysql, const char *host, const char *user, const char *passwd,
                                 const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag);
    }
    ~SQLContext() {}
    MYSQL *get() { return mysql.get(); }

private:
    std::unique_ptr<MYSQL, void (*)(MYSQL *)> mysql;
};

TEST(SQL, test) {
    // TODO: Implement tests for SQL
    EXPECT_EQ(1, 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}