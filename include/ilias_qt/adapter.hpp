#pragma once

#include <ilias/sync/event.hpp>
#include <ilias/io.hpp>
#include <QByteArray>
#include <QIODevice>
#include <QPointer>

namespace ilias_qt {

using ilias::Err;

// MARK: StreamAdapter
/**
 * @brief Mapping QIODevice to ilias Stream
 * 
 */
class StreamAdapter final : public QObject, public ilias::StreamExt<StreamAdapter> {
public:
    StreamAdapter(QIODevice *device, QObject *parent = nullptr);
    StreamAdapter(const StreamAdapter &) = delete;

    // Readable
    auto read(ilias::MutableBuffer buffer) -> ilias::IoTask<size_t>;

    // Writable
    auto write(ilias::Buffer buffer) -> ilias::IoTask<size_t>;
    auto flush() -> ilias::IoTask<void>;
    auto shutdown() -> ilias::IoTask<void>;

    // Access QIODevice
    auto device() const -> QIODevice * { return mDevice.get(); }
private:
    auto onReadReady() -> void;
    auto onBytesWritten(qint64 bytes) -> void;
    auto onClose() -> void;

    QPointer<QIODevice> mDevice;
    ilias::Event        mReadReadyEvent {ilias::Event::AutoClear};
    ilias::Event        mWriteReadyEvent {ilias::Event::AutoClear};
};

// Implementation
inline StreamAdapter::StreamAdapter(QIODevice *device, QObject *parent) : QObject(parent), mDevice(device) {
    QObject::connect(mDevice, &QIODevice::readyRead, this, &StreamAdapter::onReadReady);
    QObject::connect(mDevice, &QIODevice::bytesWritten, this, &StreamAdapter::onBytesWritten);
}

inline auto StreamAdapter::read(ilias::MutableBuffer buffer) -> ilias::IoTask<size_t> {
    while (true) { // Wait for more data
        if (!mDevice || !mDevice->isReadable() || mDevice->atEnd()) {
            co_return 0; // EOF
        }
        if (mDevice->bytesAvailable() != 0) {
            break;
        }
        co_await mReadReadyEvent;
    }
    qint64 len = mDevice->read(reinterpret_cast<char *>(buffer.data()), buffer.size());
    if (len < 0) {
        co_return Err(ilias::IoError::Other);
    }
    co_return len;
}

inline auto StreamAdapter::write(ilias::Buffer buffer) -> ilias::IoTask<size_t> {
    while (true) { // Wait for previous write to finish
        if (!mDevice || !mDevice->isWritable() || mDevice->atEnd()) {
            co_return 0;
        }
        if (mDevice->bytesToWrite() == 0) {
            break;
        }
        co_await mWriteReadyEvent;
    }
    qint64 len = mDevice->write(reinterpret_cast<const char *>(buffer.data()), buffer.size());
    if (len < 0) {
        co_return Err(ilias::IoError::Other);
    }
    co_return len;
}

inline auto StreamAdapter::flush() -> ilias::IoTask<void> {
    // Wait for all bytes to be written
    while (true) { // Wait for previous write to finish
        if (!mDevice || mDevice->bytesToWrite() == 0) {
            co_return {};
        }
        if (!mDevice->isWritable() || mDevice->atEnd()) {
            co_return Err(ilias::IoError::UnexpectedEOF);
        }
        co_await mWriteReadyEvent;
    }
}

inline auto StreamAdapter::shutdown() -> ilias::IoTask<void> {
    co_return {};
}

inline auto StreamAdapter::onReadReady() -> void {
    mReadReadyEvent.set();
}

inline auto StreamAdapter::onBytesWritten(qint64 bytes) -> void {
    mWriteReadyEvent.set();
}

inline auto StreamAdapter::onClose() -> void {
    mReadReadyEvent.set();
    mWriteReadyEvent.set();
}

} // namespace ilias_qt