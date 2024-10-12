/**
 * @file file.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief File I/O 
 * @version 0.1
 * @date 2024-10-11
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#pragma once

#include <ilias/io/system_error.hpp>
#include <ilias/io/fd_utils.hpp>
#include <ilias/io/context.hpp>
#include <ilias/io/method.hpp>
#include <cstdio>

ILIAS_NS_BEGIN

/**
 * @brief The seek enum, taken from cstdio
 * 
 */
enum class SeekFrom : int {
    Begin   = SEEK_SET,
    Current = SEEK_CUR,
    End     = SEEK_END,
};

/**
 * @brief The File class, used to represent a file stream
 * 
 */
class File final : public StreamMethod<File> {
public:
    File() = default;
    
    File(const File &) = delete;

    File(File &&other) : mCtxt(other.mCtxt), mDesc(other.mDesc), mFd(other.mFd), mOffset(other.mOffset) {
        other.mCtxt = nullptr;
        other.mDesc = nullptr;
        other.mFd = {};
        other.mOffset = std::nullopt;
    }

    ~File() {
        close();
    }

    /**
     * @brief Open the file by path and mode
     * 
     * @param ctxt The io context
     * @param path The utf-8 encoded path
     * @param mode The mode, see fopen for more details (like "r", "w", "a", "r+", "w+", "a+" etc.)
     * @return Task<void> 
     */
    auto open(IoContext *ctxt, const char *path, std::string_view mode) -> Task<void> {
        close();

        auto fd = fd_utils::open(path, mode);
        if (!fd) {
            co_return Unexpected(fd.error());
        }
        auto desc = ctxt->addDescriptor(fd.value(), IoDescriptor::File);
        if (!desc) {
            co_return Unexpected(desc.error());
        }
        mCtxt = ctxt;
        mDesc = desc.value();
        mFd = fd.value();

        // Check if the file is a regular file (support offset)
        // Copy the lowlevel offset from the fd, and let us manage it
#if defined(_WIN32)
        if (::GetFileType(mFd) == FILE_TYPE_DISK) {
            ::LARGE_INTEGER offset { .QuadPart = 0 };
            ::LARGE_INTEGER cur;
            if (::SetFilePointerEx(mFd, offset, &cur, FILE_CURRENT)) {
                mOffset = cur.QuadPart;
            }
        }
#else
        struct stat st;
        if (::fstat(mFd, &st) == 0 && S_ISREG(st.st_mode)) {
            auto offset = ::lseek(mFd, 0, SEEK_CUR);
            if (offset != -1) {
                mOffset = offset;
            }
        }
#endif // defined(_WIN32)

        co_return {};
    }

    /**
     * @brief Open the file by path and mode
     * 
     * @param path The utf-8 encoded path
     * @param mode The mode, see fopen for more details (like "r", "w", "a", "r+", "w+", "a+" etc.)
     * @return Task<void> 
     */
    auto open(const char *path, std::string_view mode) -> Task<void> {
        return open(IoContext::currentThread(), path, mode);
    }

    /**
     * @brief Close the file stream
     * 
     */
    auto close() -> void {
        if (!mDesc) {
            return;
        }
        mCtxt->removeDescriptor(mDesc);
        fd_utils::close(mFd);
        mOffset = std::nullopt;
        mDesc = nullptr;
        mCtxt = nullptr;
        mFd = {};
    }

    /**
     * @brief Start read data from the file
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> Task<size_t> {
        auto ret = co_await mCtxt->read(mDesc, buffer, mOffset);
        if (ret && mOffset) {
            *mOffset += ret.value();
        }
        co_return ret;
    }

    /**
     * @brief Write data to the file
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> Task<size_t> {
        auto ret = co_await mCtxt->write(mDesc, buffer, mOffset);
        if (ret && mOffset) {
            *mOffset += ret.value();
        }
        co_return ret;
    }

    /**
     * @brief The read operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return Task<size_t> 
     */
    auto pread(std::span<std::byte> buffer, size_t offset) -> Task<size_t> {
        return mCtxt->read(mDesc, buffer, offset);
    }

    /**
     * @brief The write operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return Task<size_t> 
     */
    auto pwrite(std::span<const std::byte> buffer, size_t offset) -> Task<size_t> {
        return mCtxt->write(mDesc, buffer, offset);
    }

    /**
     * @brief Doing seek operation
     * 
     * @param offset 
     * @param from 
     * @return Task<uint64_t> 
     */
    auto seek(int64_t offset, SeekFrom from) -> Task<uint64_t> {
        if (!mOffset) {
            co_return Unexpected(Error::OperationNotSupported);
        }
        int64_t now = *mOffset;
        switch (from) {
            case SeekFrom::Begin: now = offset; break;
            case SeekFrom::Current: now += offset; break;
            case SeekFrom::End: now = (co_await size()).value() + offset; break;
        }
        mOffset = std::min<int64_t>(0, now);
        co_return *mOffset;
    }

    /**
     * @brief Doing seek operation
     * 
     * @return Task<uint64_t> 
     */
    auto tell() -> Task<uint64_t> {
        return seek(0, SeekFrom::Current);
    }

    /**
     * @brief Get the file size
     * 
     * @return Task<uint64_t> 
     */
    auto size() const -> Task<uint64_t> {
        if (!mOffset) {
            co_return Unexpected(Error::OperationNotSupported);
        }

#if defined(_WIN32)
        ::LARGE_INTEGER size;
        if (::GetFileSizeEx(mFd, &size)) {
            co_return size.QuadPart;
        }
#else
        struct stat st;
        if (::fstat(mFd, &st) == 0 && S_ISREG(st.st_mode)) {
            co_return st.st_size;
        }
#endif // defined(_WIN32)

        co_return Unexpected(SystemError::fromErrno());
    }

    /**
     * @brief Get the file descriptor
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return mFd;
    }

    /**
     * @brief Check if the file stream is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return mDesc != nullptr;
    }
private:
    IoDescriptor *mDesc = nullptr;
    IoContext *mCtxt = nullptr;
    fd_t       mFd { };
    std::optional<size_t> mOffset; //< The offset of the file stream, nullopt for unsupport seek
};

ILIAS_NS_END