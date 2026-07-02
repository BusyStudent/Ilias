#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/ext.hpp>
#include <compare>
#include <utility>
#include <memory>

ILIAS_NS_BEGIN

// Forward declarations
class ReadableView;
class WritableView;
class StreamView;
class DynReadable;
class DynWritable;
class DynStream;

// The implementation of the trait
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
    return static_cast<T *>(object)->read(buffer)  | toTask;
}

template <Writable T>
inline auto writeProxy(void *object, Buffer buffer) -> IoTask<size_t> {
    return static_cast<T *>(object)->write(buffer) | toTask;
}

template <Writable T>
inline auto shutdownProxy(void *object) -> IoTask<void> {
    return static_cast<T *>(object)->shutdown() | toTask;
}

template <Writable T>
inline auto flushProxy(void *object) -> IoTask<void> {
    return static_cast<T *>(object)->flush() | toTask;
}

template <typename T>
inline auto deleteProxy(void *object) noexcept -> void {
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

// Types check
template <typename T>
inline constexpr bool isDynType = false;

template <>
inline constexpr bool isDynType<DynReadable> = true;

template <>
inline constexpr bool isDynType<DynWritable> = true;

template <>
inline constexpr bool isDynType<DynStream> = true;

template<>
inline constexpr bool isDynType<StreamView> = true;

template <>
inline constexpr bool isDynType<ReadableView> = true;

template <>
inline constexpr bool isDynType<WritableView> = true;

template <typename T>
concept Dyn = isDynType<std::remove_cvref_t<T> >;

// No-except handler for delete (mostly delete is noexcept, and also, we can't throw in dtor)
using DeleteHandler = void (*)(void *object) noexcept;

} // namespace dyn_traits


// MARK: Stream
/**
 * @brief The view of the Stream concept, it `BORROW` object reference
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
     * @param t The pointer of the Stream concept object
     */
    template <Stream T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr StreamView(T *t) noexcept : mVtbl(&dyn_traits::streamVtbl<T>), mObject(t) {}

    /**
     * @brief Construct a new Stream View object
     * 
     * @tparam T The type of the Stream concept
     * @param t The reference of the Stream concept object
     */
    template <Stream T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr StreamView(T &t) noexcept : StreamView(&t) {}

    /**
     * @brief Read data from the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) const -> IoTask<size_t> {
        ILIAS_ASSERT(mObject, "StreamView is null");
        return mVtbl->read(mObject, buffer);
    }

    /**
     * @brief Write data to the stream
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) const -> IoTask<size_t> {
        ILIAS_ASSERT(mObject, "StreamView is null");
        return mVtbl->write(mObject, buffer);
    }

    /**
     * @brief Flush the stream 
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        ILIAS_ASSERT(mObject, "StreamView is null");
        return mVtbl->flush(mObject);
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        ILIAS_ASSERT(mObject, "StreamView is null");
        return mVtbl->shutdown(mObject);
    }

    // operator
    auto operator ==(const StreamView &other) const noexcept -> bool {
        return mObject == other.mObject;
    }

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
friend class DynReadable;
friend class DynWritable;
};


/**
 * @brief To type erase the Stream concept, using fat pointer, it `OWN` the object
 * 
 */
class DynStream final : public StreamView {
public:
    constexpr DynStream() = default;
    constexpr DynStream(std::nullptr_t) {}
    constexpr DynStream(const DynStream &) = delete;

    /**
     * @brief Construct a new DynStream object by move
     * 
     * @param other 
     */
    constexpr DynStream(DynStream &&other) noexcept {
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Construct a new DynStream object by Stream concept 
     * 
     * @tparam T The type of the Stream concept
     * @param t The unique_ptr of the Stream
     */
    template <Stream T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynStream(std::unique_ptr<T> t) {
        mVtbl = &dyn_traits::streamVtbl<T>;
        mObject = t.release();
        mDelete = &dyn_traits::deleteProxy<T>;
    }

    /**
     * @brief Construct a new DynStream object by Readable concept
     * 
     * @tparam T 
     * @param t The Stream object
     */
    template <Readable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynStream(T t) : DynStream(std::make_unique<T>(std::move(t))) {}

    /**
     * @brief Destroy the DynStream object
     * 
     */
    ~DynStream() { close(); }

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

    // Operator
    using StreamView::operator ==;

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
    dyn_traits::DeleteHandler mDelete = nullptr; //< For deleting the object
friend class DynReadable;
friend class DynWritable;
};

// MARK: ReadableView
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
     * @param t The pointer
     */
    template <Readable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr ReadableView(T *t) noexcept : mRead(&dyn_traits::readProxy<T>), mObject(t) {}

    /**
     * @brief Create a new ReadableView object by Readable concept
     * 
     * @tparam T 
     * @param t The reference
     */
    template <Readable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr ReadableView(T &t) noexcept : ReadableView(&t) {}

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
        ILIAS_ASSERT(mObject, "ReadableView is null");
        return mRead(mObject, buffer);
    }

    // operator
    auto operator ==(const ReadableView &other) const noexcept -> bool {
        return mObject == other.mObject;
    }

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
     * @param t The pointer of the Writeable
     */
    template <Writable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr WritableView(T *t) noexcept : mVtbl(&dyn_traits::writableVtbl<T>), mObject(t) {}

    /**
     * @brief Create a new WritableView object by Writeable concept
     * 
     * @tparam T 
     * @param t The reference
     */
    template <Writable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    constexpr WritableView(T &t) noexcept : WritableView(&t) {}

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
        ILIAS_ASSERT(mObject, "WritableView is null");
        return mVtbl->write(mObject, buffer);
    }

    /**
     * @brief Flush the stream 
     * 
     * @return IoTask<void> 
     */
    auto flush() const -> IoTask<void> {
        ILIAS_ASSERT(mObject, "WritableView is null");
        return mVtbl->flush(mObject);
    }

    /**
     * @brief Shutdown the stream, it gracefully close the connection
     * 
     * @return IoTask<void> 
     */
    auto shutdown() const -> IoTask<void> {
        ILIAS_ASSERT(mObject, "WritableView is null");
        return mVtbl->shutdown(mObject);
    }

    // operator
    auto operator ==(const WritableView &other) const noexcept -> bool {
        return mObject == other.mObject;
    }

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

// MARK: DynReadable
/**
 * @brief To type erase the Readable concept, using fat pointer
 * 
 */
class DynReadable final : public ReadableView {
public:
    constexpr DynReadable() = default;
    constexpr DynReadable(std::nullptr_t) {}
    constexpr DynReadable(const DynReadable &other) = delete;

    /**
     * @brief Construct a new Dyn Readable object by move
     * 
     * @param other 
     */
    constexpr DynReadable(DynReadable &&other) noexcept {
        mRead = std::exchange(other.mRead, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Construct a new Dyn Readable object
     * 
     * @tparam T 
     * @param t The unique pointer of the Readable object
     */
    template <Readable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynReadable(std::unique_ptr<T> t) {
        mRead = &dyn_traits::readProxy<T>;
        mObject = t.release();
        mDelete = &dyn_traits::deleteProxy<T>;
    }

    /**
     * @brief Construct a new Dyn Readable object
     * 
     * @tparam T The Readable object
     */
    template <Readable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynReadable(T t) : DynReadable(std::make_unique<T>(std::move(t))) {}

    /**
     * @brief Construct a new Dyn Readable object by DynStream, upcasting
     * 
     * @param stream 
     */
    DynReadable(DynStream stream) {
        auto vtbl = std::exchange(stream.mVtbl, nullptr);
        mRead = vtbl ? vtbl->read : nullptr;
        mObject = std::exchange(stream.mObject, nullptr);
        mDelete = std::exchange(stream.mDelete, nullptr);
    }

    /**
     * @brief Destroy the Dyn Readable object
     * 
     */
    ~DynReadable() { close(); }

    /**
     * @brief Close the readable object
     * 
     */
    auto close() noexcept -> void {
        if (mDelete) {
            mDelete(mObject);
            mDelete = nullptr;
        }
        mObject = nullptr;
        mRead = nullptr;
    }

    // Operator
    using ReadableView::operator =;

    /**
     * @brief Move assignment operator 
     * 
     * @param other The other DynReadable object
     * @return DynStream & 
     */
    auto operator =(DynReadable &&other) noexcept -> DynReadable & {
        if (this == &other) {
            return *this;
        }
        close();
        mRead = std::exchange(other.mRead, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
        return *this;
    }

    /**
     * @brief Null assignment operator
     * 
     * @return DynReadable & 
     */
    auto operator =(std::nullptr_t) noexcept -> DynReadable & {
        close();
        return *this;
    }
private:
    dyn_traits::DeleteHandler mDelete = nullptr; //< For deleting the object
};

// MARK: DynWritable
class DynWritable final : public WritableView {
public:
    constexpr DynWritable() = default;
    constexpr DynWritable(std::nullptr_t) {}
    constexpr DynWritable(const DynWritable &other) = delete;

    /**
     * @brief Construct a new Dyn Writable object by move
     * 
     */
    constexpr DynWritable(DynWritable &&other) noexcept {
        mVtbl = std::exchange(other.mVtbl, nullptr);
        mObject = std::exchange(other.mObject, nullptr);
        mDelete = std::exchange(other.mDelete, nullptr);
    }

    /**
     * @brief Construct a new Dyn Writable object
     * @tparam T 
     * @param t The unique pointer of the writable object
     */
    template <Writable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynWritable(std::unique_ptr<T> t) {
        mVtbl = &dyn_traits::writableVtbl<T>;
        mObject = t.release();
        mDelete = &dyn_traits::deleteProxy<T>;
    }

    /**
     * @brief Construct a new Dyn Writable object
     * 
     * @tparam T 
     * @param t The writable object
     */
    template <Writable T> requires (!dyn_traits::Dyn<T>) // Avoid recursion
    DynWritable(T t) : DynWritable(std::make_unique<T>(std::move(t))) {}

    /**
     * @brief Construct a new Dyn Writable object, upcasting the stream
     * 
     * @param stream The dyn stream
     */
    DynWritable(DynStream stream) {
        mVtbl = std::exchange(stream.mVtbl, nullptr);
        mObject = std::exchange(stream.mObject, nullptr);
        mDelete = std::exchange(stream.mDelete, nullptr);
    }

    /**
     * @brief Destroy the Dyn Writable object
     * 
     */
    ~DynWritable() { close(); }

    /**
     * @brief Close the writable object
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

    // Operator
    using WritableView::operator =;

    /**
     * @brief Move assignment operator
     * 
     */
    auto operator =(DynWritable &&other) noexcept -> DynWritable & {
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
     */
    auto operator =(std::nullptr_t) noexcept -> DynWritable & {
        close();
        return *this;
    }
private:
    dyn_traits::DeleteHandler mDelete = nullptr; //< For deleting the object
};

// For compatible with old code
using IStreamClient [[deprecated("Use DynStream instead")]] = DynStream;
using DynStreamClient [[deprecated("Use DynStream instead")]] = DynStream;

ILIAS_NS_END