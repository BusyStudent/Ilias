#pragma once

#include "ilias_backend.hpp"
#if !defined(ILIAS_NO_FILE)


ILIAS_NS_BEGIN

/**
 * @brief A File 
 * 
 */
class File {
public:
    File();
    File(IoContext &ctxt);
    File(const File &) = delete;
    ~File();

    auto read(void *buffer, size_t n) -> Task<size_t>;
    auto write(const void *buffer, size_t n) -> Task<size_t>;
    auto seek(int64_t offset, int whence) -> Result<size_t>;
    auto tell() -> Result<size_t>;
    auto size() -> Result<size_t>;

    /**
     * @brief Open a utf8 path
     * 
     * @param path
     * @param mode "wb rb" liked 
     */
    auto open(std::string_view path, std::string_view mode) -> Result<>;
    auto close() -> void;
    
    auto isOpen() const -> bool;
    auto get() const -> fd_t;
    auto context() const -> IoContext *;
    /**
     * @brief Create a File handle from a FILE *, it doesnot take the ownship of the FILE
     * 
     * @param ctxt 
     * @param fp 
     * @return Result<File> 
     */
    static auto fromFILE(IoContext &ctxt, FILE *fp) -> Result<File>;
protected:
    IoContext *mCtxt = nullptr;
    fd_t mFd = fd_t(-1);
};

// --- File
inline File::File(IoContext &ctxt) : mCtxt(&ctxt) { }
inline File::File() { }
inline File::~File() { close(); }

inline auto File::close() -> void {
    if (!isOpen()) {
        return;
    }
    mCtxt->removeFd(mFd);
    mCtxt = nullptr;
    mFd = fd_t(-1);
}
inline auto File::isOpen() const -> bool {
    return mFd != fd_t(-1);
}
inline auto File::read(void *buffer, size_t n) -> Task<size_t> {
    return mCtxt->read(mFd, buffer, n);
}
inline auto File::write(const void *buffer, size_t n) -> Task<size_t> {
    return mCtxt->write(mFd, buffer, n);
}

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
inline auto File::open(std::string_view path, std::string_view mode) -> Result<> {
    if (isOpen()) {
        close();
    }
    // ::CreateFileW();
    return Unexpected(Error::Unknown);
}

ILIAS_NS_END
#endif