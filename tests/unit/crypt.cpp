#include <ilias/crypt.hpp>
#include <gtest/gtest.h>
#include <charconv>
#include <string>
#include <vector>

using namespace ILIAS_NAMESPACE;
using namespace std::literals;

TEST(Crypt, Base64) {
    ASSERT_EQ(base64::encode(makeBuffer("hello world"sv)), "aGVsbG8gd29ybGQ=");
    ASSERT_EQ(base64::decode<std::string>("aGVsbG8gd29ybGQ="), "hello world");

    // Test some failure cases
    ASSERT_TRUE(base64::decode<std::string>("invalid base64").empty());
    ASSERT_TRUE(base64::decode<std::string>("aGVsbG8gd29ybGQ").empty()); // Invalid padding
    ASSERT_TRUE(base64::decode<std::string>("aGVsbG8gd29ybGQ===").empty()); // Too much padding

    // Additional success cases
    ASSERT_EQ(base64::encode(makeBuffer(""sv)), "");
    ASSERT_EQ(base64::decode<std::string>(""), "");
    ASSERT_EQ(base64::encode(makeBuffer("a"sv)), "YQ==");
    ASSERT_EQ(base64::decode<std::string>("YQ=="), "a");
    ASSERT_EQ(base64::encode(makeBuffer("ab"sv)), "YWI=");
    ASSERT_EQ(base64::decode<std::string>("YWI="), "ab");
    ASSERT_EQ(base64::encode(makeBuffer("abc"sv)), "YWJj");
    ASSERT_EQ(base64::decode<std::string>("YWJj"), "abc");
    ASSERT_EQ(base64::encode(makeBuffer("abcd"sv)), "YWJjZA==");
    ASSERT_EQ(base64::decode<std::string>("YWJjZA=="), "abcd");
}

#if defined(_WIN32)
auto parseHex(std::string_view hex) -> std::vector<std::byte> {
    ILIAS_ASSERT(hex.size() % 2 == 0); // Invalid hex
    std::vector<std::byte> vec;
    for (size_t i = 0; i < hex.size(); i += 2) {
        uint8_t val;
        auto [ptr, ec] = std::from_chars(hex.data() + i, hex.data() + i + 2, val, 16);
        ILIAS_ASSERT(ec == std::errc{}); // Invalid hex
        vec.push_back(std::byte(val));
    }
    return vec;
}

TEST(Crypt, Hash) {
    ASSERT_EQ(
        CryptoHash::hash(makeBuffer("hello world"sv), CryptoHash::Sha1), 
        parseHex("2aae6c35c94fcfb415dbe95f408b9ce91ee846ed")
    );
    ASSERT_EQ(
        CryptoHash::hash(makeBuffer("hello world"sv), CryptoHash::Sha256), 
        parseHex("b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9")
    );
    ASSERT_EQ(
        CryptoHash::hash(makeBuffer("hello world"sv), CryptoHash::Sha512), 
        parseHex("309ecc489c12d6eb4cc40f50c902f2b4d0ed77ee511a7c7a9bcd3ca86d4cd86f989dd35bc5ff499670da34255b45b0cfd830e81f605dcf7dc5542e93ae9cd76f")
    );
}
#endif

auto main(int argc, char **argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}