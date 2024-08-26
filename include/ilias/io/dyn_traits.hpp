#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/method.hpp>
#include <memory>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief For traits StreamClient
 * 
 */
struct StreamClientVtbl {
    virtual ~StreamClientVtbl() = default;

    virtual auto read(std::span<std::byte> buffer) -> Task<size_t> = 0;
    virtual auto write(std::span<const std::byte> buffer) -> Task<size_t> = 0;
    virtual auto shutdown() -> Task<void> = 0;
};

}

/**
 * @brief To type erase the StreamClient concept
 * 
 */
class IStreamClient final : public StreamMethod<IStreamClient> {
public:
    IStreamClient() = default;

    /**
     * @brief Construct a new empty IStreamClient object
     * 
     */
    IStreamClient(std::nullptr_t) { }

    /**
     * @brief Construct a new IStreamClient object by StreamClient concept 
     * 
     * @tparam T 
     * @param t 
     */
    template <StreamClient T>
    IStreamClient(T&& t) : mPtr(std::make_unique<Impl<T> >(std::forward<T>(t))) { }

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> Task<size_t> {
        return mPtr->read(buffer);
    }

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        return mPtr->write(buffer);
    }

    /**
     * @brief Close the stream, it does not gracefully close the connection
     * 
     */
    auto close() -> void {
        mPtr.reset();
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return Task<void> 
     */
    auto shutdown() -> Task<void> {
        return mPtr->shutdown();
    }

    /**
     * @brief Check is empty?
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mPtr != nullptr;
    }
private:
    template <StreamClient T>
    struct Impl final : public detail::StreamClientVtbl {
        Impl(T&& t) : value(std::forward<T>(t)) { }
        ~Impl() = default;

        auto write(std::span<const std::byte> buffer) -> Task<size_t> override {
            return value.write(buffer);
        }

        auto read(std::span<std::byte> buffer) -> Task<size_t> override {
            return value.read(buffer);
        }

        auto shutdown() -> Task<void> override {
            return value.shutdown();
        }

        T value;
    };

    std::unique_ptr<detail::StreamClientVtbl> mPtr;
};

#if !defined(NDEBUG)
static_assert(StreamClient<IStreamClient>, "IStreamClient should has StreamClient concept");
#endif

ILIAS_NS_END