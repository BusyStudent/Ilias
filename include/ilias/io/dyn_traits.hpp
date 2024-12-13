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
 * @brief Generate the virtual table for StreamClient concept
 * 
 * @tparam T 
 * @return const StreamClientVtbl *
 */
template <StreamClient T>
inline auto makeStreamClientVtbl() {
    static constexpr StreamClientVtbl vtbl {
        StreamVtbl {
            [](void *object, std::span<std::byte> buffer) {
                return static_cast<T *>(object)->read(buffer);
            },
            [](void *object, std::span<const std::byte> buffer) {
                return static_cast<T *>(object)->write(buffer);
            }
        },
        [](void *object) {
            return static_cast<T *>(object)->shutdown();
        }
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
        .read = [](void *object, std::span<std::byte> buffer) {
            return static_cast<T *>(object)->read(buffer);
        },
        .write = [](void *object, std::span<const std::byte> buffer) {
            return static_cast<T *>(object)->write(buffer);
        }
    };

    return &vtbl;
}

}

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
friend class IStreamClient;
};

/**
 * @brief To type erase the StreamClient concept, using fat pointer
 * 
 */
class IStreamClient final : public StreamClientView {
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
     * @tparam T The type of the StreamClient concept
     * @param t 
     */
    template <StreamClient T>
    IStreamClient(T &&t) {
        mVtbl = detail::makeStreamClientVtbl<T>();
        mObject = new T(std::move(t));
        mDelete = [](void *object) {
            delete static_cast<T *>(object);
        };
    }

    /**
     * @brief Construct a new IStreamClient object from StreamClientView
     * 
     * @param s 
     */
    explicit IStreamClient(StreamClientView s) {
        mVtbl = s.mVtbl;
        mObject = s.mObject;
        mDelete = nullptr;
    }

    /**
     * @brief Construct a new IStreamClient object by move
     * 
     * @param other 
     */
    IStreamClient(IStreamClient &&other) noexcept {
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Destroy the IStreamClient object
     * 
     */
    ~IStreamClient() {
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
     * @param other The other IStreamClient object
     * @return IStreamClient & 
     */
    auto operator =(IStreamClient &&other) noexcept -> IStreamClient & {
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
     * @return IStreamClient & 
     */
    auto operator =(std::nullptr_t) -> IStreamClient & {
        close();
        return *this;
    }
private:
    void (*mDelete)(void *object) = nullptr; //< For deleting the object
};

#if !defined(NDEBUG)
static_assert(Stream<StreamView>, "StreamView should has Stream concept");
static_assert(StreamClient<StreamClientView>, "StreamClientView should has StreamClient concept");
static_assert(StreamClient<IStreamClient>, "IStreamClient should has StreamClient concept");
#endif

ILIAS_NS_END