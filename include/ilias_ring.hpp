#pragma once

#include "ilias.hpp"

ILIAS_NS_BEGIN


/**
 * @brief Ring Buffer
 * 
 */
template <size_t N>
class RingBuffer {
public:
    RingBuffer() = default;
    RingBuffer(const RingBuffer &) = default;
    RingBuffer(RingBuffer &&) = default;
    ~RingBuffer() = default;

    bool empty() const noexcept {
        return mSize == 0;
    }
    bool full() const noexcept {
        return mSize == mCapicity;
    }
    void clear() noexcept {
        mSize = 0;
        mHead = 0;
        mTail = 0;
    }
    size_t size() const noexcept {
        return mSize;
    }
    size_t capacity() const noexcept { 
        return mCapicity; 
    }
    bool push(uint8_t value) noexcept {
        if (full()) {
            return 0;
        }
        mBuffer[mTail] = (value);
        mTail          = (mTail + 1) % mCapicity;
        ++mSize;
        return true;
    }
    bool pop(uint8_t &value) noexcept {
        if (empty()) {
            return 0;
        }
        value = (mBuffer[mHead]);
        mHead = (mHead + 1) % mCapicity;
        --mSize;
        return true;
    }
    size_t push(const void *buffer, size_t n) noexcept {
        int64_t copySize = (std::min)(n, mCapicity - mSize);
        int64_t cp1 = mTail + copySize > mCapicity ? mCapicity - mTail : copySize;
        ::memcpy(mBuffer + mTail, buffer, cp1);
        ::memcpy(mBuffer, (uint8_t *)buffer + cp1, copySize - cp1);

#ifdef ILIAS_RING_DEBUG
        for (int64_t i = 0; i < mCapicity; ++i) {
            ::printf("%03d ", mBuffer[i]);
        }
        ::printf("\n");
        for (int64_t i = 0; i < mCapicity; ++i) {
            if (cp1 == copySize) {
                ::printf("%s", (i >= mTail && i < mTail + copySize) ? "^^^ " : "    ");
            }
            else {
                ::printf("%s", (i < (mTail + copySize) % mCapicity || i >= mTail) ? "^^^ " : "    ");
            }
        }
        ::printf("\n");
#endif

        mTail = (copySize == cp1) ? mTail + copySize : mTail + copySize - mCapicity;
        mTail = (mTail == mCapicity) ? 0 : mTail;
        mSize += copySize;

#ifdef ILIAS_RING_DEBUG
        for (int64_t i = 0; i < mCapicity; ++i) {
            if (i == mHead && i == mTail) {
                ::printf(">HT<");
            }
            else if (i == mHead) {
                ::printf(">H<<");
            }
            else if (i == mTail) {
                ::printf(">T<<");
            }
            else {
                ::printf("    ");
            }
        }
        ::printf("\n");
#endif

        return copySize;
    }
    size_t pop(void *buffer, size_t n) noexcept {
        int64_t copySize = (std::min)(n, mSize);
        int64_t cp1 = mHead + copySize > mCapicity ? mCapicity - mHead : copySize;
        ::memcpy(buffer, mBuffer + mHead, cp1);
        ::memcpy((uint8_t *)buffer + cp1, mBuffer, copySize - cp1);

#ifdef ILIAS_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            ::printf("%03d ", mBuffer[i]);
        }
        ::printf("\n");
        for (int i = 0; i < mCapicity; ++i) {
            if (cp1 == copySize) {
                ::printf("%s", (i >= mHead && i < mHead + copySize) ? "### " : "    ");
            }
            else {
                ::printf("%s", (i < (mHead + copySize) % mCapicity || i >= mHead) ? "### " : "    ");
            }
        }
        ::printf("\n");
#endif

        if (copySize == mSize) {
            mHead = mTail = 0;
            mSize = 0;
        }
        else {
            mHead = cp1 == copySize ? mHead + copySize : mHead + copySize - mCapicity;
            mHead = mHead == mCapicity ? 0 : mHead;
            mSize -= copySize;
        }

#ifdef ILIAS_RING_DEBUG
        for (int i = 0; i < mCapicity; ++i) {
            if (i == mHead && i == mTail) {
                ::printf(">HT<");
            }
            else if (i == mHead) {
                ::printf(">>H<");
            }
            else if (i == mTail) {
                ::printf(">>T<");
            }
            else {
                ::printf("    ");
            }
        }
        ::printf("\n");
        ::printf("mSize :%zu\n", mSize);
#endif
        return copySize;
    }
private:
    size_t mCapicity = N;
    size_t mSize = 0;
    size_t mHead = 0;
    size_t mTail = 0;
    uint8_t mBuffer[N] = {0};
};



ILIAS_NS_END