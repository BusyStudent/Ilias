#pragma once

#include "ilias_backend.hpp"

#if defined(_WIN32)
    #define ILIAS_FILE_STDIN "CONIN$"
    #define ILIAS_FILE_STDOUT "CONOUT$"
    #define ILIAS_FILE_STDERR "CONOUT$"
    #include <io.h>
    #include <fcntl.h>
    #include <windows.h>
#else
    #define ILIAS_FILE_STDIN "/dev/stdin"
    #define ILIAS_FILE_STDOUT "/dev/stdout"
    #define ILIAS_FILE_STDERR "/dev/stderr"
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/stat.h>
#endif

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
    File(File &&f);
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
     * @brief Open a file 
     * 
     * @param ctxt 
     * @param path 
     * @param mode 
     * @return Result<File> 
     */
    static auto open(IoContext &ctxt, std::string_view path, std::string_view mode) -> Result<File>;
    /**
     * @brief Create a File handle from a FILE *, it doesnot take the ownship of the FILE
     * 
     * @param ctxt 
     * @param fp 
     * @return Result<File> 
     */
    static auto fromFILE(IoContext &ctxt, FILE *fp) -> Result<File>;
    /**
     * @brief Open the stdin
     * 
     * @param ctxt 
     * @return Result<File> 
     */
    static auto fromStdin(IoContext &ctxt) -> Result<File>;
    static auto fromStdout(IoContext &ctxt) -> Result<File>;
    static auto fromStderr(IoContext &ctxt) -> Result<File>;
protected:
    IoContext *mCtxt = nullptr;
    fd_t mFd = fd_t(-1);
};

// --- File
inline File::File(IoContext &ctxt) : mCtxt(&ctxt) { }
inline File::File(File &&f) : mCtxt(f.mCtxt), mFd(f.mFd) {
    f.mCtxt = nullptr;
    f.mFd = fd_t(-1);
}
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
inline auto File::get() const -> fd_t {
    return mFd;
}
inline auto File::context() const -> IoContext * {
    return mCtxt;
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

#if defined(_WIN32)
    auto wpathSize = ::MultiByteToWideChar(CP_UTF8, 0, path.data(), path.size(), nullptr, 0);
    std::wstring wpath(wpathSize, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, path.data(), path.size(), wpath.data(), wpathSize);
    
    // Calc flags here
    ::DWORD accessFlags = 0;
    ::DWORD shareFlags = 0;
    ::DWORD creationFlags = 0;
    if (mode.contains('w')) {
        accessFlags |= GENERIC_WRITE;
        creationFlags |= CREATE_ALWAYS;
    }
    if (mode.contains('r')) {
        accessFlags |= GENERIC_READ;
        creationFlags |= OPEN_EXISTING;
    }
    mFd = ::CreateFileW(wpath.c_str(), accessFlags, shareFlags, nullptr, creationFlags, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (mFd == INVALID_HANDLE_VALUE) {
        return Unexpected(Error::fromErrno(::GetLastError()));
    }
    auto err = mCtxt->addFd(mFd);
    if (!err) {
        ::CloseHandle(mFd);
        mFd = INVALID_HANDLE_VALUE;
        return err;
    }
#else

#endif
    return {};
}
inline auto File::open(IoContext &ctxt, std::string_view path, std::string_view mode) -> Result<File> {
    File file(ctxt);
    auto err = file.open(path, mode);
    if (!err) {
        return Unexpected(err.error());
    }
    return file;
}
inline auto File::fromStdin(IoContext &ctxt) -> Result<File> {
    return File::open(ctxt, ILIAS_FILE_STDIN, "r");
}
inline auto File::fromStdout(IoContext &ctxt) -> Result<File> {
    return File::open(ctxt, ILIAS_FILE_STDERR, "w");
}

ILIAS_NS_END
#endif