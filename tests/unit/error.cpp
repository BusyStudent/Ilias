#include <ilias/error.hpp>
#include <ilias/log.hpp>
#include <gtest/gtest.h>

using namespace ILIAS_NAMESPACE;

TEST(Error, Basic) {
    auto errc = Error::Ok;
    auto err = Error(errc);

    std::cout << err.toString() << std::endl;
    
#if defined(__cpp_lib_format)
    std::cout << std::format("{}", err) << std::endl;

    ILIAS_LOG_SET_LEVEL(ILIAS_TRACE_LEVEL);

    ILIAS_TRACE("Test", "Error is => {}", err.message());
    ILIAS_INFO("Test", "Error is => {}", err.message());
    ILIAS_WARN("Test", "Error is => {}", err.message());
    ILIAS_ERROR("Test", "Error is => {}", err.message());

    // Test Log Nothings
    ILIAS_TRACE("Test", "Nothing");

    ILIAS_LOG_ADD_BLACKLIST("Test");
    ILIAS_TRACE("test", "This should not be printed");
#endif
}


auto main(int argc, char **argv) -> int {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
