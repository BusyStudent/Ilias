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
#include <ilias/io/fd.hpp>

ILIAS_NS_BEGIN

/**
 * @brief Concept for std::filesystem::path or another compatible path type
 * 
 * @tparam T 
 */
template <typename T>
concept PathLike = requires(T &t) {
    { t.u8string() } -> std::convertible_to<std::u8string>;
};

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
    File(IoHandle<FileDescriptor> f, std::optional<uint64_t> offset = std::nullopt) : mHandle(std::move(f)), mOffset(offset) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }

    /**
     * @brief Start read data from the file
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        auto ret = co_await mHandle.read(buffer, mOffset);
        if (ret && mOffset) {
            *mOffset += ret.value();
        }
        co_return ret;
    }

    /**
     * @brief Write data to the file
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) -> IoTask<size_t> {
        auto ret = co_await mHandle.write(buffer, mOffset);
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
     * @return IoTask<size_t> 
     */
    auto pread(MutableBuffer buffer, uint64_t offset) -> IoTask<size_t> {
        return mHandle.read(buffer, offset);
    }

    /**
     * @brief The write operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return IoTask<size_t> 
     */
    auto pwrite(Buffer buffer, uint64_t offset) -> IoTask<size_t> {
        return mHandle.write(buffer, offset);
    }

    /**
     * @brief Doing seek operation
     * 
     * @param offset 
     * @param from 
     * @return IoTask<uint64_t> 
     */
    auto seek(int64_t offset, SeekFrom from) -> IoTask<uint64_t> {
        if (!mOffset) {
            co_return Err(IoError::OperationNotSupported);
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
     * @brief Truncate the file to the given size
     * 
     * @param size 
     * @return IoTask<void> 
     */
    auto truncate(uint64_t size) -> IoTask<void> {
        if (!mOffset) { // Not Actually File in disk
            co_return Unexpected(IoError::OperationNotSupported);
        }
        co_return fd_utils::truncate(fd(), size);
    }

    /**
     * @brief Doing seek operation
     * 
     * @return IoTask<uint64_t> 
     */
    auto tell() -> IoTask<uint64_t> {
        return seek(0, SeekFrom::Current);
    }

    /**
     * @brief Get the file size
     * 
     * @return IoTask<uint64_t> 
     */
    auto size() const -> IoTask<uint64_t> {
        if (!mOffset) {
            co_return Err(IoError::OperationNotSupported);
        }
        co_return fd_utils::size(fd());
    }

    /**
     * @brief Get the file descriptor
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return fd_t(mHandle.fd());
    }

    /**
     * @brief Open the file by path and mode
     * 
     * @param path The utf-8 encoded path
     * @param mode The mode, see fopen for more details (like "r", "w", "a", "r+", "w+", "a+" etc.)
     * @return IoTask<File> 
     */
    static auto open(const char *path, std::string_view mode) -> IoTask<File> {
        auto fd = fd_utils::open(path, mode);
        if (!fd) {
            co_return Err(fd.error());
        }
        auto desc = FileDescriptor(*fd);
        auto handle = IoHandle<FileDescriptor>::make(std::move(desc), IoDescriptor::File);
        if (!handle) {
            co_return Err(handle.error());
        }

        std::optional<uint64_t> off;
        // Check if the file is a regular file (support offset)
        // Copy the lowlevel offset from the fd, and let us manage it
#if defined(_WIN32)
        if (::GetFileType(*fd) == FILE_TYPE_DISK) {
            ::LARGE_INTEGER offset { .QuadPart = 0 };
            ::LARGE_INTEGER cur;
            if (::SetFilePointerEx(*fd, offset, &cur, FILE_CURRENT)) {
                off = cur.QuadPart;
            }
        }
#else
        struct stat st;
        if (::fstat(*fd, &st) == 0 && S_ISREG(st.st_mode)) {
            auto offset = ::lseek(*fd, 0, SEEK_CUR);
            if (offset != -1) {
                off = offset;
            }
        }
#endif // defined(_WIN32)

        co_return File(std::move(*handle), off);
    }

    static auto open(const char8_t *path, std::string_view mode) -> IoTask<File> {
        return open(reinterpret_cast<const char *>(path), mode);
    }

    static auto open(const std::string &path, std::string_view mode) -> IoTask<File> {
        return open(path.c_str(), mode);
    }

    static auto open(const std::u8string &path, std::string_view mode) -> IoTask<File> {
        return open(path.c_str(), mode);
    }

    static auto open(std::string_view path, std::string_view mode) -> IoTask<File> {
        co_return co_await open(std::string(path), mode);
    }

    static auto open(std::u8string_view path, std::string_view mode) -> IoTask<File> {
        co_return co_await open(std::u8string(path), mode);
    }

    template <PathLike T>
    static auto open(const T &path, std::string_view mode) -> IoTask<File> {
        return open(path.u8string(), mode);
    }

    /**
     * @brief Check if the file stream is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }
private:
    IoHandle<FileDescriptor> mHandle;
    std::optional<uint64_t>  mOffset; //< The offset of the file stream, nullopt for unsupport seek
};

ILIAS_NS_END