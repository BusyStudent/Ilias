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
#include <ilias/fs/path.hpp>
#include <filesystem>
#include <concepts>

#if defined(_WIN32) 
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32

ILIAS_NS_BEGIN

class File;

/**
 * @brief The option used to open a file
 * 
 */
class OpenOptions {
public:
    /**
     * @brief Create an new OpenOptions object, default to nothing
     * 
     */
    constexpr OpenOptions() = default;
    constexpr OpenOptions(const OpenOptions &) = default;
    constexpr OpenOptions(OpenOptions &&) = default;

    /**
     * @brief Set the read access option
     * 
     * @param on (default on true)
     * @return OpenOptions & 
     */
    constexpr auto read(bool on = true) -> OpenOptions & {
        mRead = on;
        return *this;
    }

    /**
     * @brief Set the write option
     * 
     * @param on (default on true)
     * @return OpenOptions & 
     */
    constexpr auto write(bool on = true) -> OpenOptions & {
        mWrite = on;
        return *this;
    }

    /**
     * @brief Set the append option, it will disable seek and always append to the file
     * 
     * @param on 
     * @return OpenOptions & 
     */
    constexpr auto append(bool on = true) -> OpenOptions & {
        mAppend = on;
        return *this;
    }

    /**
     * @brief Truncate the file if it exists
     * 
     * @param on 
     * @return OpenOptions& 
     */
    constexpr auto truncate(bool on = true) -> OpenOptions & {
        mTruncate = on;
        return *this;
    }

    /**
     * @brief Create the file if it doesn't exist
     * @note This option require the write option to be set
     * 
     * @param on (default on true)
     * @return OpenOptions& 
     */
    constexpr auto create(bool on = true) -> OpenOptions & {
        mCreate = on;
        return *this;
    }

    /**
     * @brief Create the file if it doesn't exist, fail if it does
     * @note This option require the write option to be set
     * 
     * @param on 
     * @return OpenOptions& 
     */
    constexpr auto createNew(bool on = true) -> OpenOptions & {
        mCreateNew = on;
        return *this;
    }

    /**
     * @brief Open the file by the given options
     * 
     * @note The char will be treated as utf-8 encoded
     * @param path The path of the file
     * @return IoTask<File> 
     */
    template <fs::IntoPath T>
    auto open(const T &path) const -> IoTask<File>;

    // Operators
    auto operator <=>(const OpenOptions &) const = default;
    auto operator =(const OpenOptions &) -> OpenOptions & = default;
    auto operator =(OpenOptions &&) -> OpenOptions & = default;

    // Some predefined options
    // "r"
    static const OpenOptions ReadOnly;
    // "w"
    static const OpenOptions WriteOnly;
    // "r+"
    static const OpenOptions ReadWrite;
private:
    static auto doOpen(OpenOptions self, fs::Path path) -> IoTask<File>;

    // Read Mode, default to read-only
    bool mRead  = false;
    bool mWrite = false;
    bool mAppend = false;  // Append to the file if it exists
    bool mTruncate = false; // Truncate the file if it exists
    bool mCreate = false;  // Create the file if it doesn't exist
    bool mCreateNew = false; // Create the file if it doesn't exist, fail if it does

#if defined(_WIN32)
    ::DWORD mShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
#else
    ::mode_t mMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // The mode used to create the file
#endif // defined(_WIN32)
};

/**
 * @brief The File class, used to represent a file stream
 * 
 */
class File final : public StreamExt<File> {
public:
    File() = default;
    File(File &&) = default;
    File(IoHandle<FileDescriptor> f, std::optional<uint64_t> offset = std::nullopt) : mHandle(std::move(f)), mOffset(offset) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }

    // Readable
    /**
     * @brief Start read data from the file
     * @warning Don't use this function cocurrently, it will cause data race condition, use pread instead
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto read(MutableBuffer buffer) -> IoTask<size_t> {
        auto res = co_await mHandle.read(buffer, mOffset);
#if defined(_WIN32)
        if (res == Err(SystemError(ERROR_HANDLE_EOF))) { // EOF
            res = 0;
        }
#endif // _WIN32
        if (res && mOffset) {
            *mOffset += *res;
        }
        co_return res;
    }

    // Writable
    /**
     * @brief Write data to the file
     * @warning Don't use this function cocurrently, it will cause data race condition, use pwrite instead
     * 
     * @param buffer 
     * @return IoTask<size_t> 
     */
    auto write(Buffer buffer) -> IoTask<size_t> {
        auto res = co_await mHandle.write(buffer, mOffset);
        if (res && mOffset) {
            *mOffset += *res;
        }
        co_return res;
    }

    // no-op
    auto shutdown() -> IoTask<void> {
        co_return {};
    }
    
    /**
     * @brief Flush the file stream
     * 
     * @return IoTask<void> 
     */
    auto flush() -> IoTask<void> {
        co_return co_await ilias::blocking([&]() -> IoResult<void> {
#if defined(_WIN32)
            if (::FlushFileBuffers(fd())) {
                return {};
            }
#else
            if (::fdatasync(fd()) != 0) {
                return {};
            }
#endif // defined(_WIN32)
            return Err(SystemError::fromErrno());
        });
    }

    /**
     * @brief The read operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return IoTask<size_t> 
     */
    auto pread(MutableBuffer buffer, uint64_t offset) -> IoTask<size_t> {
        if (!mOffset) {
            co_return Err(IoError::OperationNotSupported);
        }
        auto res = co_await mHandle.read(buffer, offset);
#if defined(_WIN32)
        if (res == Err(SystemError(ERROR_HANDLE_EOF))) { // EOF
            res = 0;
        }
#endif // _WIN32
        co_return res;
    }

    /**
     * @brief The write operation with offset, this operation will not change the offset of the file stream
     * 
     * @param buffer 
     * @param offset 
     * @return IoTask<size_t> 
     */
    auto pwrite(Buffer buffer, uint64_t offset) -> IoTask<size_t> {
        if (!mOffset) {
            co_return Err(IoError::OperationNotSupported);
        }
        co_return co_await mHandle.write(buffer, offset);
    }

    // Seekable
    /**
     * @brief Doing seek operation
     * 
     * @param offset 
     * @param origin 
     * @return IoTask<uint64_t> 
     */
    auto seek(int64_t offset, SeekOrigin origin) -> IoTask<uint64_t> {
        if (!mOffset) {
            co_return Err(IoError::OperationNotSupported);
        }
        int64_t now = *mOffset;
        switch (origin) {
            case SeekOrigin::Begin: now = offset; break;
            case SeekOrigin::Current: now += offset; break;
            case SeekOrigin::End: {
                auto s = co_await size();
                if (!s) {
                    co_return Err(s.error());
                }
                now = *s + offset;
            }
        }
        mOffset = std::max<int64_t>(0, now);
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
            co_return Err(IoError::OperationNotSupported);
        }
        co_return fd_utils::truncate(fd(), size);
    }

    /**
     * @brief Doing seek operation
     * 
     * @return IoTask<uint64_t> 
     */
    auto tell() -> IoTask<uint64_t> {
        return seek(0, SeekOrigin::Current);
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
        co_return co_await ilias::blocking([&]() {
            return fd_utils::size(fd());
        });
    }

    /**
     * @brief Get the file descriptor
     * 
     * @return fd_t 
     */
    auto fd() const -> fd_t {
        return mHandle.fd().get();
    }

    // Operator
    auto operator <=>(const File &) const = default;
    auto operator =(File &&) -> File & = default;

    /**
     * @brief Check if the file stream is valid
     * 
     * @return true 
     * @return false 
     */
    explicit operator bool() const noexcept {
        return bool(mHandle);
    }

    /**
     * @brief Open a file
     * 
     * @tparam T 
     * @param path The path of the file
     * @param options The open options (default to read-only)
     * @return IoTask<File> 
     */
    template <fs::IntoPath T>
    static auto open(const T &path, OpenOptions options = OpenOptions::ReadOnly) -> IoTask<File> {
        return options.open(path);
    }
private:
    IoHandle<FileDescriptor> mHandle;
    std::optional<uint64_t>  mOffset; //< The offset of the file stream, nullopt for unsupport seek
};

// OpenOptions
template <fs::IntoPath T>
inline auto OpenOptions::open(const T &path) const -> IoTask<File> {
    return doOpen(*this, fs::toPath(path));
}

inline auto OpenOptions::doOpen(OpenOptions self, fs::Path path) -> IoTask<File> {
    // Validate
    if (!self.mRead && !self.mWrite && !self.mAppend) {
        co_return Err(IoError::InvalidArgument);
    }

    // Truncate need write permission
    if (self.mTruncate && !self.mWrite) {
        co_return Err(IoError::InvalidArgument);
    }

    // Append is conflict with truncate
    if (self.mAppend && self.mTruncate) {
        co_return Err(IoError::InvalidArgument);
    }

    // Create need write permission
    if ((self.mCreate || self.mCreateNew) && !self.mWrite && !self.mAppend) {
        co_return Err(IoError::InvalidArgument);
    }
#if defined(_WIN32)
    //  create | truncate | createNew  |  disposition
    // --------|----------|------------|------------------
    //    -    |    -     |     -      | OPEN_EXISTING
    //    ✓    |    -     |     -      | OPEN_ALWAYS
    //    -    |    ✓     |     -      | TRUNCATE_EXISTING
    //    ✓    |    ✓     |     -      | CREATE_ALWAYS
    //    *    |    *     |     ✓      | CREATE_NEW
    ::DWORD access = 0;
    ::DWORD creationDisposition = 0;
    ::DWORD flagsAndAttributes = FILE_FLAG_OVERLAPPED;
    ::DWORD lastError = ERROR_SUCCESS;

    // Access
    if (self.mRead) {
        access |= GENERIC_READ;
    }
    if (self.mWrite) {
        access |= GENERIC_WRITE;
    }
    if (self.mAppend) {
        access &= ~(DWORD)GENERIC_WRITE;
        access |= FILE_APPEND_DATA;
    }

    // creationDisposition
    if (self.mCreateNew) {
        creationDisposition = CREATE_NEW;
    }
    else if (self.mCreate && self.mTruncate) {
        creationDisposition = CREATE_ALWAYS;
    }
    else if (self.mCreate) {
        creationDisposition = OPEN_ALWAYS;
    }
    else if (self.mTruncate) {
        creationDisposition = TRUNCATE_EXISTING;
    }
    else {
        creationDisposition = OPEN_EXISTING;
    }

    auto fd = co_await ilias::blocking([&]() { // The CreateFileW may block
        auto fd = ::CreateFileW(
            path.wstring().c_str(),
            access,
            self.mShareMode,
            nullptr,
            creationDisposition,
            flagsAndAttributes,
            nullptr
        );
        if (fd == INVALID_HANDLE_VALUE) {
            lastError = ::GetLastError();
        }
        return fd;
    });
    if (lastError != ERROR_SUCCESS) {
        co_return Err(SystemError(lastError));
    }
#else
    // Mapping by man fopen
    // r  | O_RDONLY
    // w  | O_WRONLY | O_CREAT | O_TRUNC 
    // a  | O_WRONLY | O_CREAT | O_APPEND
    // r+ | O_RDWR                       
    // w+ | O_RDWR | O_CREAT | O_TRUNC   
    // a+ | O_RDWR | O_CREAT | O_APPEND  
    int flags = O_CLOEXEC;
    int errc = 0;
    bool write = self.mWrite || self.mAppend;

    if (self.mRead && write) {
        flags |= O_RDWR;
    }
    else if (self.mRead) {
        flags |= O_RDONLY;
    }
    else if (write) {
        flags |= O_WRONLY; 
    }

    if (self.mAppend) {
        flags |= O_APPEND;
    }
    if (self.mTruncate) {
        flags |= O_TRUNC;
    }
    if (self.mCreate) {
        flags |= O_CREAT;
    }
    if (self.mCreateNew) {
        flags |= O_CREAT | O_EXCL;
    }

    auto fd = co_await ilias::blocking([&]() {
        auto u8 = path.u8string();
        auto s = reinterpret_cast<const char *>(u8.c_str());
        auto fd = 0;
        if (flags & O_CREAT) {
            fd = ::open(s, flags, self.mMode);
        }
        else {
            fd = ::open(s, flags);
        }
        if (fd < 0) {
            errc = errno;
        }
        return fd;
    });
    if (fd < 0) {
        co_return Err(SystemError(errc));
    }
#endif // defined(_WIN32)

    // Wrap the file descriptor
    auto handle = IoHandle<FileDescriptor>::make(FileDescriptor {fd}, IoDescriptor::File);
    if (!handle) {
        co_return Err(handle.error());
    }


    std::optional<uint64_t> off;
    // Check if the file is a regular file (support offset)
    // Copy the lowlevel offset from the fd, and let us manage it
    if (self.mAppend) { // Append mode, the offset is not available
        co_return File {std::move(*handle), std::nullopt};
    }
#if defined(_WIN32)
    if (::GetFileType(fd) == FILE_TYPE_DISK) {
        ::LARGE_INTEGER offset { .QuadPart = 0 };
        ::LARGE_INTEGER cur;
        if (::SetFilePointerEx(fd, offset, &cur, FILE_CURRENT)) {
            off = cur.QuadPart;
        }
    }
#else
    struct stat st;
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        auto offset = ::lseek(fd, 0, SEEK_CUR);
        if (offset != -1) {
            off = offset;
        }
    }
#endif // defined(_WIN32)

    co_return File {std::move(*handle), off};
}

// Predefined open options
constexpr OpenOptions OpenOptions::ReadOnly = OpenOptions {}.read(true);
constexpr OpenOptions OpenOptions::WriteOnly = OpenOptions {}.write(true).create(true).truncate(true);
constexpr OpenOptions OpenOptions::ReadWrite = OpenOptions {}.read(true).write(true).create(true);

ILIAS_NS_END