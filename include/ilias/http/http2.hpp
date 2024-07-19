#pragma once


#include "../coro/channel.hpp"
#include "../coro/mutex.hpp"
#include "transfer.hpp"
#include <cmath>
#include <span>
#include <map>

ILIAS_NS_BEGIN

/**
 * @brief The Http2 frame type (by RFC 9113)
 * 
 */
enum class Http2FrameType : uint8_t {
    DATA          = 0x0,
    HEADERS       = 0x1,
    PRIORITY      = 0x2,
    RST_STREAM    = 0x3,
    SETTINGS      = 0x4,
    PUSH_PROMISE  = 0x5,
    PING          = 0x6,
    GOAWAY        = 0x7,
    WINDOW_UPDATE = 0x8,
    CONTINUATION  = 0x9,
};

enum class Http2StreamState : uint8_t {
    Idle = 0,
    Open = 4,
    HalfClosedLocal = 5,
    HalfClosedRemote = 6,
    Closed = 7,
};

/**
 * @brief The Http2 Detailed Error code on stream or connection
 * 
 */
enum class Http2Error : uint32_t {
    OK                  = 0x0,  //< NO_ERROR
    PROTOCOL_ERROR      = 0x1,
    INTERNAL_ERROR      = 0x2,
    FLOW_CONTROL_ERROR  = 0x3,
    SETTINGS_TIMEOUT    = 0x4,
    STREAM_CLOSED       = 0x5,
    FRAME_SIZE_ERROR    = 0x6,
    REFUSED_STREAM      = 0x7,
    CANCEL              = 0x8,
    COMPRESSION_ERROR   = 0x9,
    CONNECT_ERROR       = 0x0a,
    ENHANCE_YOUR_CALM   = 0x0b,
    INADEQUATE_SECURITY = 0x0c,
    HTTP_1_1_REQUIRED   = 0x0d,
};


/**
 * @brief Raw Http2 Frame
 * 
 */
struct Http2Frame {
    uint8_t length_[3];   //< 24 length
    uint8_t type;         //< 8 type
    uint8_t flags;        //< 8 flags
                          //< 1 bit was reserved
    uint8_t streamId_[4]; //< 31 streamId

    
    auto length() const -> uint32_t {
        auto [a, b, c] = length_;
        return uint32_t(a) << 16 | uint32_t(b) << 8 | uint32_t(c);
    }
    auto streamId() const -> uint32_t {
        auto [a, b, c, d] = streamId_;
        return (uint32_t(a) & 0b01111111) << 24 | uint32_t(b) << 16 | uint32_t(c) << 8 | uint32_t(d);
    }
};

/**
 * @brief The data frame
 * 
 */
struct Http2DataFrame final : Http2Frame {
    uint8_t padLength; //< 8 pad length
    uint8_t payload[]; //< data, pad


    // flags here is 4 unused, 1 padded, 2 unused, 1 endStream
    auto endStream() const -> bool {
        return flags & 0b00000001;
    }
    auto padded() const -> bool {
        return flags & 0b00001000;
    }
    auto data() -> std::span<std::byte> {
        auto len = length();
        len -= sizeof(uint8_t);
        if (padded()) {
            len -= padLength;
        }
        return std::as_writable_bytes(std::span(payload, len));
    }
};

struct Http2HeaderFrame final : Http2Frame {
    uint8_t padLength;     //< 8 pad length
                           //< 1 Exclusive 
    uint8_t streamDeps[4]; //< 31 Stream Dependency
    uint8_t weight;        //< 8 weight
    uint8_t payload[];     //< Field Block Fragment, pad


    // flags here is 2 unused, 1 priority, 1 unused, 1 padded, 1 end headers, 1 unused, 1 end stream
    auto endHeaders() const noexcept -> bool {
        return flags & 0b00000100;
    }
    auto endStream() const noexcept -> bool {
        return flags & 0b00000001;
    }
    auto padded() const noexcept -> bool {
        return flags & 0b00001000;
    }
};

struct Http2Setting {
    uint16_t id;
    uint32_t value;
};

static_assert(sizeof(Http2Frame) == 9, "Http2Frame size mismatch with RFC 9113");
static_assert(sizeof(Http2DataFrame) == 10);


class Http2Connection final : public HttpConnection {
public:
    Http2Connection(IStreamClient &&client) : mClient(std::move(client)) {
        mHandle = ilias_go _processFrames();
    }
    ~Http2Connection() {
        mHandle.cancel();
        mHandle.join();
    }
private:
    auto _processFrames() -> Task<void>;

    ByteStream<>     mClient;
    JoinHandle<void> mHandle;
};

ILIAS_NS_END