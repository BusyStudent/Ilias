/**
 * @file pipe.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief pipe class
 * @version 0.1
 * @date 2024-08-30
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
 * @brief The reader part of the pipe
 * 
 */
class PipeReader final : public ReadableExt<PipeReader> {
public:
    PipeReader() = default;
    PipeReader(PipeReader &&) = default;
    PipeReader(IoHandle<FileDescriptor> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }
    auto detach() { return mHandle.detach(); }

    // Readable
    auto read(MutableBuffer buffer) -> IoTask<size_t> {

#if defined(_WIN32) // Windows named pipe spec
        auto val = co_await mHandle.read(buffer, std::nullopt);
        if (val == Err(SystemError(ERROR_BROKEN_PIPE))) {
            co_return 0; // Map closed to EOF(0)
        }
        co_return val;
#else
        return mHandle.read(buffer, std::nullopt);
#endif // defined(_WIN32)

    }

    // Operator
    auto operator <=>(const PipeReader &) const = default;
    auto operator =(PipeReader &&) -> PipeReader & = default;

    // Check if the pipe is valid
    explicit operator bool() const {
        return bool(mHandle);
    }
private:
    IoHandle<FileDescriptor> mHandle;
};

/**
 * @brief The writer part of the pipe
 * 
 */
class PipeWriter final : public WritableExt<PipeWriter> {
public:
    PipeWriter() = default;
    PipeWriter(PipeWriter &&) = default;
    PipeWriter(IoHandle<FileDescriptor> h) : mHandle(std::move(h)) {}

    auto close() { return mHandle.close(); }
    auto cancel() { return mHandle.cancel(); }
    auto detach() { return mHandle.detach(); }

    // Writable
    auto write(Buffer buffer) -> IoTask<size_t> {
        return mHandle.write(buffer, std::nullopt);
    }

    auto shutdown() -> IoTask<void> {
        co_return {};
    }

    auto flush() -> IoTask<void> {
        co_return {};
    }

    // Operator
    auto operator <=>(const PipeWriter &) const = default;
    auto operator =(PipeWriter &&) -> PipeWriter & = default;

    // Check if the pipe is valid
    explicit operator bool() const {
        return bool(mHandle);
    }
private:
    IoHandle<FileDescriptor> mHandle;
};

/**
 * @brief The pair of pipe reader and writer
 * 
 */
class PipePair {
public:
    PipeWriter writer;
    PipeReader reader;

    /**
     * @brief Create a pipe pair (writer -> reader)
     * 
     * @return IoResult<PipePair> 
     */
    static auto make() -> IoResult<PipePair> {
        auto pair = fd_utils::pipe();
        if (!pair) {
            return Err(pair.error());
        }
        auto writer = IoHandle<FileDescriptor>::make(FileDescriptor {pair->writer}, IoDescriptor::Pipe);
        auto reader = IoHandle<FileDescriptor>::make(FileDescriptor {pair->reader}, IoDescriptor::Pipe);
        if (!writer) {
            return Err(writer.error());
        }
        if (!reader) {
            return Err(reader.error());
        }
        return PipePair {
            .writer = PipeWriter {std::move(*writer)},
            .reader = PipeReader {std::move(*reader)}
        };
    }
};

ILIAS_NS_END