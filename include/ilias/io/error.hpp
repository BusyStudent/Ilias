#pragma once
#include <ilias/defines.hpp>
#include <ilias/result.hpp>
#include <system_error>

#define ILIAS_DECLARE_ERROR(errc, category)                                     \
    inline auto _ilias_error_category_of(errc) -> const std::error_category & { \
        return category::instance();                                            \
    }                                                                           \

ILIAS_NS_BEGIN

template <typename T>
concept IntoIoError = requires(T t) {
    _ilias_error_category_of(t);  // Get the error category of T
};

using IoError = std::error_code;

template <typename T>
using IoResult = Result<T, IoError>;

template <typename T>
using IoTask = Task<IoResult<T> >;

// Interop with std::error_code
template <typename T>
inline auto make_error_code(T t) -> IoError {
    return IoError(static_cast<int>(t), _ilias_error_category_of(t));
}

ILIAS_NS_END

template <ILIAS_NAMESPACE::IntoIoError T> 
struct std::is_error_code_enum<T> : std::true_type {};