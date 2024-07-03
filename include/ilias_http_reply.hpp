#pragma once

#include "ilias_http_headers.hpp"
#include "ilias_http_transfer.hpp"
#include "ilias_async.hpp"
#include "ilias_zlib.hpp"

ILIAS_NS_BEGIN

/**
 * @brief Reply class
 * 
 */
class HttpReply {
public:
    HttpReply(const HttpReply &) = delete;
    HttpReply(HttpReply &&);
    ~HttpReply();

    /**
     * @brief Get the reply utf8 text
     * 
     * @return Task<std::string> 
     */
    auto text() -> Task<std::string>;
    /**
     * @brief Get the reply raw binrary content
     * 
     * @return Task<std::vector<std::byte> > 
     */
    auto content() -> Task<std::vector<std::byte> >;
    /**
     * @brief Get the reply status code like (200, 404, etc..)
     * 
     * @return int 
     */
    auto statusCode() const -> int;
    /**
     * @brief Get the reply status string like (OK, Continue)
     * 
     * @return std::string_view 
     */
    auto status() const -> std::string_view;
    /**
     * @brief Get the reply headers
     * 
     * @return const HttpHeaders& 
     */
    auto headers() const -> const HttpHeaders &;

    /**
     * @brief Get how many time we transfered
     * 
     * @return std::chrono::milliseconds 
     */
    auto transferDuration() const -> std::chrono::milliseconds;

    /**
     * @brief Recv content from server (used in stream mode)
     * 
     * @param buffer 
     * @param n 
     * @return Task<size_t> 
     */
    auto recv(void *buffer, size_t n) -> Task<size_t>;

    /**
     * @brief Recv all the content from the server (used in stream mode)
     * 
     * @tparam T 
     * @return Task<T> 
     */
    template <typename T = std::vector<std::byte> >
    auto recvAll() -> Task<T>;

    /**
     * @brief Assign an http reply
     * 
     * @return HttpReply& 
     */
    auto operator =(const HttpReply &) -> HttpReply & = delete;
    auto operator =(HttpReply &&) -> HttpReply &;

    /**
     * @brief Initalize the HttpReply from a HttpStream
     * 
     * @param url The request url
     * @param stream The http stream
     * @param streamMode Did we need read all reply in place?, if false, we will do it
     * @return Task<HttpReply> 
     */
    static auto from(const Url &url, std::unique_ptr<HttpStream> stream, bool streamMode = false) -> Task<HttpReply>;
private:
    HttpReply();

    Url mUrl;
    int mStatusCode = 0;
    bool mStreamMode = false;
    std::string mStatus;
    std::vector<std::byte> mContent; //< The Content, empty on stream Mode
    HttpHeaders mRequestHeaders;
    HttpHeaders mResponseHeaders;
    std::chrono::milliseconds mTransferDuration;
    std::unique_ptr<HttpStream> mStream;
friend class HttpSession;
};

// --- HttpReply
inline HttpReply::HttpReply(HttpReply &&) = default;
inline HttpReply::HttpReply() = default;
inline HttpReply::~HttpReply() = default;

inline auto HttpReply::text() -> Task<std::string> {
    if (!mContent.empty()) {
        // Not Stream mode, just copy
        co_return std::string(reinterpret_cast<char*>(mContent.data()), mContent.size());
    }
    co_return co_await recvAll<std::string>();
}
inline auto HttpReply::content() -> Task<std::vector<std::byte> > {
    if (!mContent.empty()) {
        // Not Stream mode, just copy
        co_return mContent;
    }
    co_return co_await recvAll<std::vector<std::byte> >();
}
inline auto HttpReply::statusCode() const -> int {
    return mStatusCode;
}
inline auto HttpReply::status() const -> std::string_view {
    return mStatus;
}
inline auto HttpReply::headers() const -> const HttpHeaders & {
    return mResponseHeaders;
}
inline auto HttpReply::transferDuration() const -> std::chrono::milliseconds {
    return mTransferDuration;
}
inline auto HttpReply::operator =(HttpReply &&) -> HttpReply & = default;

template <typename T>
inline auto HttpReply::recvAll() -> Task<T> {
    // TODO : Uncompress if needed
    if (!mStream) {
        co_return T();
    }

    T buf;
    buf.resize(1024);
    size_t curPos = 0;
    while (true) {
        auto target = std::span(buf).subspan(curPos);
        if (target.empty()) {
            buf.resize(buf.size() * 1.5);
            continue;
        }
        auto num = co_await mStream->recvContent(std::as_writable_bytes(target));
        if (!num) {
            co_return Unexpected(num.error());
        }
        curPos += *num;
        if (*num == 0) {
            // EOF
            mStream.reset();
            break;
        }
    }

#if !defined(ILIAS_NO_ZLIB)
    if (auto encoding = mResponseHeaders.value(HttpHeaders::ContentEncoding); encoding == "gzip") {
        buf = Zlib::decompress<T>(std::as_bytes(std::span(buf)), Zlib::GzipFormat);
    }
    else if (encoding == "deflate") {
        buf = Zlib::decompress<T>(std::as_bytes(std::span(buf)), Zlib::DeflateFormat);
    }
#endif
    co_return buf;
}

inline auto HttpReply::from(const Url &url, std::unique_ptr<HttpStream> stream, bool streamMode) -> Task<HttpReply> {
    HttpReply reply;
    reply.mUrl = url;
    reply.mStream = std::move(stream);

    // First recv header
    if (auto val = co_await reply.mStream->recvHeaders(reply.mStatusCode, reply.mStatus, reply.mResponseHeaders); !val) {
        co_return Unexpected(val.error());
    }

    // Check if stream mode
    if (!streamMode) {
        auto vec = co_await reply.recvAll();
        if (!vec) {
            co_return Unexpected(vec.error());
        }
        reply.mContent = std::move(*vec);
    }
    co_return reply;
}

ILIAS_NS_END 