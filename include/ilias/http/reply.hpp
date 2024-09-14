#pragma once

#include <ilias/http/transfer.hpp>
#include <ilias/http/headers.hpp>
#include <ilias/task/task.hpp>
#include <ilias/io/traits.hpp>
#include <ilias/io/method.hpp>
#include <ilias/buffer.hpp>
#include <ilias/zlib.hpp>
#include <ilias/url.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The Http Reply
 * 
 */
class HttpReply final : public ReadableMethod<HttpReply> {
public:
    HttpReply() = default;
    HttpReply(const HttpReply &) = delete;
    HttpReply(HttpReply &&) = default;
    ~HttpReply() = default;

    using ReadableMethod<HttpReply>::readAll;

    /**
     * @brief Read the reply body from the stream
     * 
     * @param buffer 
     * @return Task<size_t> 
     */
    auto read(std::span<std::byte> buffer) -> Task<size_t>;

    /**
     * @brief Read all the reply body from the stream (not recommended for large body, for large body, please use for + read until zero)
     * 
     * @tparam T 
     * @return Task<T> 
     */
    template <typename T>
    auto readAll() -> Task<T>;

    /**
     * @brief Get the Reply's content (not recommended for large body, see readAll)
     * 
     * @return Task<std::vector<std::byte> > 
     */
    auto content() -> Task<std::vector<std::byte> >;

    /**
     * @brief Get the Reply's content as a string (not recommended for large body, see readAll)
     * 
     * @return Task<std::string> 
     */
    auto text() -> Task<std::string>;

    /**
     * @brief Get the Reply's url (may not same as the request's url because of redirects)
     * 
     * @return const Url& 
     */
    auto url() const -> const Url& { return mUrl; }

    /**
     * @brief Get the Reply's status message
     * 
     * @return const std::string& 
     */
    auto status() const -> const std::string & { return mStatus; }

    /**
     * @brief Get the Reply's status code
     * 
     * @return int 
     */
    auto statusCode() const -> int { return mStatusCode; }

    /**
     * @brief Get the Reply's headers
     * 
     * @return const HttpHeaders& 
     */
    auto headers() const -> const HttpHeaders& { return mHeaders; }                   

    auto operator =(const HttpReply &) -> HttpReply & = delete;

    auto operator =(HttpReply &&) -> HttpReply & = default;

    /**
     * @brief Construct a reply from the raw the HttpStream stream
     * 
     * @param stream 
     * @param streamMode 
     * @return Task<HttpReply> 
     */
    static auto make(std::unique_ptr<HttpStream> stream, bool streamMode) -> Task<HttpReply>;
private:
    Url mUrl; 
    int mStatusCode = 0;
    std::string mStatus;
    HttpRequest mRequest; //< The request that generated this reply
    HttpHeaders mHeaders; //< The received headers of the reply
    std::optional<Error> mLastError; //< The last error that occurred while reading the reply
    std::vector<std::byte> mContent; //< The received content of the reply (not in stream mode)
    std::unique_ptr<HttpStream> mStream; //< The stream used to read the whole reply
friend class HttpSession;
};

inline auto HttpReply::make(std::unique_ptr<HttpStream> stream, bool streamMode) -> Task<HttpReply> {
    ILIAS_ASSERT(stream);

    HttpReply reply;
    if (auto ret = co_await stream->readHeaders(reply.mStatusCode, reply.mStatus, reply.mHeaders); !ret) {
        co_return Unexpected(ret.error());
    }
    reply.mStream = std::move(stream);
    reply.mUrl = reply.mRequest.url();
    if (!streamMode) {
        auto ret = co_await reply.readAll<std::vector<std::byte> >();
        if (!ret) { 
            co_return Unexpected(ret.error()); 
        }
        reply.mContent = std::move(*ret);

#if !defined(ILIAS_NO_ZLIB)
        auto contentEncoding = reply.mHeaders.value("Content-Encoding");
        std::optional<zlib::ZFormat> format;
        if (contentEncoding == "gzip") {
            format = zlib::GzipFormat;
        }
        else if (contentEncoding == "deflate") {
            format = zlib::DeflateFormat;
        }
        if (format) {
            ILIAS_TRACE("Http", "Decompressing content by format: {}", contentEncoding);
            auto ret = zlib::decompress(makeBuffer(reply.mContent), *format);
            if (!ret) {
                co_return Unexpected(ret.error());
            }
            reply.mContent = std::move(*ret);
        }
#endif

    }
    co_return reply;
}

inline auto HttpReply::read(std::span<std::byte> buffer) -> Task<size_t> {
    if (!mStream) { //< All content has been read
        if (mLastError) {
            co_return Unexpected(*mLastError);
        }
        co_return 0;
    }
    auto ret = co_await mStream->read(buffer);
    // TODO: Handle decompression here, not in the make function



    if (!ret) {
        mLastError = ret.error();
    }
    if (!ret || *ret == 0) { //< Error or EOF
        mStream.reset();
    }
    co_return ret;
}

inline auto HttpReply::content() -> Task<std::vector<std::byte> > {
    if (!mContent.empty()) {
        co_return mContent;
    }
    co_return co_await readAll<std::vector<std::byte> >();
}

inline auto HttpReply::text() -> Task<std::string> {
    if (!mContent.empty()) {
        auto sv = std::string_view(
            reinterpret_cast<const char *>(mContent.data()),
            mContent.size()
        );
        co_return std::string(sv);
    }
    co_return co_await readAll<std::string>();
}

template <typename T>
inline auto HttpReply::readAll() -> Task<T> {
    size_t readed = 0;
    T data;
    data.resize(1024);
    while (true) {
        auto buf = makeBuffer(data).subspan(readed);
        if (buf.empty()) {
            data.resize(data.size() * 2);
            continue;
        }
        auto ret = co_await read(buf);
        if (!ret) {
            co_return Unexpected(ret.error());
        }
        readed += *ret;
        if (*ret == 0) { //< EOF
            break;
        }
    }
    data.resize(readed);
    co_return data;
}

ILIAS_NS_END