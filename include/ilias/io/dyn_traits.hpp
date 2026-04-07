#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/method.hpp>
#include <compare>
#include <utility>

ILIAS_NS_BEGIN

namespace dyn_traits {

struct WritableVtbl {
    IoTask<size_t> (*write)(void *object, Buffer buffer) = nullptr;
    IoTask<void>   (*shutdown)(void *object) = nullptr;
    IoTask<void>   (*flush)(void *object) = nullptr;
};

struct StreamVtbl : public WritableVtbl {
    IoTask<size_t> (*read)(void *object, MutableBuffer buffer) = nullptr;
};

template <Readable T>
inline auto readProxy(void *object, MutableBuffer buffer) -> IoTask<size_t> {
    return static_cast<T *>(object)->read(buffer);
}

template <Writable T>
inline auto writeProxy(void *object, Buffer buffer) -> IoTask<size_t> {
    return static_cast<T *>(object)->write(buffer);
}

template <Writable T>
inline auto shutdownProxy(void *object) -> IoTask<void> {
    return static_cast<T *>(object)->shutdown();
}

template <Writable T>
inline auto flushProxy(void *object) -> IoTask<void> {
    return static_cast<T *>(object)->flush();
}

template <typename T>
inline auto deleteProxy(void *object) -> void {
    delete static_cast<T *>(object);
}

// Vtbls, C++20 compatible
template <Stream T>
inline constexpr StreamVtbl streamVtbl {
    WritableVtbl {
        .write    = writeProxy<T>,
        .shutdown = shutdownProxy<T>,
        .flush    = flushProxy<T>,
    },
    readProxy<T>,
};

template <Writable T>
inline constexpr WritableVtbl writableVtbl {
    .write    = writeProxy<T>,
    .shutdown = shutdownProxy<T>,
    .flush    = flushProxy<T>,
};

} // namespace dyn_traits

// MARK: Stream
/**
 * @brief The view of the Stream concept, it does not own the object
 * 
 */
class StreamView : public StreamExt<StreamView> {
public:
    constexpr StreamView() = default;
    constexpr StreamView(std::nullptr_t) {}
    constexpr StreamView(const StreamView &) = default;

    /**
     * @brief Construct a new Stream View object
     * 
     * @tparam T Type of the Stream concept
     * @param t The reference of the Stream concept object
     */
    template <Stream T> requires (!std::is_same_v<std::remove_cvref_t<T>, StreamView>) // Avoid recursion
    constexpr StreamView(T &t) noexcept : mVtbl(&dyn_traits::streamVtbl<T>), mObject(&t) { }

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) const -> IoTask<size_t> {
        return mVtbl->read(mObject, buffer);
    }

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) const -> IoTask<size_t> {
        return mVtbl->write(mObject, buffer);
    }

    /**
     * @brief Flush the stream 
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        return mVtbl->flush(mObject);
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        return mVtbl->shutdown(mObject);
    }

    // operator
    auto operator <=>(const StreamView &other) const noexcept = default;

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
    const dyn_traits::StreamVtbl *mVtbl = nullptr;
    void                         *mObject = nullptr;
friend class WritableView;
friend class ReadableView;
friend class DynStream;
};


/**
 * @brief To type erase the Stream concept, using fat pointer
 * 
 */
class DynStream final : public StreamView {
public:
    constexpr DynStream() = default;
    constexpr DynStream(std::nullptr_t) {}
    constexpr DynStream(const DynStream &) = delete;

    /**
     * @brief Construct a new DynStream object by Stream concept 
     * 
     * @tparam T The type of the Stream concept
     * @param t 
     */
    template <Stream T> requires (
        !std::is_same_v<std::remove_cvref_t<T>, StreamView> && 
        !std::is_same_v<std::remove_cvref_t<T>, DynStream>
    )
    DynStream(T t) {
        mVtbl = &dyn_traits::streamVtbl<T>;
        mObject = new T {std::move(t)};
        mDelete = &dyn_traits::deleteProxy<T>;
    }

    /**
     * @brief Construct a new DynStream object by move
     * 
     * @param other 
     */
    DynStream(DynStream &&other) noexcept {
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Destroy the DynStream object
     * 
     */
    ~DynStream() {
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
     * @param other The other DynStream object
     * @return DynStream & 
     */
    auto operator =(DynStream &&other) noexcept -> DynStream & {
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
     * @return DynStream & 
     */
    auto operator =(std::nullptr_t) -> DynStream & {
        close();
        return *this;
    }
private:
    void (*mDelete)(void *object) = nullptr; //< For deleting the object
};

// MARK: Readable
/**
 * @brief The view of the Readable concept, it does not own the object
 * 
 */
class ReadableView : public ReadableExt<ReadableView> {
public:
    constexpr ReadableView() = default;
    constexpr ReadableView(std::nullptr_t) {}
    constexpr ReadableView(const ReadableView &other) = default;

    /**
     * @brief Create a new ReadableView object by Readable concept
     * 
     * @tparam T 
     * @param t
     */
    template <Readable T> requires (
        !std::is_same_v<std::remove_cvref_t<T>, ReadableView> && // Let the ReadableView reference use copy 
        !std::is_same_v<std::remove_cvref_t<T>, StreamView> && // Let StreamView DynStream use upcasting
        !std::is_same_v<std::remove_cvref_t<T>, DynStream>
    )
    constexpr ReadableView(T &t) noexcept : mRead(&dyn_traits::readProxy<T>), mObject(&t) {}

    /**
     * @brief Create a new ReadableView object by StreamView, upcasting
     * 
     */
    constexpr ReadableView(StreamView s) noexcept : mRead(s ? s.mVtbl->read : nullptr), mObject(s.mObject) {}

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) const -> IoTask<size_t> {
        return mRead(mObject, buffer);
    }

    // operator
    auto operator <=>(const ReadableView &other) const noexcept = default;

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
    IoTask<size_t> (*mRead)(void *object, MutableBuffer buffer) = nullptr;
    void            *mObject = nullptr;
};

// MARK: WritableView
/**
 * @brief The view of the Writeable concept, it does not own the object
 * 
 */
class WritableView : public WritableExt<WritableView> {
public:
    constexpr WritableView() = default;
    constexpr WritableView(std::nullptr_t) {}
    constexpr WritableView(const WritableView &other) = default;

    /**
     * @brief Create a new WritableView object by Writeable concept
     * 
     * @tparam T 
     * @param t
     */
    template <Writable T> requires(
        !std::is_same_v<std::remove_cvref_t<T>, WritableView> && // Let the WritableView reference use copy 
        !std::is_same_v<std::remove_cvref_t<T>, StreamView> && // Let StreamView DynStream use upcasting
        !std::is_same_v<std::remove_cvref_t<T>, DynStream>
    )
    constexpr WritableView(T &t) noexcept : mVtbl(&dyn_traits::writableVtbl<T>), mObject(&t) {}

    /**
     * @brief Create a new WritableView object by StreamView, upcasting
     * 
     */
    constexpr WritableView(StreamView s) noexcept : mVtbl(s ? s.mVtbl : nullptr), mObject(s.mObject) {}

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) const -> IoTask<size_t> {
        return mVtbl->write(mObject, buffer);
    }

    /**
     * @brief Flush the stream 
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        return mVtbl->flush(mObject);
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        return mVtbl->shutdown(mObject);
    }

    // operator
    auto operator <=>(const WritableView &other) const noexcept = default;

    /**
     * @brief Check is empty?
     * 
     */
    explicit operator bool() const noexcept {
        return mObject != nullptr;
    }
protected:
    const dyn_traits::WritableVtbl *mVtbl = nullptr;
    void                           *mObject = nullptr;
};

// For compatible with old code
using IStreamClient [[deprecated("Use DynStream instead")]] = DynStream;
using DynStreamClient [[deprecated("Use DynStream instead")]] = DynStream;

ILIAS_NS_END