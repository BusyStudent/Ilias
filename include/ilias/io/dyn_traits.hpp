#pragma once

#include <ilias/io/traits.hpp>
#include <ilias/io/method.hpp>
#include <compare>
#include <utility>

ILIAS_NS_BEGIN

namespace dyn_traits {

struct StreamVtbl {
    IoTask<size_t> (*read)(void *object, MutableBuffer buffer) = nullptr;
    IoTask<size_t> (*write)(void *object, Buffer buffer) = nullptr;
    IoTask<void>   (*shutdown)(void *object) = nullptr;
    IoTask<void>   (*flush)(void *object) = nullptr;
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

template <Stream T>
inline auto streamVtbl() -> const StreamVtbl * {
    static constexpr StreamVtbl vtbl = {
        .read     = readProxy<T>,
        .write    = writeProxy<T>,
        .shutdown = shutdownProxy<T>,
        .flush    = flushProxy<T>,
    };
    return &vtbl;
}

} // namespace dyn_traits

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
    StreamView(T &t) : mVtbl(dyn_traits::streamVtbl<T>()), mObject(&t) { }

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
    const dyn_traits::StreamVtbl *mVtbl = nullptr;
    void                         *mObject = nullptr;
friend class DynStream;
};


/**
 * @brief To type erase the Stream concept, using fat pointer
 * 
 */
class DynStream final : public StreamView {
public:
    DynStream() = default;

    /**
     * @brief Construct a new empty Stream object
     * 
     */
    DynStream(std::nullptr_t) { }

    /**
     * @brief Construct a new DynStream object by Stream concept 
     * 
     * @tparam T The type of the Stream concept
     * @param t 
     */
    template <Stream T>
    DynStream(T &&t) {
        mVtbl = dyn_traits::streamVtbl<T>();
        mObject = new T(std::move(t));
        mDelete = dyn_traits::deleteProxy<T>;
    }

    /**
     * @brief Construct a new DynStream object from StreamView
     * 
     * @param s 
     */
    explicit DynStream(StreamView s) {
        mVtbl = s.mVtbl;
        mObject = s.mObject;
        mDelete = nullptr;
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


// For compatible with old code
using IStreamClient = DynStream;
using DynStreamClient = DynStream;

ILIAS_NS_END