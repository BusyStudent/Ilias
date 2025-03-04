#include <ilias/crypt.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(Crypt, Base64) {
    constexpr auto what = []() {
        std::array<std::byte, 1> buf;
        base64::decodeTo("GQ==", buf);
        return buf;
    }();
    ASSERT_EQ(base64::encode(makeBuffer("hello world"sv)), "aGVsbG8gd29ybGQ=");
    ASSERT_EQ(base64::decode<std::string>("aGVsbG8gd29ybGQ="), "hello world");
}

#if defined(_WIN32)
TEST(Crypt, Hash) {
    auto data = CryptoHash::hash(makeBuffer("hello world"sv), CryptoHash::Sha1);
}
#endif

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}