#pragma once

#include "headers.hpp"
#include <cctype>
#include <string>
#include <array>
#include <deque>
#include <span>

ILIAS_NS_BEGIN

using HpackFieldView = std::pair<std::string_view, std::string_view>;
using HpackField     = std::pair<std::string, std::string>;

/**
 * @brief The static table by RFC7541
 * 
 */

inline constexpr HpackFieldView HpackStaticTable[] = {
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"warning", ""},
    {"www-authenticate", ""}
};
static_assert(sizeof(HpackStaticTable) / sizeof(HpackFieldView) == 61, "HpackStaticTable size mismatch with RFC");

/**
 * @brief The packer for decode or encode with HPACK
 * 
 */
class Hpack {
public:
    Hpack() = default;
    Hpack(const Hpack &) = default;
    Hpack(Hpack &&) = default;
    ~Hpack() = default;


    auto resizeTable(size_t size) -> void;
    auto decodeHeadersTo(std::span<const std::byte> view, HttpHeaders &headers) -> bool;

    /**
     * @brief Encode header to the output
     * 
     * @param headers 
     * @param output 
     * @return true 
     * @return false 
     */
    auto encodeHeadersTo(const HttpHeaders &headers, std::span<std::byte> &output) -> bool;

    /**
     * @brief Get the space of the encoded headers
     * 
     * @param headers 
     * @return size_t 
     */
    auto encodeHeadersSize(const HttpHeaders &headers) const -> size_t;
private:
    auto _push(HpackFieldView view) -> void {
        size_t size = view.first.size() + view.second.size();
        while (mSize + size > mMaxSize) {
            _pop();
        }
        mDatas.emplace_front(view);
        mSize += size;
    }
    auto _pop() -> void {
        if (mDatas.empty()) {
            return;
        }
        
        auto &[key, value] = mDatas.back();
        mSize -= (key.size() + value.size());
        mDatas.pop_back();
    }

    //< The dynamic table
    std::deque<HpackField> mDatas;
    size_t                 mSize = 0;
    size_t                 mMaxSize = 0;
};

inline auto Hpack::resizeTable(size_t size) -> void {
    mMaxSize = size;
    while (mSize > mMaxSize) {
        _pop();
    }
}

ILIAS_NS_END