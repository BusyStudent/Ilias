/**
 * @file ws.hpp
 * @author BusyStudent (fyw90mc@gmail.com)
 * @brief This file contains the WebSocket class. and WebStream class
 * @version 0.1
 * @date 2025-02-26
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <ilias/detail/mem.hpp>
#include <ilias/io/dyn_traits.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/net/addrinfo.hpp>
#include <ilias/net/tcp.hpp>
#include <ilias/http/headers.hpp>
#include <ilias/task/task.hpp>
#include <ilias/buffer.hpp>
#include <ilias/crypt.hpp> // For base64 and sha1
#include <ilias/url.hpp>
#include <ilias/ssl.hpp>

ILIAS_NS_BEGIN

namespace detail {

struct WsFrame {
    enum Opcode : uint8_t {
        Continuation = 0,
        Text         = 1,
        Binary       = 2,
        Close        = 8,
        Ping         = 9,
        Pong         = 10,  
    };
    uint8_t fin        : 1 = 0;
    uint8_t rsv1       : 1 = 0;
    uint8_t rsv2       : 1 = 0;
    uint8_t rsv3       : 1 = 0;
    uint8_t opcode     : 4 = 0;
    uint8_t mask       : 1 = 0;
    uint8_t payloadLen : 7 = 0;
};
static_assert(sizeof(WsFrame) == 2);

} // namespace detail

/**
 * @brief The WebSocket class, used to create a WebSocket connection
 * 
 */
class WebSocket {
public:
    enum MessageType : uint8_t {
        TextMessage   = 1,
        BinaryMessage = 2,
    };
    enum CloseCode   : uint16_t {
        NormalClosure           = 1000,
        GoingAway               = 1001,
        ProtocolError           = 1002,
        UnsupportedData         = 1003,
        NoStatus                = 1005,
        AbnormalClosure         = 1006,
        InvalidFramePayloadData = 1007,
        PolicyViolation         = 1008,
        MessageTooBig           = 1009,
        MandatoryExtension      = 1010,
        InternalError           = 1011,
        ServiceRestart          = 1012,
        TryAgainLater           = 1013,
        BadGateway              = 1014,
        TlsHandshake            = 1015,  
    };
    enum FinalFlag : uint8_t {
        Final        = 1,
        Continuation = 0,
    };
    struct CloseEvent {
        int         code     = NoStatus;
        std::string message  = "";
        bool        wasClean = false;
    };

    static constexpr auto MagicKey = std::string_view {"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

    WebSocket();
    WebSocket(const WebSocket &) = delete;
    WebSocket(const Url &url, const HttpHeaders &headers = {});

    auto open() -> IoTask<void>;
    /**
     * @brief Gracefully close the connection and shutdown the low level stream
     * 
     * @param code The close code
     * @param message The close message (length must be less than 123)
     * @return IoTask<void> 
     */
    auto shutdown(uint16_t code = NormalClosure, std::string_view message = {}) -> IoTask<void>;

    /**
     * @brief Set the Stream object
     * 
     * @param stream 
     */
    auto setStream(DynStreamClient stream) -> void;

    /**
     * @brief Set the Url object
     * 
     * @param url 
     */
    auto setUrl(const Url &url) -> void;

    /**
     * @brief Set the Headers object
     * 
     * @param headers 
     */
    auto setHeaders(const HttpHeaders &headers) -> void;

    /**
     * @brief Send a message chunk to the server
     * @code 
     *  co_await ws.sendMessageChunk(data1Chunk, WebSocket::TextMessage, WebSocket::Continuation);
     *  co_await ws.sendMessageChunk(data1Chunk2, WebSocket::TextMessage, WebSocket::Final);
     *  co_await ws.sendMessageChunk(wholeData2, WebSocket::TextMessage, WebSocket::Final);
     * @endcode
     * 
     * @param data 
     * @param type The type of the message, it can be TextMessage or BinaryMessage (ignored on the continuation frame)
     * @param fin The final flag, it can be set to Final or Continuation
     * @return IoTask<void> 
     */
    auto sendMessageChunk(std::span<const std::byte> data, MessageType type, FinalFlag fin = Final) -> IoTask<void>;

    /**
     * @brief Send a message to the server
     * 
     * @param data The data to send
     * @param type The type of the message, it can be TextMessage or BinaryMessage (default is BinaryMessage)
     * @return IoTask<void> 
     */
    auto sendMessage(std::span<const std::byte> data, MessageType type = BinaryMessage) -> IoTask<void>;
    
    /**
     * @brief Send a text message to the server
     * 
     * @param text The text to send
     * @return IoTask<void> 
     */
    auto sendMessage(std::string_view text) -> IoTask<void>;

    /**
     * @brief Begin to receive a message
     * @example See the example in recvMessageChunk
     * 
     * @return IoTask<MessageType> The type of the message
     */
    auto recvMessageBegin() -> IoTask<MessageType>;

    /**
     * @brief Receive a chunk of message
     * @note It must be called after recvMessageBegin, and call it until it returns 0 to finish the message
     * @code 
     *  auto type = co_await ws.recvMessageBegin();
     *  auto len = co_await ws.recvMessageChunk(data);
     *  while (len && len != 0) {
     *      process(data);
     *      len = co_await ws.recvMessageChunk(data);
     *  }
     * @endcode
     * 
     * @param data The buffer to store the received data
     * @return IoTask<size_t> The size of the chunk received, 0 if the message is finished
     */
    auto recvMessageChunk(std::span<std::byte> data) -> IoTask<size_t>;

    /**
     * @brief Receive a message
     * 
     * @tparam T The container type to store the message
     * @return IoTask<std::pair<T, MessageType> > 
     */
    template <MemContainer T = std::vector<std::byte> >
    auto recvMessage() -> IoTask<std::pair<T, MessageType> >;

    /**
     * @brief Get the Close Event object
     * 
     * @return const std::optional<CloseEvent>& 
     */
    auto closeEvent() const -> const std::optional<CloseEvent> &;
private:
    auto readFrame(detail::WsFrame &frame, size_t &payloadLen) -> IoTask<void>;
    auto writeFrame(detail::WsFrame frame, std::span<const std::byte> buffer) -> IoTask<void>;
    auto connect() -> IoTask<void>;
    auto makeHeaders() -> std::string;
    
    BufferedStream<> mStream; // Low level stream
    HttpHeaders      mHeaders; // The extra headers for handshake
    Url              mUrl; // The url to connect
    std::vector<std::string> mProtocols; // The protocols to use

    // Send & Recv state
    bool             mRecvFrameFin = true; // The current received frame is marked as fin
    size_t           mRemaingPayloadLen = 0; // The remaining payload length to receive
    std::optional<CloseEvent> mCloseEvent; // The close event
    std::string      mSecWebSocketKey; // The Sec-WebSocket-Key header

#if !defined(ILIAS_NO_SSL)
    std::optional<SslContext> mSslContext;
#endif

};

inline WebSocket::WebSocket() = default;
inline WebSocket::WebSocket(const Url &url, const HttpHeaders &headers)
    : mHeaders(headers), mUrl(url) 
{
}

inline auto WebSocket::open() -> IoTask<void> {
    if (!mStream) { // No User specified stream, create by default
        if (auto res = co_await connect(); !res) {
            co_return res;
        }
    }
    // TODO: Split HTTP1.1 common into a separate file?
    const auto headers = makeHeaders();
    if (const auto res = co_await mStream.writeAll(makeBuffer(headers)); res != headers.size()) {
        co_return Unexpected(res.error_or(Error::ConnectionAborted));
    }
    // Read the response
    // First line MUST be 101 Switching Protocols
    auto line = co_await mStream.getline("\r\n");
    if (!line || line != "HTTP/1.1 101 Switching Protocols") {
        co_return Unexpected(line.error_or(Error::WebSocketBadHandshake));
    }
    HttpHeaders replyHeaders;
    do {
        line = co_await mStream.getline("\r\n");
        if (!line) {
            co_return Unexpected(line.error());
        }
        if (line->empty()) {
            break;
        }
        // Find :
        auto view = std::string_view(*line);
        auto delim = view.find(": ");
        if (delim == std::string::npos) {
            co_return Unexpected(Error::WebSocketBadHandshake);
        }
        auto key = view.substr(0, delim);
        auto value = view.substr(delim + 2);
        replyHeaders.append(key, value);
    }
    while (true);
    // Check the headers
    if (mem::strcasecmp(replyHeaders.value("Upgrade"), "websocket") != std::strong_ordering::equal) {
        co_return Unexpected(Error::WebSocketBadHandshake);
    }
    if (mem::strcasecmp(replyHeaders.value("Connection"), "Upgrade") != std::strong_ordering::equal) {
        co_return Unexpected(Error::WebSocketBadHandshake);
    }

#if !defined(ILIAS_NO_CRYPTOHASH)
    auto expectedKey = base64::encode(
        CryptoHash::hash(
            makeBuffer(mSecWebSocketKey + MagicKey.data()), 
            CryptoHash::Sha1
        )
    );
    if (auto key = replyHeaders.value("Sec-WebSocket-Accept"); key != expectedKey) {
        ILIAS_ERROR("WebSocket", "Expected key: {}, got: {}", expectedKey, key);
        co_return Unexpected(Error::WebSocketBadHandshake);
    }
#endif // !defined(ILIAS_NO_CRYPTOHASH)
    ILIAS_TRACE("WebSocket", "Handshake complete");
    co_return {};
}

inline auto WebSocket::shutdown(uint16_t code, std::string_view message) -> IoTask<void> {
    ILIAS_TRACE("WebSocket", "Closing with code: {}, message: {}", code, message);
    ILIAS_ASSERT(message.size() < 123); // Must less than the biggest payload length - status code size
    detail::WsFrame frame {
        .fin = 1,
        .opcode = detail::WsFrame::Close,
    };

    // Build the payload
    std::byte buffer[125];
    size_t bufferLen = sizeof(code); // Minimum size is the code
    code = hostToNetwork(code);
    ::memcpy(buffer, &code, sizeof(code));
    if (!message.empty()) {
        bufferLen += message.size();
        ::memcpy(buffer + sizeof(code), message.data(), message.size());
    }
    if (auto res = co_await writeFrame(frame, makeBuffer(buffer, bufferLen)); !res) {
        co_return Unexpected(res.error());
    }
    ILIAS_TRACE("WebSocket", "Close frame sent");
    co_return co_await mStream.shutdown();
}

inline auto WebSocket::setStream(DynStreamClient stream) -> void {
    mStream = std::move(stream);
}

inline auto WebSocket::setUrl(const Url &url) -> void {
    mUrl = url;
}

inline auto WebSocket::setHeaders(const HttpHeaders &headers) -> void {
    mHeaders = headers;
}

inline auto WebSocket::sendMessageChunk(std::span<const std::byte> buffer, MessageType type, FinalFlag fin) -> IoTask<void> {
    detail::WsFrame frame {
        .fin = uint8_t(fin),
        .opcode = uint8_t(type),
    };
    return writeFrame(frame, buffer);
}

inline auto WebSocket::sendMessage(std::span<const std::byte> data, MessageType type) -> IoTask<void> {
    return sendMessageChunk(data, type, Final);
}

inline auto WebSocket::sendMessage(std::string_view data) -> IoTask<void> {
    return sendMessageChunk(makeBuffer(data), TextMessage, Final);
}

inline auto WebSocket::recvMessageBegin() -> IoTask<MessageType> {
    if (!mRecvFrameFin || mRemaingPayloadLen != 0) { // The last message is not finished, so we can't read another one
        co_return Unexpected(Error::InvalidArgument);
    }
    // Read the header
    while (true) {
        detail::WsFrame frame { };
        size_t payloadLen = 0;
        if (auto res = co_await readFrame(frame, payloadLen); !res) {
            co_return Unexpected(res.error());
        }
        mRemaingPayloadLen = payloadLen;
        mRecvFrameFin = frame.fin;
        switch (frame.opcode) { 
            case detail::WsFrame::Text: co_return TextMessage;
            case detail::WsFrame::Binary: co_return BinaryMessage;
            default: co_return Unexpected(Error::WebSocketBadFrame); // Invalid opcode
        }
    }
}

inline auto WebSocket::recvMessageChunk(std::span<std::byte> buffer) -> IoTask<size_t> {
    if (mRecvFrameFin && mRemaingPayloadLen == 0) { // The last message is finished, so we can't read another one
        co_return 0;
    }
    size_t bufferLength = buffer.size();
    while (!buffer.empty()) {
        if (mRemaingPayloadLen != 0) { // Still has some data to read
            auto len = std::min(mRemaingPayloadLen, buffer.size());
            if (auto res = co_await mStream.readAll(buffer.subspan(0, len)); res != len) {
                co_return Unexpected(res.error_or(Error::ConnectionAborted));
            }
            buffer = buffer.subspan(len); // Add the length of the data we have read
            mRemaingPayloadLen -= len; 
        }
        if (mRemaingPayloadLen == 0 || buffer.empty()) { // The whole message is received or the buffer is full
            co_return bufferLength - buffer.size(); // return How many bytes we have read
        }
        // Not the whole message is received, so we need to read the next frame
        detail::WsFrame frame { };
        size_t payloadLen = 0;
        if (auto res = co_await readFrame(frame, payloadLen); !res) {
            co_return Unexpected(res.error());
        }
        if (frame.opcode != detail::WsFrame::Continuation) { // Not fin, the next data frame must be a continuation frame
            co_return Unexpected(Error::WebSocketBadFrame);
        }
        mRemaingPayloadLen = payloadLen;
        mRecvFrameFin = frame.fin;
    }
}

template <MemContainer T>
inline auto WebSocket::recvMessage() -> IoTask<std::pair<T, MessageType>> {
    auto type = co_await recvMessageBegin();
    if (!type) {
        co_return Unexpected(type.error());
    }
    T container;
    container.resize(mRemaingPayloadLen); // Resize the container to the expected size
    size_t got = 0;
    auto len = co_await recvMessageChunk(makeBuffer(container).subspan(got));
    while (len && len != 0) {
        got += len.value();
        len = co_await recvMessageChunk(makeBuffer(container).subspan(got));
        if (len != 0 && got == container.size()) { // Buffer is full and still has data
            container.resize(container.size() + mRemaingPayloadLen);
        }
    }
    if (!len) {
        co_return Unexpected(len.error());
    }
    container.resize(got); // Truncate the container to the actual size
    co_return std::make_pair(std::move(container), *type);
}

inline auto WebSocket::closeEvent() const -> const std::optional<CloseEvent> & {
    return mCloseEvent;
}

inline auto WebSocket::readFrame(detail::WsFrame &frame, size_t &payloadLen) -> IoTask<void> {
    while (true) {
        std::byte rawFrame[2] { };
        if (auto res = co_await mStream.readAll(rawFrame); res != sizeof(rawFrame)) {
            co_return Unexpected(res.error_or(Error::ConnectionAborted));
        }
        // Parse the frame
        uint8_t byte1 = std::to_integer<uint8_t>(rawFrame[0]);
        uint8_t byte2 = std::to_integer<uint8_t>(rawFrame[1]);
        frame.fin        = (byte1 & 0x80) >> 7;
        frame.rsv1       = (byte1 & 0x40) >> 6;
        frame.rsv2       = (byte1 & 0x20) >> 5;
        frame.rsv3       = (byte1 & 0x10) >> 4;
        frame.opcode     = (byte1 & 0x0F);
        frame.mask       = (byte2 & 0x80) >> 7;
        frame.payloadLen = (byte2 & 0x7F);
        // Check frame
        if (frame.mask || frame.rsv1 || frame.rsv2 || frame.rsv3) { // Server must not mask the frame
            co_return Unexpected(Error::WebSocketBadFrame);
        }
        switch (frame.payloadLen) {
            case 126: {
                // Still need to read 2 bytes
                uint16_t len = 0;
                if (auto res = co_await mStream.readAll(makeBuffer(&len, sizeof(len))); res != sizeof(len)) {
                    co_return Unexpected(res.error_or(Error::ConnectionAborted));
                }
                payloadLen = networkToHost(len);
                break;
            }
            case 127: {
                // Still need to read 8 bytes
                uint64_t len = 0;
                if (auto res = co_await mStream.readAll(makeBuffer(&len, sizeof(len))); res != sizeof(len)) {
                    co_return Unexpected(res.error_or(Error::ConnectionAborted));
                }
                payloadLen = networkToHost(len);
                break;
            }
            default: {
                ILIAS_ASSERT(frame.payloadLen < 126); // 126 and 127 are used for extended payload length
                payloadLen = frame.payloadLen;
                break;
            }
        }
        ILIAS_TRACE("WebSocket", "Received frame: fin={}, opcode={}, payloadLen={}", bool(frame.fin), int(frame.opcode), payloadLen);
        // Process if is control frame
        // TODO: Handle control frames, such as ping, pong, close
        switch (frame.opcode) {
            case detail::WsFrame::Continuation: {
                if (frame.fin) {
                    ILIAS_ERROR("WebSocket", "Received a continuation frame with fin set, bad frame");
                    co_return Unexpected(Error::WebSocketBadFrame);
                }
                [[fallthrough]];
            }
            case detail::WsFrame::Text:
            case detail::WsFrame::Binary: co_return {};
            case detail::WsFrame::Ping: {
                ILIAS_INFO("WebSocket", "Received a ping frame");
                auto data = std::make_unique<std::byte[]>(payloadLen);
                auto buf = makeBuffer(data.get(), payloadLen);
                if (auto res = co_await mStream.readAll(buf); res != payloadLen) {
                    co_return Unexpected(res.error_or(Error::WebSocketClosed));
                }
                // Receive a ping frame, send a pong frame
                detail::WsFrame pongFrame {
                    .fin    = 1,
                    .opcode = detail::WsFrame::Pong,
                };
                if (auto res = co_await writeFrame(pongFrame, buf); !res) {
                    co_return Unexpected(res.error());
                }
                continue;
            }
            case detail::WsFrame::Pong: {
                ILIAS_INFO("WebSocket", "Received a pong frame");
                auto data = std::make_unique<std::byte[]>(payloadLen);
                auto buf = makeBuffer(data.get(), payloadLen);
                if (auto res = co_await mStream.readAll(buf); res != payloadLen) {
                    co_return Unexpected(res.error_or(Error::WebSocketClosed));
                }
                // Check data
                for (size_t i = 0; i < payloadLen; i++) {
                    if (data[i] == std::byte {114} ) {
                        continue;
                    }
                    co_await shutdown(InvalidFramePayloadData);
                    co_return Unexpected(Error::WebSocketClosed);
                }
                continue;
            }
            case detail::WsFrame::Close: {
                // Parse the close frame
                ILIAS_INFO("WebSocket", "Received a close frame");
                mCloseEvent.emplace();
                if (payloadLen < 2) {
                    // Assume the close code is 1005
                    payloadLen = 2;
                    co_return Unexpected(Error::WebSocketClosed);
                }
                uint16_t code = 0;
                if (auto res = co_await mStream.readAll(makeBuffer(&code, sizeof(code))); res != sizeof(code)) { // Try to read the close code
                    co_return Unexpected(res.error_or(Error::WebSocketClosed));
                }
                mCloseEvent->code = networkToHost(code);
                payloadLen -= 2;
                if (payloadLen > 0) { // Read the close message
                    mCloseEvent->message.resize(payloadLen);
                    if (auto res = co_await mStream.readAll(makeBuffer(mCloseEvent->message)); res != payloadLen) {
                        mCloseEvent->message.clear();
                        co_return Unexpected(res.error_or(Error::WebSocketClosed));
                    }
                }
                // Check if is finished
#if 1
                std::byte buf[1];
                if (auto res = co_await mStream.read(buf); res != 0) {
                    ILIAS_ERROR("WebSocket", "Received more than 1 byte after close frame");
                    co_return Unexpected(res.error_or(Error::WebSocketClosed));
                }
#endif
                mCloseEvent->wasClean = true; // Successfully received the close frame
                co_return Unexpected(Error::WebSocketClosed);
            }
            default: co_return Unexpected(Error::WebSocketBadFrame); // Invalid opcode
        }
    }
}

inline auto WebSocket::writeFrame(detail::WsFrame frame, std::span<const std::byte> buffer) -> IoTask<void> {
    std::byte frameBuffer[sizeof(detail::WsFrame) + 8 + 4] { }; // Header + max payload length + mask
    size_t frameSize = sizeof(detail::WsFrame);
    // Build the payload length
    if (buffer.size() > std::numeric_limits<uint16_t>::max()) { // Use 64 bit length
        frame.payloadLen = 127;
        frameSize += sizeof(uint64_t);
        *reinterpret_cast<uint64_t *>(frameBuffer + sizeof(detail::WsFrame)) = hostToNetwork(uint64_t(buffer.size()));
    }
    else if (buffer.size() >= 126) { // Use 16 bit length
        frame.payloadLen = 126;
        frameSize += sizeof(uint16_t);
        *reinterpret_cast<uint16_t *>(frameBuffer + sizeof(detail::WsFrame)) = hostToNetwork(uint16_t(buffer.size()));
    }
    else { // Use 7 bit length
        frame.payloadLen = uint8_t(buffer.size());
    }
    frame.mask = 1; // Client must mask the frame
    // Build the header
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    byte1 |= frame.fin << 7;
    byte1 |= frame.rsv1 << 6;
    byte1 |= frame.rsv2 << 5;
    byte1 |= frame.rsv3 << 4;
    byte1 |= frame.opcode;
    byte2 |= frame.mask << 7;
    byte2 |= frame.payloadLen;
    frameBuffer[0] = std::byte(byte1);
    frameBuffer[1] = std::byte(byte2);
    // Write the mask? We use 0, 0, 0, 0 as mask to simplify the code
    *reinterpret_cast<uint32_t *>(frameBuffer + frameSize) = 0;
    frameSize += 4;
    
    // Write the header
    ILIAS_TRACE("WebSocket", "Sending frame: fin={}, opcode={}, payloadLen={}", bool(frame.fin), int(frame.opcode), buffer.size());
    if (const auto res = co_await mStream.writeAll(makeBuffer(frameBuffer, frameSize)); res != frameSize) {
        co_return Unexpected(res.error_or(Error::ConnectionAborted));
    }
    // Write the payload
    if (const auto res = co_await mStream.writeAll(buffer); res != buffer.size()) {
        co_return Unexpected(res.error_or(Error::ConnectionAborted));
    }
    co_return {};
}

inline auto WebSocket::connect() -> IoTask<void> {
    auto stream = DynStreamClient { };
    auto port = mUrl.port();
    auto host = std::string(mUrl.host());
    auto scheme = std::string(mUrl.scheme());
    if (!port) {
        if (scheme == "ws") {
            port = 80;
        }
        else if (scheme == "wss") {
            port = 443;
        }
        else {
            co_return Unexpected(Error::ProtocolNotSupported);
        }
    }
    auto info = co_await AddressInfo::fromHostnameAsync(host.c_str(), std::to_string(*port).c_str());
    if (!info) {
        co_return Unexpected(info.error());
    }
    auto endpoints = info->endpoints();
    for (size_t idx = 0; idx < endpoints.size(); ++idx) {
        auto client = co_await TcpClient::make(endpoints[idx].family());
        if (!client) {
            co_return Unexpected(client.error());
        }
        if (auto res = co_await client->connect(endpoints[idx]); !res) {
            if (res.error() == Error::Canceled) {
                co_return res;
            }
            if (idx == endpoints.size() - 1) { // The Last endpoint, failed to connect
                co_return res;
            }
            continue;
        }
        stream = std::move(*client);
        break;
    }
    ILIAS_ASSERT(stream);
    // Check if the stream requires SSL
    if (scheme == "wss") {
#if !defined(ILIAS_NO_SSL)
        mSslContext.emplace();
        SslClient sslClient(*mSslContext, std::move(stream));
        sslClient.setHostname(host);
        if (auto res = co_await sslClient.handshake(); !res) {
            co_return res;
        }
        stream = std::move(sslClient);
#else
        co_return Unexpected(Error::ProtocolNotSupported);
#endif // !defined(ILIAS_NO_SSL)    
    }
    mStream = std::move(stream);
    co_return {};
}

inline auto WebSocket::makeHeaders() -> std::string {
    std::string headersBuf;
    std::string path(mUrl.path());
    std::string host(mUrl.host());
    if (auto query = mUrl.query(); !query.empty()) {
        path += "?";
        path += query;
    }
    sprintfTo(headersBuf, "GET %s HTTP/1.1\r\n", path.c_str());
    sprintfTo(headersBuf, "Host: %s\r\n", host.c_str());
    for (const auto &[key, value] : mHeaders) { //  Add user specified headers
        sprintfTo(headersBuf, "%s: %s\r\n", key.c_str(), value.c_str());
    }
    if (const auto origin = mHeaders.value("Origin"); origin.empty()) { // Add origin if not specified
        headersBuf += "Origin: ";
        headersBuf += mUrl.scheme();
        headersBuf += "://";
        headersBuf += mUrl.host();
        headersBuf += "\r\n";
    }
    headersBuf += "Upgrade: websocket\r\n";
    headersBuf += "Connection: Upgrade\r\n";

    // Begin the WebSocket headers
    std::byte key[16];
    for (auto &k : key) {
        k = std::byte(::rand() % 256);
    }

    mSecWebSocketKey = base64::encode(key);
    headersBuf += "Sec-WebSocket-Key: ";
    headersBuf += mSecWebSocketKey;
    headersBuf += "\r\n";
    if (!mProtocols.empty()) { // Add protocols if any
        headersBuf += "Sec-WebSocket-Protocol: ";
        for (const auto &proto : mProtocols) {
            headersBuf += proto;
            headersBuf += ", ";
        }
        headersBuf.pop_back();
        headersBuf.pop_back();
        headersBuf += "\r\n";
    }
    headersBuf += "Sec-WebSocket-Version: 13\r\n";
    headersBuf += "\r\n";
    return headersBuf;
}

ILIAS_NS_END
