#include <ilias/error.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Error, Basic) {
    auto errc = Error::Ok;
    auto err = Error(errc);

    std::cout << err.toString() << std::endl;
}


auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
