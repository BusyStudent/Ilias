#pragma once

#include <ilias/ilias.hpp>
#include <cstring>
#include <memory>
#include <cstdio>
#include <array>
#include <span>

ILIAS_NS_BEGIN

/**
 * @brief Implmentation of Ring on a writable memory view
 * 
 * @tparam T 
 */
template <typename T>
class RingImpl {
public:
    /**
     * @brief Construct a new empty Ring Impl object
     * 
     */
    RingImpl() = default;

    /**
     * @brief Construct a new Ring Impl object by forward the args
     * 
     * @tparam Args 
     * @param args 
     */
    template <typename ...Args>
    RingImpl(Args &&...args) : mBuffer(std::forward<Args>(args)...) { }

    /**
     * @brief Construct a new Ring Impl object by copy
     * 
     */
    RingImpl(const RingImpl &) = default;

    /**
     * @brief Construct a new Ring Impl object by move
     * 
     */
    RingImpl(RingImpl &&) = default;

    /**
     * @brief Destroy the Ring Impl object
     * 
     */
    ~RingImpl() = default;

    using value_type = typename T::value_type;
    static_assert(std::is_trivial_v<value_type>, "It is binary data, must be trivial type !!!");

    /**
     * @brief Check this ring is empty?
     * 
     * @return true 
     * @return false 
     */
    auto empty() const noexcept -> bool;

    /**
     * @brief Check this ring is full?
     * 
     * @return true 
     * @return false 
     */
    auto full() const noexcept -> bool;

    /**
     * @brief Get the capacity of the ring
     * 
     * @return size_t 
     */
    auto capacity() const noexcept -> size_t;

    /**
     * @brief Get the num of data in the ring
     * 
     * @return size_t 
     */
    auto size() const noexcept -> size_t;

    /**
     * @brief Clear the tail and head of the ring
     * 
     */
    auto clear() noexcept -> void;

    /**
     * @brief Get the buffer the ring based on
     * 
     * @return const T& 
     */
    auto buffer() const noexcept -> const T &;

    /**
     * @brief Get the buffer the ring based on
     * 
     * @return T& 
     */
    auto buffer() noexcept -> T &;

    /**
     * @brief Try to push a value into it
     * 
     * @param value 
     * @return true 
     * @return false 
     */
    auto push(value_type value) noexcept -> bool;

    /**
     * @brief Push a range of data inside the ring
     * 
     * @param values 
     * @return size_t The num of data pushed
     */
    auto push(std::span<const value_type> values) noexcept -> size_t;

    /**
     * @brief Push a range of data inside the ring, traditional way
     * 
     * @param mem 
     * @param n 
     * @return size_t The num of data pushed
     */
    auto push(const value_type *mem, size_t n) noexcept -> size_t;

    /**
     * @brief Try to pop a value from it
     * 
     * @param value 
     * @return true 
     * @return false 
     */
    auto pop(value_type &value) noexcept -> bool;

    /**
     * @brief Pop a range of data from it
     * 
     * @param values 
     * @return size_t The num of data popped
     */
    auto pop(std::span<value_type> values) noexcept -> size_t;

    /**
     * @brief Pop a range of data from it, traditional way
     * 
     * @param mem 
     * @param n 
     * @return size_t The num of data popped
     */
    auto pop(value_type *mem, size_t n) noexcept -> size_t;    

    /**
     * @brief Get the continuous memory Push Buffer
     * must call endPushBuf to finish push.
     * this function will return a tuple, first is the pointer of buf, 
     * second is the size of contiguous memory size in the buf.
     * @warning
     *  ! Must be call endPush to finish push buf operator.
     *  ! Do not manipulate memory that exceeds size, 
     *    it will cause undefined behavior.
     * 
     * @return std::span<value_type>
     */
    auto getPushBuffer() -> std::span<value_type>;

    /**
     * @brief flush buf to finish push buf operator
     * 
     * while offset greater than remainder size, return false.
     * 
     * @param offset the size push by push buf.
     * @return true 
     * @return false 
     */
    auto endPush(size_t size) noexcept -> bool;

    /**
     * @brief Get the continuous memory of Pop Buf
     * must call endPopBuf to finish pop.
     * this function will return a tuple, first is the pointer of buf, 
     * second is the size of contiguous memory size in the buf.
     * @warning
     *  ! Must be call endPushBuf to finish push buf operator.
     *  ! Do not manipulate memory that exceeds size, 
     *    it will cause undefined behavior.
     * 
     * @return std::span<const value_type>
     */
    auto getPopBuffer() -> std::span<const value_type>;

    /**
     * @brief flush buf to finish pop buf operator
     * 
     * @param offset the size pop by pop buf.
     * @return true 
     * @return false 
     */
    auto endPop(size_t offset) noexcept -> bool;

    /**
     * @brief Check the ring's data is continuous?
     * 
     * @return true 
     * @return false 
     */
    auto continuous() const noexcept -> bool;

    /**
     * @brief Move the inside data into continuous memory
     * 
     */
    auto defragment() noexcept -> void;

    /**
     * @brief Rebuild the ring with new capacity
     * 
     * @param newCapacity The old Capacity of the buffer
     * @param rebuildCallback The callback to resize the buffer inside
     * 
     * @note If the new capacity is smaller than the current data's size, the front of the data will be discarded
     */
    template <typename RebuildCallback>
    auto rebuild(size_t newCapacity, RebuildCallback &&cb) noexcept -> void;
private:
    auto printData(int offset, char c) -> void;
    auto printIndex() -> void;

    size_t mSize = 0;
    size_t mHead = 0; //< The position of the first data when pop
    size_t mTail = 0; //< The position after the last data
    T mBuffer { };
};


/**
 * @brief RingBuffer with a fixed size
 * 
 * @tparam N the num of the elements
 * @tparam T the type of the elements, default in std::byte
 */
template <size_t N, typename T = std::byte>
class RingBuffer final {
public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer &) = delete;
    ~RingBuffer() = default;

    auto empty() const noexcept -> bool { return mData.empty(); }
    auto full() const noexcept -> bool { return mData.full(); }
    auto capacity() const noexcept -> size_t { return mData.capacity(); }
    auto continuous() const noexcept -> bool { return mData.continuous(); }
    auto size() const noexcept -> size_t { return mData.size(); }
    auto clear() noexcept -> void { mData.clear(); }
    
    auto push(T value) noexcept -> bool { return mData.push(value); }
    auto push(std::span<const T> values) noexcept -> size_t { return mData.push(values); }
    auto push(const T *values, size_t n) noexcept -> size_t { return mData.push(values, n); }

    auto pop(T &value) noexcept -> bool { return mData.pop(value); }
    auto pop(std::span<T> values) noexcept -> size_t { return mData.pop(values); }
    auto pop(T *values, size_t n) noexcept -> size_t { return mData.pop(values, n); }

    auto getPushBuffer() -> std::span<T> { return mData.getPushBuffer(); }
    auto endPush(size_t offset) noexcept -> bool { return mData.endPush(offset); }

    auto getPopBuffer() -> std::span<const T> { return mData.getPopBuffer(); }
    auto endPop(size_t offset) noexcept -> bool { return mData.endPop(offset); }

    auto defragment() noexcept -> void { mData.defragment(); }
private:
    RingImpl<std::array<T, N> > mData;
};

/**
 * @brief A Ring working on a resizable vector
 * 
 * @tparam T 
 */
template <typename T = std::byte>
class RingVector {
public:
    RingVector();
    auto resize(size_t newCapicity) -> void;
private:
    RingImpl<std::span<T> > mData;
};


// --- Impl for Ring
template <typename T>
inline auto RingImpl<T>::empty() const noexcept -> bool {
    return mSize == 0;
}

template <typename T>
inline auto RingImpl<T>::full() const noexcept -> bool {
    return mSize == capacity();
}

template <typename T>
inline auto RingImpl<T>::capacity() const noexcept -> size_t {
    return mBuffer.size();
}

template <typename T>
inline auto RingImpl<T>::size() const noexcept -> size_t {
    return mSize;
}

template <typename T>
inline auto RingImpl<T>::clear() noexcept -> void {
    mHead = 0;
    mTail = 0;
    mSize = 0;
}

template <typename T>
inline auto RingImpl<T>::buffer() const noexcept -> const T & {
    return mBuffer;
}

template <typename T>
inline auto RingImpl<T>::buffer() noexcept -> T & {
    return mBuffer;
}

template <typename T>
inline auto RingImpl<T>::push(value_type value) noexcept -> bool {
    if (full()) {
        return false;
    }
    mBuffer[mTail] = value;
    mTail = (mTail + 1) % capacity();
    mSize++;
    return true;
}

template <typename T>
inline auto RingImpl<T>::push(const value_type *buffer, size_t n) noexcept -> size_t {
    int64_t copySize = (std::min)(n, capacity() - mSize);
    int64_t cp1 = mTail + copySize > capacity() ? capacity() - mTail : copySize;
    ::memcpy(mBuffer.data() + mTail, buffer, cp1 * sizeof(value_type));
    ::memcpy(mBuffer.data(), buffer + cp1, (copySize - cp1) * sizeof(value_type));

#ifdef ILIAS_RING_DEBUG
    printData(copySize, '^');
#endif

    mTail = (copySize == cp1) ? mTail + copySize : mTail + copySize - capacity();
    mTail = (mTail == capacity()) ? 0 : mTail;
    mSize += copySize;

#ifdef ILIAS_RING_DEBUG
    printIndex();
#endif

    return copySize;
}

template <typename T>
inline auto RingImpl<T>::push(std::span<const value_type> values) noexcept -> size_t {
    return push(values.data(), values.size());
}

template <typename T>
inline auto RingImpl<T>::pop(value_type &value) noexcept -> bool {
    if (empty()) {
        return false;
    }
    value = mBuffer[mHead];
    mHead = (mHead + 1) % capacity();
    mSize--;
    return true;
}

template <typename T>
inline auto RingImpl<T>::pop(value_type *buffer, size_t n) noexcept -> size_t {
    int64_t copySize = (std::min)(n, mSize);
    int64_t cp1 = mHead + copySize > capacity() ? capacity() - mHead : copySize;
    ::memcpy(buffer, mBuffer.data() + mHead, cp1 * sizeof(value_type));
    ::memcpy((uint8_t *)buffer + cp1, mBuffer.data(), (copySize - cp1) * sizeof(value_type));

#ifdef ILIAS_RING_DEBUG
    printData(copySize, '#');
#endif

    if (copySize == mSize) {
        mHead = mTail = 0;
        mSize = 0;
    }
    else {
        mHead = cp1 == copySize ? mHead + copySize : mHead + copySize - capacity();
        mHead = mHead == capacity() ? 0 : mHead;
        mSize -= copySize;
    }

#ifdef ILIAS_RING_DEBUG
    printIndex();
#endif

    return copySize;
}

template <typename T>
inline auto RingImpl<T>::pop(std::span<value_type> values) noexcept -> size_t {
    return pop(values.data(), values.size());
}

// startPush endPush etc...
template <typename T>
inline auto RingImpl<T>::getPushBuffer() -> std::span<value_type> {
    if (mTail >= mHead) {
        return std::span(mBuffer.data() + mTail, capacity() - mTail);
    } 
    else {
        return std::span(mBuffer.data(), mHead - mTail);
    }
}

template <typename T>
inline auto RingImpl<T>::getPopBuffer() -> std::span<const value_type> {
    if (mTail > mHead) {
        return std::span(mBuffer.data() + mHead, mTail - mHead);
    } 
    else {
        return std::span(mBuffer.data() + mHead, capacity() - mHead);
    }
}

template <typename T>
inline auto RingImpl<T>::endPop(size_t offset) noexcept -> bool {
#ifdef ILIAS_RING_DEBUG
    printData(offset, '#');
#endif
    if (offset > mSize) {
        return false;
    }
    if (mTail > mHead) {
        mHead += offset;
    }
    else {
        mHead = offset + mHead < capacity() ? mHead + offset : mHead + offset - capacity();
    }
    mSize -= offset;

#ifdef ILIAS_RING_DEBUG
    printIndex();
#endif
    return true;
}

template <typename T>
inline auto RingImpl<T>::endPush(size_t offset) noexcept -> bool {
#ifdef ILIAS_RING_DEBUG
    printData(offset, '^');
#endif
    if (offset + mSize > capacity()) {
        return false;
    }
    if (mTail < mHead) {
        mTail += offset;
    }
    else {
        mTail = offset + mTail < capacity() ? mTail + offset : mTail + offset - capacity();
    }
    mSize += offset;

#ifdef ILIAS_RING_DEBUG
    printIndex();
#endif
    return true;
}


// Rebuild and the defragment
// TODO:
template<typename T>
inline auto RingImpl<T>::continuous() const noexcept -> bool {
    if (empty()) return true;
    // DATA  | Tail | Empty ----- Space | Head | DATA
    if (full()) {
        return mHead == capacity(); // DATA ...... | Head Tail, it is still continuous
    }
    return mTail > mHead;
}

// template <typename T>
// template <typename RebuildCallback>
// inline auto RingImpl<T>::rebuild(size_t oldCapacity, RebuildCallback &&cb) noexcept -> void {
//     if (oldCapacity == capacity()) {
//         return;
//     }
//     ::abort();
// }


// Debug
template <typename T>
auto RingImpl<T>::printData(int offset, char c) -> void {
    for (int64_t i = 0; i < capacity(); ++i) {
        ::fprintf(stderr, "%03d ", mBuffer[i]);
    }
    ::fprintf(stderr, "\n");
    int cp1 = 0;
    if (mTail <= mHead && offset > mTail) {
        cp1 = offset - mTail;
    }
    else {
        cp1 = offset;
    }
    for (int64_t i = 0; i < capacity(); ++i) {
        if (cp1 == offset) {
            char str[5];
            ::sprintf(str, "%c%c%c ", c, c, c);
            ::fprintf(stderr, "%s", (i >= mTail && i < mTail + offset) ? str : "    ");
        }
        else {
            char str[5];
            ::sprintf(str, "%c%c%c ", c, c, c);
            ::fprintf(stderr, "%s", (i < (mTail + offset) % capacity() || i >= mTail) ? str : "    ");
        }
    }
    ::printf("\n");
}

template <typename T>
auto RingImpl<T>::printIndex() -> void {
    for (int64_t i = 0; i < capacity(); ++i) {
        if (i == mHead && i == mTail) {
            ::fprintf(stderr, ">HT<");
        }
        else if (i == mHead) {
            ::fprintf(stderr, ">H<<");
        }
        else if (i == mTail) {
            ::fprintf(stderr, ">T<<");
        }
        else {
            ::fprintf(stderr, "    ");
        }
    }
    ::fprintf(stderr, "\n");
}

ILIAS_NS_END