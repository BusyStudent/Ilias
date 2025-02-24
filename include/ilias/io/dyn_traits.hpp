#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/method.hpp>
#include <compare>
#include <utility>

ILIAS_NS_BEGIN

namespace detail {

/**
 * @brief For traits Stream
 * 
 */
struct StreamVtbl {
    IoTask<size_t> (*read)(void *object, std::span<std::byte> buffer) = nullptr;
    IoTask<size_t> (*write)(void *object, std::span<const std::byte> buffer) = nullptr;
};

/**
 * @brief For traits StreamClient
 * 
 */
struct StreamClientVtbl : public StreamVtbl {
    IoTask<void> (*shutdown)(void *object) = nullptr;
};

/**
 * @brief Proxy for read operation
 * 
 * @tparam T 
 * @param object 
 * @param buffer 
 * @return IoTask<size_t> 
 */
template <Readable T>
inline auto readProxy(void *object, std::span<std::byte> buffer) -> IoTask<size_t> {
    return static_cast<T *>(object)->read(buffer);
}

/**
 * @brief Proxy for write operation
 * 
 * @tparam T 
 * @param object 
 * @param buffer 
 * @return IoTask<size_t> 
 */
template <Writable T>
inline auto writeProxy(void *object, std::span<const std::byte> buffer) -> IoTask<size_t> {
    return static_cast<T *>(object)->write(buffer);
}

/**
 * @brief Proxy for shutdown operation
 * 
 * @tparam T 
 * @param object 
 * @return IoTask<void> 
 */
template <Shuttable T>
inline auto shutdownProxy(void *object) -> IoTask<void> {
    return static_cast<T *>(object)->shutdown();
}

/**
 * @brief Proxy for delete the T pointer
 * 
 * @tparam T 
 * @param object 
 */
template <typename T>
inline auto deleteProxy(void *object) -> void {
    delete static_cast<T *>(object);
}

/**
 * @brief Generate the virtual table for StreamClient concept
 * 
 * @tparam T 
 * @return const StreamClientVtbl *
 */
template <StreamClient T>
inline auto makeStreamClientVtbl() {
    static constexpr StreamClientVtbl vtbl {
        StreamVtbl { readProxy<T>, writeProxy<T> },
        shutdownProxy<T>
    };
    return &vtbl;
}

/**
 * @brief Generate the virtual table for Stream concept
 * 
 * @tparam T 
 * @return const StreamVtbl *
 */
template <Stream T>
inline auto makeStreamVtbl() -> const StreamVtbl * {
    static constexpr StreamVtbl vtbl = {
        .read = readProxy<T>,
        .write = writeProxy<T>
    };
    return &vtbl;
}

} // namespace detail

/**
 * @brief The view of the Stream concept, it does not own the object
 * 
 */
class StreamView : public StreamMethod<StreamView> {
public:
    StreamView() = default;

    /**
     * @brief Construct a new empty Stream View object
     * 
     */
    StreamView(std::nullptr_t) { }

    /**
     * @brief Construct a new Stream View object
     * 
     * @tparam T Type of the Stream concept
     * @param t The reference of the Stream concept object
     */
    template <Stream T>
    StreamView(T &t) : mVtbl(detail::makeStreamVtbl<T>()), mObject(&t) { }

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) const -> IoTask<size_t> {
        return mVtbl->read(mObject, buffer);
    }

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) const -> IoTask<size_t> {
        return mVtbl->write(mObject, buffer);
    }

    /**
     * @brief Allows to compare two StreamView objects
     * 
     */
    auto operator <=>(const StreamView &other) const = default;

    /**
     * @brief Check is empty?
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mObject != nullptr;
    }
protected:
    const detail::StreamVtbl *mVtbl = nullptr;
    void                   *mObject = nullptr;
};

/**
 * @brief The view of the StreamClient concept, it does not own the object
 * 
 */
class StreamClientView : public StreamView {
public:
    StreamClientView() = default;

    /**
     * @brief Construct a new empty Stream Client View object
     * 
     */
    StreamClientView(std::nullptr_t) { }

    /**
     * @brief Construct a new Stream Client View object
     * 
     * @tparam T Type of the StreamClient concept
     * @param t The reference of the StreamClient concept object
     */
    template <StreamClient T>
    StreamClientView(T &t) {
        mVtbl = &detail::makeStreamClientVtbl<T>();
        mObject = &t;
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        return static_cast<const detail::StreamClientVtbl*>(mVtbl)->shutdown(mObject);
    }
friend class DynStreamClient;
};

/**
 * @brief To type erase the StreamClient concept, using fat pointer
 * 
 */
class DynStreamClient final : public StreamClientView {
public:
    DynStreamClient() = default;

    /**
     * @brief Construct a new empty DynStreamClient object
     * 
     */
    DynStreamClient(std::nullptr_t) { }

    /**
     * @brief Construct a new DynStreamClient object by StreamClient concept 
     * 
     * @tparam T The type of the StreamClient concept
     * @param t 
     */
    template <StreamClient T>
    DynStreamClient(T &&t) {
        mVtbl = detail::makeStreamClientVtbl<T>();
        mObject = new T(std::move(t));
        mDelete = detail::deleteProxy<T>;
    }

    /**
     * @brief Construct a new DynStreamClient object from StreamClientView
     * 
     * @param s 
     */
    explicit DynStreamClient(StreamClientView s) {
        mVtbl = s.mVtbl;
        mObject = s.mObject;
        mDelete = nullptr;
    }

    /**
     * @brief Construct a new DynStreamClient object by move
     * 
     * @param other 
     */
    DynStreamClient(DynStreamClient &&other) noexcept {
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Destroy the DynStreamClient object
     * 
     */
    ~DynStreamClient() {
        close();
    }

    /**
     * @brief Close the stream, it forcefully close the stream
     * 
     */
    auto close() noexcept -> void {
        if (mDelete) {
            mDelete(mObject);
            mDelete = nullptr;
        }
        mObject = nullptr;
        mVtbl = nullptr;
    }

    /**
     * @brief Move assignment operator 
     * 
     * @param other The other DynStreamClient object
     * @return DynStreamClient & 
     */
    auto operator =(DynStreamClient &&other) noexcept -> DynStreamClient & {
        if (this == &other) {
            return *this;
        }
        close();
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
        return *this;
    }

    /**
     * @brief Null assignment operator
     * 
     * @return DynStreamClient & 
     */
    auto operator =(std::nullptr_t) -> DynStreamClient & {
        close();
        return *this;
    }
private:
    void (*mDelete)(void *object) = nullptr; //< For deleting the object
};

/**
 * @brief To type erase the StreamClient concept, using fat pointer, alias from DynStreamClient 
 * 
 */
using IStreamClient = DynStreamClient;

#if !defined(NDEBUG)
static_assert(Stream<StreamView>, "StreamView should has Stream concept");
static_assert(StreamClient<StreamClientView>, "StreamClientView should has StreamClient concept");
static_assert(StreamClient<DynStreamClient>, "DynStreamClient should has StreamClient concept");
#endif

ILIAS_NS_END