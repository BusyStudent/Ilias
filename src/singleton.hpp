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
#if defined(ILIAS_STATIC)
#include <unordered_map>
#include <shared_mutex> // std::shared_mutex
#include <stdexcept>
#include <optional>
#include <string>
#include <memory>
#include <thread> // std::thread::id
#include <mutex> // std::lock_guard

// Import system headers
#if defined(_WIN32)
    #include <ilias/detail/win32defs.hpp>
#endif // _WIN32

// Import shared member
#include <ilias/runtime/executor.hpp> // Executor
#include <ilias/io/system_error.hpp>  // SystemCategory
#include <ilias/io/error.hpp>         // IoCategory
#include <ilias/net/addrinfo.hpp>     // GaiCategory

ILIAS_NS_BEGIN

// Using shared memory
namespace singleton {

// Import some names
using runtime::Executor;

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
    size_t                 size    = sizeof(SharedData);    // The size of the shared data
    std::string_view       version = ILIAS_VERSION_STRING;  // The version string (ILIAS_VERSION_STRING)

    // Executor (ThreadLocal)
    ThreadLocal<Executor*> executor; // The executor ptr for this thread (The runtime will set nullptr before thread exit)

    // FiberContext (ThreadLocal)
#if !defined(_WIN32)
    ThreadLocal<void*>     fiberContext; // The current fiber context
#endif // _WIN32

    // ErrorCategory
    IoCategory             ioCategory;
    GaiCategory            gaiCategory;
    SystemCategory         systemCategory;
};

// The manager for open / close the shared memory
class Manager {
public:
    Manager() {

        std::string uniqueName = "IliasRuntimeSingleton-f4c69531-9f22-4d1f-a4eb-a4c9b7cb422c-";

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
        if (mSharedData->size != sizeof(SharedData)) {
            panic("ABI mismatch want %zu, got %zu", sizeof(SharedData), mSharedData->size);
        }
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
#endif // _WIN32
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
#endif // _WIN32

    SharedBlock *mSharedBlock = nullptr;
    SharedData  *mSharedData = nullptr;
friend auto access() -> SharedData &;
friend auto access2() -> SharedData &;
};

// Access the shared data
inline auto access() -> SharedData & {
    static  Manager manager;
    return *manager.mSharedData;
}

// Just for test purpose, test it with multiple instances of manager
inline auto access2() -> SharedData & {
#if defined(NDEBUG)
    return access();
#else
    static  Manager manager;
    return *manager.mSharedData;
#endif // NDEBUG
}

} // namespace singleton

// The proxy for singleton
template <typename T>
class Singleton;

#define MAKE_SINGLETON(type, name)        \
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
        operator type &() const {                              \
            auto &name = singleton::access().name;             \
            return name;                                       \
        }                                                      \
    };                                                         \

#define MAKE_SINGLETON_TLS(type, name)                         \
    template <>                                                \
    class Singleton<type> {                                    \
    public:                                                    \
        constexpr Singleton() = default;                       \
        constexpr ~Singleton() = default;                      \
                                                               \
        template <typename T>                                  \
        auto operator =(T &&what) const -> const Singleton & { \
            auto &name = singleton::access2().name;            \
            name = std::forward<T>(what);                      \
            return *this;                                      \
        }                                                      \
                                                               \
        operator type() const {                                \
            auto &name = singleton::access2().name;            \
            return name;                                       \
        }                                                      \
    };                                                         \

// For category and any more
MAKE_SINGLETON_TLS(runtime::Executor *, executor);
MAKE_SINGLETON(SystemCategory, systemCategory);
MAKE_SINGLETON(GaiCategory, gaiCategory);
MAKE_SINGLETON(IoCategory, ioCategory);

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