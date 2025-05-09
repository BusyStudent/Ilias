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

#if defined(__linux__)
    #include <sys/mman.h>
#endif // defined(__linux__)

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

/**
 * @brief The File class, used to represent a file stream
 * 
 */
class File final : public StreamMethod<File> {
public:
    File() = default;
    
    File(const File &) = delete;

    File(File &&other) : 
        mCtxt(std::exchange(other.mCtxt, nullptr)), 
        mDesc(std::exchange(other.mDesc, nullptr)), 
        mFd(std::exchange(other.mFd, {})), 
        mOffset(std::exchange(other.mOffset, std::nullopt)) 
    {

    }

    ~File() {
        close();
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
     * @brief Cancel all pending operations on the file
     * 
     * @return Result<void> 
     */
    auto cancel() -> Result<void> {
        return mCtxt->cancel(mDesc);
    }

    /**
     * @brief Start read data from the file
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> IoTask<size_t> {
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
     * @return IoTask<size_t> 
     */
    auto write(std::span<const std::byte> buffer) -> IoTask<size_t> {
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
     * @return IoTask<size_t> 
     */
    auto pread(std::span<std::byte> buffer, uint64_t offset) -> IoTask<size_t> {
        return mCtxt->read(mDesc, buffer, offset);
    }

    /**
     * @brief The write operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return IoTask<size_t> 
     */
    auto pwrite(std::span<const std::byte> buffer, uint64_t offset) -> IoTask<size_t> {
        return mCtxt->write(mDesc, buffer, offset);
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
     * @brief Truncate the file to the given size
     * 
     * @param size 
     * @return IoTask<void> 
     */
    auto truncate(uint64_t size) -> IoTask<void> {
        if (!mOffset) { // Not Actually File in disk
            co_return Unexpected(Error::OperationNotSupported);
        }
        co_return fd_utils::truncate(mFd, size);
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
            co_return Unexpected(Error::OperationNotSupported);
        }
        co_return fd_utils::size(mFd);
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
     * @brief Move assignment
     * 
     * @param other 
     * @return File & 
     */
    auto operator = (File &&other) -> File & {
        if (&other == this) {
            return *this;
        }
        close();
        mCtxt = std::exchange(other.mCtxt, nullptr);
        mDesc = std::exchange(other.mDesc, nullptr);
        mFd = std::exchange(other.mFd, {});
        mOffset = std::exchange(other.mOffset, std::nullopt);
        return *this;
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
            co_return Unexpected(fd.error());
        }
        auto ctxt = co_await currentIoContext();
        auto desc = ctxt.get().addDescriptor(fd.value(), IoDescriptor::File);
        if (!desc) {
            co_return Unexpected(desc.error());
        }

        File file;
        file.mDesc = desc.value();
        file.mCtxt = &ctxt.get();
        file.mFd = fd.value();

        // Check if the file is a regular file (support offset)
        // Copy the lowlevel offset from the fd, and let us manage it
#if defined(_WIN32)
        if (::GetFileType(*fd) == FILE_TYPE_DISK) {
            ::LARGE_INTEGER offset { .QuadPart = 0 };
            ::LARGE_INTEGER cur;
            if (::SetFilePointerEx(*fd, offset, &cur, FILE_CURRENT)) {
                file.mOffset = cur.QuadPart;
            }
        }
#else
        struct stat st;
        if (::fstat(*fd, &st) == 0 && S_ISREG(st.st_mode)) {
            auto offset = ::lseek(*fd, 0, SEEK_CUR);
            if (offset != -1) {
                file.mOffset = offset;
            }
        }
#endif // defined(_WIN32)

        co_return file;
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
        return mDesc != nullptr;
    }

    /**
     * @brief Cast to the file descriptor
     * 
     * @return fd_t 
     */
    explicit operator fd_t() const noexcept {
        return mFd;
    }
private:
    IoDescriptor *mDesc = nullptr;
    IoContext *mCtxt = nullptr;
    fd_t       mFd { };
    std::optional<uint64_t> mOffset; //< The offset of the file stream, nullopt for unsupport seek
};

/**
 * @brief Wrapping the mmap operation, no advance operations are supported, only for plain read / write operations
 * 
 */
class FileMapping {
public:
    enum Flags {
        ReadOnly  = 1 << 1,
        WriteOnly = 1 << 2,
        Private   = 1 << 3, // Copy on write
        ReadWrite = ReadOnly | WriteOnly
    };

    FileMapping() = default;

    FileMapping(const FileMapping &) = delete;

    FileMapping(FileMapping &&other) : mBuffer(std::exchange(other.mBuffer, {})) { }

    ~FileMapping() { unmap(); }

    /**
     * @brief Unmap the file
     * 
     */
    auto unmap() -> void {
        if (mBuffer.empty()) {
            return;
        }

#if defined(_WIN32)
        ::UnmapViewOfFile(mBuffer.data());
#else
        ::munmap(mBuffer.data(), mBuffer.size());
#endif // defined(_WIN32)
        mBuffer = {};
    }

    /**
     * @brief Get the content of the file (read-only)
     * 
     * @return std::span<const std::byte> 
     */
    auto data() const -> std::span<const std::byte> {
        return mBuffer;
    }

    /**
     * @brief Get the content of the file (read-write)
     * 
     * @return std::span<std::byte> 
     */
    auto mutableData() -> std::span<std::byte> {
        return mBuffer;
    }

    /**
     * @brief Move the file mapping
     * 
     * @param other 
     * @return FileMapping & 
     */
    auto operator =(FileMapping &&other) -> FileMapping & {
        if (this == &other) {
            return *this;
        }
        unmap();
        mBuffer = std::exchange(other.mBuffer, {});
        return *this;
    }

    /**
     * @brief Map the file into memory
     * 
     * @param fd The file descriptor of the file
     * @param offset The offset of the file
     * @param size The size of the file
     * @param flags The flags of the file
     * @return IoTask<FileMapping> 
     */
    static auto mapFrom(fd_t fd, std::optional<size_t> offset, std::optional<size_t> size, Flags flags) -> IoTask<FileMapping> {

#if defined(_WIN32)
        ::ULARGE_INTEGER offsetEx { .QuadPart = offset.value_or(0) };
        ::DWORD access = 0;
        if (flags & ReadOnly) {
            access |= FILE_MAP_READ;
        }
        if (flags & WriteOnly) {
            access |= FILE_MAP_WRITE;
        }
        if (flags & Private) {
            access |= FILE_MAP_COPY;
        }
        auto ptr = ::MapViewOfFile(fd, access, offsetEx.HighPart, offsetEx.LowPart, size.value_or(0));
        if (!ptr) {
            co_return Unexpected(SystemError::fromErrno());
        }
        // Get the size of the mapping
        ::MEMORY_BASIC_INFORMATION info { };
        if (!::VirtualQuery(ptr, &info, sizeof(info))) {
            ::UnmapViewOfFile(ptr);
            co_return Unexpected(SystemError::fromErrno());
        }
        FileMapping mapping;
        mapping.mBuffer = { reinterpret_cast<std::byte *>(ptr), static_cast<size_t>(info.RegionSize) };
        co_return mapping;
#else
        co_return Unexpected(Error::OperationNotSupported); // Not supported yet
#endif // defined(_WIN32)
        
    }

    /**
     * @brief Map the file into memory
     * 
     * @param fd 
     * @param flags The flags of the file (default to ReadOnly)
     * @return Task<FileMapping> 
     */
    static auto mapFrom(fd_t fd, Flags flags = ReadOnly) -> IoTask<FileMapping> {
        return mapFrom(fd, std::nullopt, std::nullopt, flags);
    }
private:
    std::span<std::byte> mBuffer;
};


ILIAS_NS_END