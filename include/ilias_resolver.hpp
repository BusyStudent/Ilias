#pragma once

#include "ilias.hpp"
#include "ilias_async.hpp"
#include "ilias_expected.hpp"

#include <chrono>
#include <vector>

ILIAS_NS_BEGIN

class DnsNameParser;
class DnsResponse;
class DnsHeader;
class DnsQuery;

/**
 * @brief A struct for common dns query / response header
 * 
 */
class DnsHeader {
public:
    uint16_t id;       //< identification number
    uint8_t rd :1;     //< recursion desired
    uint8_t tc :1;     //< truncated message
    uint8_t aa :1;     //< authoritive answer
    uint8_t opcode :4; //< purpose of message
    uint8_t qr :1;     //< query/response flag
    uint8_t rcode :4;  //< response code
    uint8_t cd :1;     //< checking disabled
    uint8_t ad :1;     //< authenticated data
    uint8_t z :1;      //< its z! reserved
    uint8_t ra :1;     //< recursion available
    uint16_t questionCount;  //< number of question entries
    uint16_t answerCount; //< number of answer entries
    uint16_t authCount; //< number of authority entries
    uint16_t resCount; //< number of resource entries
};
static_assert(sizeof(DnsHeader) == 12, "DnsHeader size mismatch");

/**
 * @brief A Dns Query
 * 
 */
class DnsQuery {
public:
    enum Type : uint16_t {
        A    = 1,
        AAAA = 28,
        CNAME = 5,
    };

    DnsQuery() = default;
    /**
     * @brief Construct a new Dns Query object
     * 
     * @param hostname The hostname want to query
     * @param type The record type you want to get (default in A)
     */
    DnsQuery(const char *hostname, uint16_t type = A);
    DnsQuery(const DnsQuery &) = default;
    ~DnsQuery();

    /**
     * @brief Fill the query request data to buffer
     * 
     * @return true 
     * @return false 
     */
    bool fillBuffer(uint16_t transId, void *buffer, size_t n) const;
    bool fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const;
    /**
     * @brief Get the size of buffer needed to fill the query request data
     * 
     * @return size_t
     */
    size_t fillBufferSize() const noexcept;
private:
    std::string mHostname;     //< Human readable name, www.google.com
    std::string mEncodedName;  //< Encoded name, 3www6google3com0
    uint16_t    mType = A;
};
/**
 * @brief Dns answer wrapper
 * 
 */
class DnsAnswer {
public:
    enum Type : uint16_t {
        A    = 1,
        AAAA = 28,
        CNAME = 5,
    };
    /**
     * @brief Construct a new Dns Answer object
     * 
     */
    DnsAnswer() = default;
    DnsAnswer(const DnsAnswer &) = default;
    ~DnsAnswer() = default;
    /**
     * @brief Get the name of the answer
     * 
     * @return std::string 
     */
    std::string name() const;
    /**
     * @brief Get the type of the answer
     * 
     * @return uint16_t 
     */
    uint16_t type() const noexcept;
    /**
     * @brief Get the class of the answer
     * 
     * @return uint16_t 
     */
    uint16_t class_() const noexcept;
    /**
     * @brief Get the TTL of the answer (in second)
     * 
     * @return uint32_t 
     */
    uint32_t ttl() const noexcept;
    /**
     * @brief Get the data len
     * 
     * @return size_t 
     */
    size_t dataLength() const noexcept;
    /**
     * @brief Get the data
     * 
     * @return const void* 
     */
    const void *data() const noexcept;
    /**
     * @brief Try Get the cname (type must be CNAME)
     * 
     * @return std::string 
     */
    std::string cname() const;
    /**
     * @brief Try Get the address (type must be A or AAAA)
     * 
     * @return IPAddress 
     */
    IPAddress address() const;
private:
    std::string mName;
    uint16_t mType = 0;
    uint16_t mClass = 0;
    uint32_t mTTL = 0;
    std::string mData;
friend class DnsResponse;
};
/**
 * @brief A Dns Response
 * 
 */
class DnsResponse {
public:
    DnsResponse() = default;
    DnsResponse(const DnsResponse &) = default;
    DnsResponse(DnsResponse &&) = default;
    ~DnsResponse() = default;

    /**
     * @brief Is the query succeed?
     * 
     * @return true 
     * @return false 
     */
    bool isOk() const noexcept;
    /**
     * @brief Get the trans Id
     * 
     * @return uint16_t 
     */
    uint16_t transId() const noexcept;
    /**
     * @brief Get the answer count
     * 
     * @return uint16_t 
     */
    uint16_t answerCount() const noexcept;
    /**
     * @brief Get the cname
     * 
     * @return std::string 
     */
    std::string cname() const;
    /**
     * @brief Get the response's all A or AAAA records
     * 
     * @return std::vector<IPAddress> 
     */
    auto addresses() const -> std::vector<IPAddress>;
    /**
     * @brief Get the response answers
     * 
     * @return const std::vector<DnsAnswer>& 
     */
    auto answers() const -> const std::vector<DnsAnswer> &;
    /**
     * @brief Parse the response from memory
     * 
     * @param buffer 
     * @param n 
     * @return Expected<DnsResponse, size_t> 
     */
    static auto parse(const void *buffer, size_t n) -> Expected<DnsResponse, size_t>;
private:
    bool _parse(const uint8_t *buffer, size_t size, size_t *stopPosition);
    bool _skipQuestion(const uint8_t *buffer, size_t size, const uint8_t **cur);
    bool _skipName(const uint8_t *buffer, size_t size, const uint8_t **name);
    bool _unpackName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output);
    bool _parseAnswer(const uint8_t *buffer, size_t size, const uint8_t **answer, DnsAnswer &output);
    bool _parseName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output);

    DnsHeader mHeader { };
    std::vector<DnsAnswer> mAnswers;
};

/**
 * @brief A Resolver for manage query and response
 * 
 */
class Resolver {
public:
    using Query = DnsQuery;

    Resolver(IOContext &);
    Resolver(const Resolver &) = delete;
    ~Resolver();
private:
    IOContext *mCtxt = nullptr;
};

// --- DnsQuery Impl
inline DnsQuery::DnsQuery(const char *name, uint16_t type) : mHostname(name), mType(type) {
    // Split string by . and encode to 3www6google3com0 like string
    size_t prev = 0;
    size_t pos = mHostname.find('.');
    while (pos != mHostname.npos) {
        mEncodedName += char(pos - prev);
        mEncodedName.append(mHostname, prev, pos - prev);
        prev = pos + 1;
        pos = mHostname.find('.', prev);
    }
    if (pos != prev) {
        mEncodedName += char(mHostname.size() - prev);
        mEncodedName.append(mHostname, prev, pos - prev);
    }
}
inline DnsQuery::~DnsQuery() { }

inline bool DnsQuery::fillBuffer(uint16_t transId, void *buffer, size_t n) const {
    if (mEncodedName.empty()) {
        return false;
    }
    uint8_t *now = static_cast<uint8_t*>(buffer);

    // Make header
    if (n < sizeof(DnsHeader)) {
        // No encough space
        return false;
    }
    auto header = reinterpret_cast<DnsHeader*>(now);
    ::memset(header, 0, sizeof(DnsHeader));

    header->id = ::htons(transId);
    header->rd = 1;
    header->questionCount = ::htons(1); //< Make 1 question

    now += sizeof(DnsHeader);
    n -= sizeof(DnsHeader);

    // Make question
    if (n < mEncodedName.size() + 1 + sizeof(uint16_t) * 2) {
        return false;
    }
    ::memcpy(now, mEncodedName.data(), mEncodedName.size());
    now[mEncodedName.size()] = '\0';
    
    now += (mEncodedName.size() + 1);

    uint16_t type_ = ::htons(mType);
    uint16_t class_ = ::htons(1);

    ::memcpy(now, &type_, sizeof(type_));
    ::memcpy(now + sizeof(type_), &class_, sizeof(class_));
    return true;
}
inline bool DnsQuery::fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const {
    buffer.resize(fillBufferSize());
    return fillBuffer(transId, buffer.data(), buffer.size());
}
inline size_t DnsQuery::fillBufferSize() const noexcept {
    // DnsHeader + 1 * Question(Hostname + Request Type + Request Class)
    return sizeof(DnsHeader) + (mEncodedName.size() + 1 + sizeof(uint16_t) * 2);
}

// --- DnsAnswer Impl
inline std::string DnsAnswer::name() const {
    return mName;
}
inline uint16_t DnsAnswer::type() const noexcept {
    return mType;
}
inline uint16_t DnsAnswer::class_() const noexcept {
    return mClass;
}
inline uint32_t DnsAnswer::ttl() const noexcept {
    return mTTL;
}
inline size_t DnsAnswer::dataLength() const noexcept {
    return mData.size();
}
inline const void *DnsAnswer::data() const noexcept {
    return mData.data();
}

inline std::string DnsAnswer::cname() const {
    if (mType != DnsAnswer::CNAME) {
        return std::string();
    }
    return mData;
}
inline IPAddress DnsAnswer::address() const {
    if (mType != DnsAnswer::A && mType != DnsAnswer::AAAA) {
        return IPAddress();
    }
    return IPAddress::fromRaw(mData.data(), mData.size());
}

// --- DnsResponse Impl
inline auto DnsResponse::parse(const void *buffer, size_t n) -> Expected<DnsResponse, size_t> {
    if (buffer == nullptr || n < sizeof(DnsHeader)) {
        return Unexpected<size_t>(0);
    }
    DnsResponse res;
    size_t stopPosition = 0;
    if (!res._parse(static_cast<const uint8_t *>(buffer), n, &stopPosition)) {
        return Unexpected<size_t>(stopPosition);
    }
    return res;
}
inline bool DnsResponse::_parse(const uint8_t *buffer, size_t n, size_t *stopPosition) {
    const uint8_t *cur = buffer;

    // Copy headers
    mHeader = *reinterpret_cast<const DnsHeader *>(cur);
    cur += sizeof(DnsHeader);
    
    // Skip question
    for (size_t i = 0; i < ::htons(mHeader.questionCount); ++i) {
        if (!_skipQuestion(buffer, n, &cur)) {
            *stopPosition = cur - buffer;
            return false;
        }
    }
    // Parse answer
    for (size_t i = 0; i < ::htons(mHeader.answerCount); ++i) {
        if (!_parseAnswer(buffer, n, &cur, mAnswers.emplace_back())) {
            *stopPosition = cur - buffer;
            return false;
        }
    }
    return true;
}
inline bool DnsResponse::_skipQuestion(const uint8_t *buffer, size_t n, const uint8_t **cur) {
    if (!_skipName(buffer, n, cur)) {
        return false;
    }
    size_t left = n - (*cur - buffer);
    if (left < sizeof(uint16_t) * 2) {
        return false;
    }
    *cur += sizeof(uint16_t) * 2;
    return true;
}
inline bool DnsResponse::_skipName(const uint8_t *buffer, size_t size, const uint8_t **cur) {
    const uint8_t *ptr = *cur;
    bool jumped = false;
    size_t step = 1;

    while (*ptr != 0) {
        if (!jumped && ((*ptr & 0xC0) == 0xC0)) {
            jumped = true;
            ptr = buffer + sizeof(DnsHeader) + (*ptr & ~0xC0);
            step += 1;
            
            // Check if we are out of bounds
            if(ptr > (buffer + size)){
                return false;
            }
            continue;
        }

        auto n = *ptr;

        // Check if we are out of bounds
        if(ptr + n + 1 > (buffer + size)){
            return false;
        }
        if (!jumped) {
            step += (n + 1);
        }
        ptr += (n + 1);
    }
    *cur += step;
    return true;
}
inline bool DnsResponse::_parseAnswer(const uint8_t *buffer, size_t size, const uint8_t **answer, DnsAnswer &output) {
    // Answer is name + type(uint16) + class(uint16) + ttl(uint32) + rdlength(uint16) + rdata
    std::string name;
    const uint8_t *now = *answer;
    if (!_parseName(buffer, size, &now, name)) {
        return false;
    }
    output.mName = std::move(name);
    if ((now + sizeof(uint16_t) * 3 + sizeof(uint32_t)) >= (buffer + size)) {
        return false;
    }
    uint16_t type = 0;
    uint16_t class_ = 0;
    uint32_t ttl = 0;
    uint16_t rdlength = 0;

    ::memcpy(&type, now, sizeof(type));
    now += sizeof(type);
    ::memcpy(&class_, now, sizeof(class_));
    now += sizeof(class_);
    ::memcpy(&ttl, now, sizeof(ttl));
    now += sizeof(ttl);
    ::memcpy(&rdlength, now, sizeof(rdlength));
    now += sizeof(rdlength);

    // Convert to host byte order
    type = ::ntohs(type);
    class_ = ::ntohs(class_);
    ttl = ::ntohl(ttl);
    rdlength = ::ntohs(rdlength);

    // Copy to answer
    output.mType = type;
    output.mClass = class_;
    output.mTTL = ttl;

    if ((now + rdlength) > (buffer + size)) {
        return false;
    }
    // Move
    *answer = now + rdlength;

    // Copy data
    if (type == DnsAnswer::CNAME) {
        if (!_parseName(buffer, size, &now, output.mData)) {
            return false;
        }
    }
    else {
        output.mData.assign(reinterpret_cast<const char*>(now), reinterpret_cast<const char*>(now + rdlength));
    }
    return true;
}
inline bool DnsResponse::_unpackName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) {
    const uint8_t *ptr = *name;
    while (*ptr != 0) {
        uint8_t n = *ptr;
        ptr += 1;

        switch (n & 0xC0) {
            case 0: {
                // Check the label size
                if (n >= 63){
                    return false;
                }
                // Check out of bounds
                if (ptr + n > buffer + size) {
                    return false;
                }
                // Append the label
                output.push_back(n);
                output.append(ptr, ptr + n);
                // Move forward
                ptr += n;
                break;
            }
            case 0xC0: {
                // Reach compressed label
                // Jump over the label

                if (ptr >= buffer + size){
                    return false;
                }
                ptr = buffer + (((n & 0x3f) << 8) | (*ptr & 0xff));
                // Read the offset
                if (ptr < buffer || ptr > buffer + size) {
                    return false;
                }
                break;
            }
            default: {
                // Fail
                return false;
            }
        }
    }
    return _skipName(buffer, size, name);
}
inline bool DnsResponse::_parseName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) {
    std::string encodedName;
    if (!_unpackName(buffer, size, name, encodedName)) {
        return false;
    }
    bool first = true;
    for (size_t i = 0; i < encodedName.size();) {
        char n = encodedName[i];
        if (n == 0) {
            break;
        }
        if (n >= 63 || i + n >= encodedName.size()) {
            return false;
        }
        if (!first) {
            output.push_back('.');
        }
        output.append(encodedName.c_str() + i + 1, encodedName.c_str() + i + 1 + n);
        i += (n + 1);
        first = false;
    }
    return true;
}
inline bool DnsResponse::isOk() const noexcept {
    return mHeader.rcode == 0;
}
inline uint16_t DnsResponse::transId() const noexcept {
    return ::ntohs(mHeader.id);
}
inline uint16_t DnsResponse::answerCount() const noexcept {
    return ::ntohs(mHeader.answerCount);
}
inline auto DnsResponse::answers() const -> const std::vector<DnsAnswer> & {
    return mAnswers;
}
inline auto DnsResponse::addresses() const -> std::vector<IPAddress> {
    std::vector<IPAddress> addrs;
    for (const auto &answer : mAnswers) {
        if (answer.type() != DnsAnswer::A && answer.type() != DnsAnswer::AAAA) {
            continue;
        }
        addrs.push_back(answer.address());
    }
    return addrs;
}


ILIAS_NS_END