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
    auto onReadChannelFinished() -> void;
    auto onClose() -> void;

    QPointer<QIODevice> mDevice;
    ilias::Event        mReadReadyEvent {ilias::Event::AutoClear};
    ilias::Event        mWriteReadyEvent {ilias::Event::AutoClear};
    bool                mReadEOF = false;
};

// Implementation
inline StreamAdapter::StreamAdapter(QIODevice *device, QObject *parent) : QObject(parent), mDevice(device) {
    QObject::connect(mDevice, &QIODevice::readyRead, this, &StreamAdapter::onReadReady);
    QObject::connect(mDevice, &QIODevice::bytesWritten, this, &StreamAdapter::onBytesWritten);
    QObject::connect(mDevice, &QIODevice::readChannelFinished, this, &StreamAdapter::onReadChannelFinished);
    QObject::connect(mDevice, &QIODevice::aboutToClose, this, &StreamAdapter::onClose);
    QObject::connect(mDevice, &QIODevice::destroyed, this, &StreamAdapter::onClose);
}

inline auto StreamAdapter::read(ilias::MutableBuffer buffer) -> ilias::IoTask<size_t> {
    while (true) { // Wait for more data
        if (!mDevice || !mDevice->isReadable()) {
            break;
        }
        qint64 len = mDevice->read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        if (len > 0) {
            co_return len;
        }
        if (len < 0) { // -1 may be an error or EOF
            if (mDevice->isSequential() && mReadEOF) { // Maybe network device, EOF
                break;
            }
            co_return Err(ilias::IoError::Other);
        }
        // Is real EOF?
        if (!mDevice->isSequential() && mDevice->atEnd()) { // Maybe file
            break;
        }
        co_await mReadReadyEvent;
    }
    co_return 0;
}

inline auto StreamAdapter::write(ilias::Buffer buffer) -> ilias::IoTask<size_t> {
    while (true) { // Wait for previous write to finish
        if (!mDevice || !mDevice->isWritable()) {
            break;
        }
        qint64 len = mDevice->write(reinterpret_cast<const char *>(buffer.data()), buffer.size());
        if (len > 0) {
            co_return len;
        }
        if (len < 0) {
            if (mDevice->isSequential()) { // Maybe network device, EOF
                break;
            }
            co_return Err(ilias::IoError::Other);
        }
        // Is real EOF?
        if (!mDevice->isSequential() && mDevice->atEnd()) { // Maybe file
            break;
        }
        co_await mWriteReadyEvent;
    }
    co_return 0;
}

inline auto StreamAdapter::flush() -> ilias::IoTask<void> {
    // Wait for all bytes to be written
    while (true) { // Wait for previous write to finish
        if (!mDevice || mDevice->bytesToWrite() == 0) {
            co_return {};
        }
        if (!mDevice->isWritable()) {
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

inline auto StreamAdapter::onReadChannelFinished() -> void {
    mReadEOF = true;
    mReadReadyEvent.set();
}

inline auto StreamAdapter::onClose() -> void {
    mReadEOF = true;
    mReadReadyEvent.set();
    mWriteReadyEvent.set();
}

} // namespace ilias_qt