/**
 * @file method.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For basic traits helper method
 * @version 0.1
 * @date 2024-08-26
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/task/task.hpp>
#include <cstddef>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Writeable Helper Method
 * 
 * @tparam T 
 */
template <typename T>
class WritableMethod {
public:
    /**
     * @brief Write All Data to Stream
     * 
     * @param data The 
     * @return Task<size_t> 
     */
    auto writeAll(std::span<const std::byte> data) -> Task<size_t> {
        size_t written = 0;
        while (!data.empty()) {
            auto n = co_await static_cast<T*>(this)->write(data);
            if (!n && written == 0) {
                // First time write failed, return error
                co_return Unexpected(n.error());
            }
            if (!n) {
                break;
            }
            if (*n == 0) {
                break;
            }
            written += *n;
            data = data.subspan(*n);
        }
        co_return written;
    }

    auto operator <=>(const WritableMethod &rhs) const noexcept = default;
};

/**
 * @brief Helper Method for Readable
 * 
 * @tparam T 
 */
template <typename T>
class ReadableMethod {
public:
    /**
     * @brief Read All Data from Stream
     * 
     * @param data The 
     * @return Task<size_t> 
     */
    auto readAll(std::span<std::byte> data) -> Task<size_t> {
        size_t read = 0;
        while (!data.empty()) {
            auto n = co_await static_cast<T*>(this)->read(data);
            if (!n && read == 0) {
                // First time read failed, return error
                co_return Unexpected(n.error());
            }
            if (!n) {
                break;
            }
            if (*n == 0) {
                break;
            }
            read += *n;
            data = data.subspan(*n);
        }
        co_return read;
    }

    auto operator <=>(const ReadableMethod &rhs) const noexcept = default;
};

/**
 * @brief Helper for both Readable and Writable
 * 
 * @tparam T 
 */
template <typename T>
class StreamMethod : public WritableMethod<T>, public ReadableMethod<T> {
public:
    auto operator <=>(const StreamMethod &rhs) const noexcept -> std::strong_ordering = default;
};

ILIAS_NS_END