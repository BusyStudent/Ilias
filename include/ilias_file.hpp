#pragma once

#include "ilias_backend.hpp"
#if !defined(ILIAS_NO_FILE)


ILIAS_NS_BEGIN

/**
 * @brief A File Descriptor
 * 
 */
class FileDescriptor {
public:
    FileDescriptor(IoContext &ctxt);
    FileDescriptor(IoContext &ctxt, fd_t fd);
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&fp);
    FileDescriptor();
    ~FileDescriptor();

    auto close() -> void;
    auto setFd(fd_t fd) -> Result<void>;
    auto read(void *buffer, size_t n) -> Task<size_t>;
    auto write(const void *buffer, size_t n) -> Task<size_t>;

    auto isValid() const -> bool;
    auto get() const -> fd_t;
    auto context() const -> IoContext *;
protected:
    IoContext *mCtxt = nullptr;
    fd_t mFd = fd_t(-1);
};

/**
 * @brief A File 
 * 
 */
class File : public FileDescriptor {
public:
    using FileDescriptor::FileDescriptor;

    auto seek(int64_t offset, int whence) -> Result<size_t>;
    auto tell() -> Result<size_t>;
    auto size() -> Result<size_t>;
    auto open(std::string_view path, std::string_view mode) -> Result<void>;
    /**
     * @brief Create a File handle from a FILE *, it doesnot take the ownship of the FILE
     * 
     * @param ctxt 
     * @param fp 
     * @return Result<File> 
     */
    static auto fromFILE(IoContext &ctxt, FILE *fp) -> Result<File>;
};

// --- FileDescriptor
inline FileDescriptor::FileDescriptor(IoContext &ctxt, fd_t fd) : mCtxt(&ctxt), mFd(fd) {
    if (!ctxt.addFd(fd)) {
        fd = fd_t(-1);
    }
}
inline FileDescriptor::~FileDescriptor() {
    close();
}
inline auto FileDescriptor::close() -> void {
    if (!isValid()) {
        return;
    }
    mCtxt->removeFd(mFd);
    mCtxt = nullptr;
    mFd = fd_t(-1);
}
inline auto FileDescriptor::isValid() const -> bool {
    return mFd != fd_t(-1);
}
inline auto FileDescriptor::read(void *buffer, size_t n) -> Task<size_t> {
    return mCtxt->read(mFd, buffer, n);
}
inline auto FileDescriptor::write(const void *buffer, size_t n) -> Task<size_t> {
    return mCtxt->write(mFd, buffer, n);
}

// --- File
inline auto File::seek(int64_t offset, int whence) -> Result<size_t> {
    static_assert(FILE_CURRENT == SEEK_CUR);
    static_assert(FILE_BEGIN == SEEK_SET);
    static_assert(FILE_END == SEEK_END);
    ::LARGE_INTEGER cur;
    ::LARGE_INTEGER off;
    off.QuadPart = offset;
    if (::SetFilePointerEx(mFd, off, &cur, whence)) {
        return cur.QuadPart;
    }
    return Unexpected(Error::fromErrno(::GetLastError()));
}
inline auto File::size() -> Result<size_t> {
    ::LARGE_INTEGER s;
    if (::GetFileSizeEx(mFd, &s)) {
        return s.QuadPart;
    }
    return Unexpected(Error::fromErrno(::GetLastError()));
}
inline auto File::tell() -> Result<size_t> {
    return seek(0, SEEK_CUR);
}

ILIAS_NS_END
#endif