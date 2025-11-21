// INTERNAL!!!
/**
 * @file singleton.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief For slove the problem of multiple instances when using static libraries. inspired by YY-Thunks
 * @version 0.1
 * @date 2025-10-20
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#pragma once
#include <ilias/defines.hpp>

// Only used when build on static library
#if defined(ILIAS_STATIC) && !defined(ILIAS_NO_SINGLETON)
#include <unordered_map>
#include <shared_mutex> // std::shared_mutex
#include <stdexcept>
#include <optional>
#include <cstdarg> // va_list
#include <string>
#include <memory>
#include <thread> // std::thread::id
#include <mutex> // std::lock_guard

// Import system headers
#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif // _WIN32

// Import shared member
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/io/system_error.hpp>  // SystemCategory
#include <ilias/io/error.hpp>         // IoCategory
#include <ilias/net/addrinfo.hpp>     // GaiCategory
#include <ilias/fiber/fiber.hpp>      // FiberContext

#define ILIAS_SHM_SINGLETON 1

ILIAS_NS_BEGIN

// Using shared memory
namespace singleton {

// Import some names
using runtime::Executor;
using fiber::FiberContext;

// Thread Local wrapper
template <typename T>
class ThreadLocal;

// Thread Local pointer
template <typename T>
class ThreadLocal<T*> {
public:
    ThreadLocal() = default;
    ThreadLocal(const ThreadLocal &) = delete;
    ~ThreadLocal() = default;

    auto get() const -> T * {
        auto id = std::this_thread::get_id();
        auto locker = std::shared_lock(mMutex);
        auto it = mStorage.find(id);
        if (it != mStorage.end()) {
            return it->second;
        }
        return nullptr;
    }

    auto store(T *ptr) -> void {
        auto id = std::this_thread::get_id();
        auto locker = std::lock_guard(mMutex);
        if (!ptr) { // nullptr, remove it
            mStorage.erase(id);
        }
        else {
            mStorage[id] = ptr;
        }
    }

    // Overload operator
    auto operator =(T *ptr) -> ThreadLocal & {
        store(ptr);
        return *this;
    }
    
    operator T*() const {
        return get();
    }
private:
    std::unordered_map<std::thread::id, T*> mStorage;
    mutable std::shared_mutex               mMutex;
};


// The control blocks， used for sync
class SharedBlock {
public:
    size_t    refcount; // The reference count of the shared data
    std::byte data[];   // The shared data
};

// The shared data block
class SharedData {
public:
    // ABI information
    size_t                     size    = sizeof(SharedData);    // The size of the shared data
    std::string_view           version = ILIAS_VERSION_STRING;  // The version string (ILIAS_VERSION_STRING)

    // ABI information about the core components
    size_t                     promiseSize = sizeof(runtime::CoroPromise);
    size_t                     contextSize = sizeof(runtime::CoroContext);

    // Executor (ThreadLocal)
    ThreadLocal<Executor*>     executor; // The executor ptr for this thread (The runtime will set nullptr before thread exit)

#if !defined(_WIN32)
    // FiberContext (ThreadLocal)
    ThreadLocal<FiberContext*> fiberContext; // The current fiber context

    // ThreadPool (only use on linux)
    std::atomic_flag           threadpoolInit;
    std::atomic<void*>         threadpool;
#endif // _WIN32

    // ErrorCategory
    IoCategory                 ioCategory;
    GaiCategory                gaiCategory;
    SystemCategory             systemCategory;
};

// The manager for open / close the shared memory
class Manager {
public:
    Manager() {
        std::string uniqueName;

#if !defined(_WIN32)
        uniqueName += "/"; // man says we need / before the name for portable
#endif

        uniqueName += "IliasRuntimeSingleton-f4c69531-9f22-4d1f-a4eb-a4c9b7cb422c-";

#if   defined(__amd64__) || defined(_M_X64)
        uniqueName += "x64-";
#elif defined(__i386__)  || defined(_M_IX86)
        uniqueName += "x86-";
#elif defined(__arm__)   || defined(_M_ARM)
        uniqueName += "arm-";
#endif

        uniqueName += "ns-";
        uniqueName += ILIAS_STRINGIFY(ILIAS_NAMESPACE);
        uniqueName += "-";

#if defined(_WIN32)
        uniqueName += std::to_string(::GetCurrentProcessId());

        std::wstring mapName = win32::toWide(uniqueName) + L"-map";
        std::wstring mutexName = win32::toWide(uniqueName) + L"-mutex";

        // Just allocate 1 pages, it should be enough
        ::SYSTEM_INFO info;
        ::GetSystemInfo(&info);
        auto blockSize = info.dwPageSize;
        ILIAS_ASSERT(blockSize >= sizeof(SharedData) + sizeof(SharedBlock));

        // Open the mutex for sync
        mMutexHandle = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mMutexHandle) {
            panic("CreateMutexW failed %d", ::GetLastError());
        }

        // Lock it
        if (::WaitForSingleObject(mMutexHandle, INFINITE) != WAIT_OBJECT_0) {
            panic("Lock Mutex failed %d", ::GetLastError());
        }

        struct Guard {
            ~Guard() {
                ::ReleaseMutex(h);
            }
            HANDLE h;
        } guard {mMutexHandle};

        mMapHandle = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr, 
            PAGE_READWRITE, 
            0, 
            blockSize, 
            mapName.c_str()
        );
        if (!mMapHandle) {
            panic("CreateFileMappingW failed %d", ::GetLastError());
        }
        mSharedBlock = static_cast<SharedBlock*>(::MapViewOfFile(mMapHandle, FILE_MAP_ALL_ACCESS, 0, 0, blockSize));
        if (!mSharedBlock) {
            panic("MapViewOfFile failed %d", ::GetLastError());
        }
#else
        uniqueName += std::to_string(::getpid());

        // Get the page size
        mBlockSize = ::getpagesize();
        ILIAS_ASSERT(mBlockSize >= sizeof(SharedData) + sizeof(SharedBlock));

        mShmFd = ::shm_open(uniqueName.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (mShmFd == -1) {
            panic("shm_open failed %d", errno);
        }

        // Lock it
        if (::lockf(mShmFd, F_LOCK, 0) == -1) {
            panic("lockf failed %d", errno);
        }
        struct Guard {
            ~Guard() {
                ::lockf(fd, F_ULOCK, 0);
            }
            int fd;  
        } guard {mShmFd};

        // Make sure has this size
        if (::ftruncate(mShmFd, mBlockSize) == -1) {
            panic("ftruncate failed %d", errno);
        }

        mSharedBlock = static_cast<SharedBlock*>(::mmap(nullptr, mBlockSize, PROT_READ | PROT_WRITE, MAP_SHARED, mShmFd, 0));
        if (mSharedBlock == MAP_FAILED) {
            panic("mmap failed %d", errno);
        }
        mShmName = std::move(uniqueName);
#endif // _WIN32
        // Loaded, accroding to msdn, the first time created the shared memory, the bytes is all zero
        auto refcount = std::atomic_ref {mSharedBlock->refcount}.fetch_add(1);
        if (refcount == 0) { // Init it
            mSharedData = std::construct_at(reinterpret_cast<SharedData*>(mSharedBlock->data));
        }
        else {
            mSharedData = std::launder(reinterpret_cast<SharedData*>(mSharedBlock->data));
        }

        // Check ABI
        checkAbi<SharedData>(mSharedData->size);
        checkAbi<runtime::CoroPromise>(mSharedData->promiseSize);
        checkAbi<runtime::CoroContext>(mSharedData->contextSize);
        if (mSharedData->version != ILIAS_VERSION_STRING) {
            panic("ABI mismatch want %s, got %s", ILIAS_VERSION_STRING, mSharedData->version.data());
        }
    }

    ~Manager() {
#if defined(_WIN32)
        if (::WaitForSingleObject(mMutexHandle, INFINITE) != WAIT_OBJECT_0) {
            panic("Lock Mutex failed %d", ::GetLastError());
        }

        struct Guard {
            ~Guard() {
                ::ReleaseMutex(h);
            }
            HANDLE h;
        } guard {mMutexHandle};

        if (mSharedBlock) {
            auto refcount = std::atomic_ref {mSharedBlock->refcount}.fetch_sub(1);
            if (refcount == 1) { // To zero, destroy it
                std::destroy_at(mSharedData);
            }
            if (!::UnmapViewOfFile(mSharedBlock)) {
                panic("UnmapViewOfFile failed %d", ::GetLastError());
            }
        }
        if (!::CloseHandle(mMapHandle)) {
            panic("CloseHandle failed %d", ::GetLastError());
        }
#else
        if (::lockf(mShmFd, F_LOCK, 0) == -1) {
            panic("lockf failed %d", errno);
        }
        
        struct Guard {
            ~Guard() {
                ::lockf(fd, F_ULOCK, 0);
                ::close(fd);
            }
            int fd;
        } guard {mShmFd};

        if (mSharedBlock) {
            auto refcount = std::atomic_ref {mSharedBlock->refcount}.fetch_sub(1);
            if (refcount == 1) { // To zero, destroy it
                std::destroy_at(mSharedData);

                // We are the last one, remove it
                if (::shm_unlink(mShmName.c_str()) == -1) {
                    panic("shm_unlink failed %d", errno);
                }
            }
            if (::munmap(mSharedBlock, mBlockSize) == -1) {
                panic("munmap failed %d", errno);
            }
        }
#endif // _WIN32
    }

    template <typename T>
    auto checkAbi(size_t got) -> void {
        if (got != sizeof(T)) {
            panic("ABI mismatch want %zu, got %zu", sizeof(T), got);
        }
    }

    [[noreturn]]
    auto panic(const char *fmt, ...) -> void {
        char buf[1024] {0};
        va_list args;
        va_start(args, fmt);
        ::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        auto message = std::string("ERROR [Singleton] ") + buf;
        ILIAS_THROW(std::runtime_error(message));
    }

private:
#if defined(_WIN32)
    HANDLE      mMapHandle = nullptr;
    HANDLE      mMutexHandle = nullptr;
#else
    int         mShmFd = -1;
    size_t      mBlockSize = 0;
    std::string mShmName; // Used for unlink
#endif // _WIN32

    SharedBlock *mSharedBlock = nullptr;
    SharedData  *mSharedData = nullptr;
friend auto access() -> SharedData &;
};

// Access the shared data
inline auto access() -> SharedData & {
    static  Manager manager;
    return *manager.mSharedData;
}

} // namespace singleton

// The proxy for singleton
template <typename T>
class Singleton;

#define MAKE_SINGLETON(type, type2, name) \
    template <>                           \
    class Singleton<type> {               \
    public:                               \
        constexpr Singleton() = default;  \
        constexpr ~Singleton() = default; \
                                          \
        template <typename T>             \
        auto operator =(T &&what) const -> const Singleton & { \
            auto &name = singleton::access().name;             \
            name = std::forward<T>(what);                      \
            return *this;                                      \
        }                                                      \
                                                               \
        operator type2() const {                               \
            auto &name = singleton::access().name;             \
            return name;                                       \
        }                                                      \
    };                                                         \

// For category and any more
#if !defined(_WIN32)
MAKE_SINGLETON(fiber::FiberContext *, fiber::FiberContext *, fiberContext);
#endif // _WIN32

MAKE_SINGLETON(runtime::Executor *, runtime::Executor *, executor);
MAKE_SINGLETON(SystemCategory, const SystemCategory &,systemCategory);
MAKE_SINGLETON(GaiCategory, const GaiCategory &, gaiCategory);
MAKE_SINGLETON(IoCategory, const IoCategory &, ioCategory);

#undef MAKE_SINGLETON
#undef MAKE_SINGLETON_PTR

ILIAS_NS_END

#else // Dll Mode

ILIAS_NS_BEGIN

// Just using it
template <typename T>
using Singleton = T;

ILIAS_NS_END

#endif // ILIAS_STATIC