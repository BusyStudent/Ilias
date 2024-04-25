#pragma once

#include "ilias.hpp"
#include "ilias_async.hpp"
#include "ilias_channel.hpp"
#include "ilias_expected.hpp"

#include <chrono>
#include <vector>
#include <map>
#include <memory>

ILIAS_NS_BEGIN

class DnsNameParser;
class DnsResponse;
class DnsHeader;
class DnsQuery;
class DnsLookup;

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
    DnsQuery(std::string_view name, uint16_t type = A);
    DnsQuery(const DnsQuery &) = default;
    ~DnsQuery();

    /**
     * @brief Fill the query request data to buffer
     * 
     * @return true 
     * @return false 
     */
    auto fillBuffer(uint16_t transId, void *buffer, size_t n) const -> bool;
    auto fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const -> bool;
    /**
     * @brief Get the size of buffer needed to fill the query request data
     * 
     * @return size_t
     */
    auto fillBufferSize() const -> size_t;
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
     * @return std::string_view 
     */
    auto name() const -> std::string_view;
    /**
     * @brief Get the type of the answer
     * 
     * @return uint16_t 
     */
    auto type() const -> uint16_t;
    /**
     * @brief Get the class of the answer
     * 
     * @return uint16_t 
     */
    auto class_() const -> uint16_t;
    /**
     * @brief Get the TTL of the answer (in second)
     * 
     * @return uint32_t 
     */
    auto ttl() const -> uint32_t;
    /**
     * @brief Get the data len
     * 
     * @return size_t 
     */
    auto dataLength() const -> size_t;
    /**
     * @brief Get the data
     * 
     * @return const void* 
     */
    auto data() const -> const void *;
    /**
     * @brief Try Get the cname (type must be CNAME)
     * 
     * @return std::string_view 
     */
    auto cname() const -> std::string_view;
    /**
     * @brief Try Get the address (type must be A or AAAA)
     * 
     * @return IPAddress 
     */
    auto address() const -> IPAddress;
    /**
     * @brief Check if the answer is expired
     * 
     * @return true 
     * @return false 
     */
    auto isExpired() const -> bool;
private:
    std::string mName;
    uint16_t mType = 0;
    uint16_t mClass = 0;
    uint32_t mTTL = 0;
    std::string mData;
    std::chrono::steady_clock::time_point mExpireTime;
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
    auto isOk() const -> bool;
    /**
     * @brief Get the trans Id
     * 
     * @return uint16_t 
     */
    auto transId() const -> uint16_t;
    /**
     * @brief Get the answer count
     * 
     * @return uint16_t 
     */
    auto answerCount() const -> uint16_t;
    /**
     * @brief Get the cname
     * 
     * @return std::string 
     */
    auto cname() const -> std::string;
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
    auto _parse(const uint8_t *buffer, size_t size, size_t *stopPosition) -> bool;
    auto _skipQuestion(const uint8_t *buffer, size_t size, const uint8_t **cur) -> bool;
    auto _skipName(const uint8_t *buffer, size_t size, const uint8_t **name) -> bool;
    auto _unpackName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) -> bool;
    auto _parseAnswer(const uint8_t *buffer, size_t size, const uint8_t **answer, DnsAnswer &output) -> bool;
    auto _parseName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) -> bool;

    DnsHeader mHeader { };
    std::vector<DnsAnswer> mAnswers;
};
/**
 * @brief a dns lookup for listen response from server
 * 
 */
class DnsLookup {
public:
    using Type = DnsQuery::Type;
public:
    DnsLookup(IoContext &ctxt);
    ~DnsLookup();
    /**
     * @brief manager dns server
     * 
     * @param dnsServer 
     */
    auto addDnsServer(const IPAddress &dnsServer) -> void;
    auto addDnsServer(const IPAddress &dnsServer, uint16_t port) -> void;
    auto addDnsServer(const IPEndpoint &dnsServer) -> void;
    auto clearDnsServers() -> void;

    /**
     * @brief query hostname
     * 
     * This method queries the DNS server for the domain name,and listen the result.
     * 
     * @param hostname 
     * @param type 
     * @return Task<std::vector<DnsAnswer>> 
     */
    auto query(const std::string &hostname, Type type = Type::A) -> Task<std::vector<DnsAnswer>>;

private:
    auto _query(const std::string &hostname, Type type, const IPEndpoint &dnsServer) -> Task<std::vector<DnsAnswer>>;
    auto _look() -> Task<void>;
    static auto _handRecv(std::shared_ptr<UdpClient> client,Sender<DnsResponse> sender) -> Task<void>; 
private:
    IoContext &mIoContext;
    std::vector<IPEndpoint> mDnsServers = {
            IPEndpoint("114.114.114.114", 53)
        };
    
    std::map<uint16_t, DnsResponse> mDnsCache;
    std::shared_ptr<UdpClient> mDnsClientV4;
    std::shared_ptr<UdpClient> mDnsClientV6;
    Sender<DnsResponse> mDnsSenderChannel;
    Receiver<DnsResponse> mDnsReceiverChannel;
    uint16_t mTransId = 0;
};
/**
 * @brief A Resolver for manage query and response
 * 
 */
class Resolver {
public:
    using Query = DnsQuery;

    Resolver(IoContext &);
    Resolver(const Resolver &) = delete;
    ~Resolver();

    auto resolve(std::string_view hostname) -> Task<std::vector<IPAddress> >;
private:
    auto _findCache(std::string_view hostname) -> Result<std::vector<IPAddress> >;
    auto _run(Receiver<DnsQuery> recv) -> Task<void>;

    IoContext &mCtxt;
    int64_t    mTimeout = 5000;
    std::multimap<std::string, DnsAnswer, std::less<> > mAnswers;
    DnsLookup mDnsLookup;
};

// --- DnsQuery Impl
inline DnsQuery::DnsQuery(std::string_view name, uint16_t type) : mHostname(name), mType(type) {
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

inline auto DnsQuery::fillBuffer(uint16_t transId, void *buffer, size_t n) const -> bool {
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
inline auto DnsQuery::fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const -> bool {
    buffer.resize(fillBufferSize());
    return fillBuffer(transId, buffer.data(), buffer.size());
}
inline auto DnsQuery::fillBufferSize() const -> size_t {
    // DnsHeader + 1 * Question(Hostname + Request Type + Request Class)
    return sizeof(DnsHeader) + (mEncodedName.size() + 1 + sizeof(uint16_t) * 2);
}

// --- DnsAnswer Impl
inline auto DnsAnswer::name() const -> std::string_view {
    return mName;
}
inline auto DnsAnswer::type() const -> uint16_t {
    return mType;
}
inline auto DnsAnswer::class_() const -> uint16_t {
    return mClass;
}
inline auto DnsAnswer::ttl() const -> uint32_t {
    return mTTL;
}
inline auto DnsAnswer::dataLength() const -> size_t {
    return mData.size();
}
inline auto DnsAnswer::data() const -> const void * {
    return mData.data();
}

inline auto DnsAnswer::cname() const -> std::string_view {
    if (mType != DnsAnswer::CNAME) {
        return std::string_view();
    }
    return mData;
}
inline auto DnsAnswer::address() const -> IPAddress {
    if (mType != DnsAnswer::A && mType != DnsAnswer::AAAA) {
        return IPAddress();
    }
    return IPAddress::fromRaw(mData.data(), mData.size());
}
inline auto DnsAnswer::isExpired() const -> bool {
    return std::chrono::steady_clock::now() > mExpireTime;
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
inline auto DnsResponse::_parse(const uint8_t *buffer, size_t n, size_t *stopPosition) -> bool {
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
inline auto DnsResponse::_skipQuestion(const uint8_t *buffer, size_t n, const uint8_t **cur) -> bool {
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
inline auto DnsResponse::_skipName(const uint8_t *buffer, size_t size, const uint8_t **cur) -> bool {
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
inline auto DnsResponse::_parseAnswer(const uint8_t *buffer, size_t size, const uint8_t **answer, DnsAnswer &output) -> bool {
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
    output.mExpireTime = std::chrono::steady_clock::now() + std::chrono::seconds(ttl);

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
inline auto DnsResponse::_unpackName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) -> bool {
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
inline auto DnsResponse::_parseName(const uint8_t *buffer, size_t size, const uint8_t **name, std::string &output) -> bool{
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
inline auto DnsResponse::isOk() const -> bool {
    return mHeader.rcode == 0;
}
inline auto DnsResponse::transId() const -> uint16_t {
    return ::ntohs(mHeader.id);
}
inline auto DnsResponse::answerCount() const -> uint16_t {
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


inline DnsLookup::DnsLookup(IoContext &ctxt) : 
    mIoContext(ctxt)
    , mDnsClientV4(new UdpClient(ctxt, AF_INET))
    , mDnsClientV6(new UdpClient(ctxt, AF_INET6))
{ 
    mDnsClientV4->bind(IPEndpoint(IPAddress4::any(), 0));
    mDnsClientV6->bind(IPEndpoint(IPAddress6::any(), 0));
    auto [sx, rx] = Channel<DnsResponse>::make();
    mDnsSenderChannel = sx;
    mDnsReceiverChannel = std::move(rx);
    _look();
}
inline DnsLookup::~DnsLookup() {
    mDnsClientV4->close();
    mDnsClientV6->close();
    mDnsReceiverChannel.close();
    mDnsSenderChannel.close();
}
inline auto DnsLookup::addDnsServer(const IPAddress &dnsServer) -> void {
    mDnsServers.push_back(IPEndpoint(dnsServer, 53));
}
inline auto DnsLookup::addDnsServer(const IPAddress &dnsServer, uint16_t port) -> void {
    mDnsServers.push_back(IPEndpoint(dnsServer, port));
}
inline auto DnsLookup::addDnsServer(const IPEndpoint &dnsServer) -> void {
    mDnsServers.push_back(dnsServer);
}
inline auto DnsLookup::clearDnsServers() -> void {
    mDnsServers.clear();
}

inline auto DnsLookup::query(const std::string &hostname, Type type) -> Task<std::vector<DnsAnswer>> {
    Error ret(Error::Unknown);
    for (auto &dnsServer : mDnsServers) {
        std::chrono::seconds st(3); // 1s
        auto waitResult = co_await WhenAny(_query(hostname, type, dnsServer), Sleep(st));
        if (waitResult.index() == 0) {
            auto queueResult = std::get<0>(waitResult);
            if (queueResult) {
                co_return queueResult.value();
            } 
            else if (queueResult.error() == Error::Canceled) {
                ret = queueResult.error();
            } 
            else {
                ret = queueResult.error();
            }
        } 
        else {
            ret = Error::TimedOut;
        }
    }
    co_return ret;
}

inline auto DnsLookup::_query(
    const std::string &hostname, 
    Type type, 
    const IPEndpoint &dnsServer) -> Task<std::vector<DnsAnswer>>
{
    DnsQuery query(hostname, type);
    std::vector<uint8_t> data;
    uint16_t transId = ++mTransId;
    query.fillBuffer(transId, data);

    UdpClient* client;
    if (dnsServer.family() == AF_INET) {
        client = mDnsClientV4.get();
    } 
    else if (dnsServer.family() == AF_INET6) {
        client = mDnsClientV6.get();
    } 
    else {
        co_return Unexpected<Error>(Error::AddressFamilyNotSupported);
    }

    if (mDnsCache.contains(transId)) {
        mDnsCache.erase(transId);
    }

    auto dnsRequest = co_await client->sendto(data.data(), data.size(), dnsServer);
    if (!dnsRequest) {
        co_return Unexpected<Error>(Error::Unknown);
    }

    if (mDnsCache.contains(transId)) {
        auto response = mDnsCache[transId];
        mDnsCache.erase(transId);
        co_return response.answers();
    }

    while (true) {
        auto response = co_await mDnsReceiverChannel.recv();
        if (response.value().transId() == transId) {
            co_return response.value().answers();
        } 
        else if (!response && response.error() == Error::Canceled) {
            co_return Unexpected<Error>(Error::Canceled);
        }
    }
    co_return Unexpected<Error>(Error::Unknown);
}

inline auto DnsLookup::_handRecv(std::shared_ptr<UdpClient> client,Sender<DnsResponse> sender) -> Task<void> {
    uint8_t recvdata[1024];
    while (true) {
        auto ret = co_await client->recvfrom(recvdata, sizeof(recvdata));
        if (!ret) {
            co_return ret.error();
        }
        auto response = DnsResponse::parse(recvdata, ret->first);
        if (!response) {
            continue;
        }
        auto recv = co_await sender.send(response.value());
        if (!recv && (recv.error() == Error::Canceled || recv.error() == Error::ChannelBroken)) {
            co_return recv.error();
        }
    }
    co_return Result<void>();
}


inline auto DnsLookup::_look() -> Task<void> {
    ilias_go _handRecv(mDnsClientV4, mDnsSenderChannel);
    ilias_go _handRecv(mDnsClientV6, mDnsSenderChannel);
    co_return Result<void>();
}

// --- Resolver
inline Resolver::Resolver(IoContext &ctxt) : mCtxt(ctxt), mDnsLookup(ctxt) { }
inline Resolver::~Resolver() { }

inline auto Resolver::resolve(std::string_view host) -> Task<std::vector<IPAddress> > {
    if (auto var = _findCache(host); var) {
        co_return std::move(var.value());
    }
    if (auto ret = co_await mDnsLookup.query(std::string(host), DnsLookup::Type::A); ret) {
        for (auto &ans : ret.value()) {
            mAnswers.insert(std::make_pair(host, ans));
        }
    }
    if (auto ret = co_await mDnsLookup.query(std::string(host), DnsLookup::Type::AAAA); ret) {
        for (auto &ans : ret.value()) {
            mAnswers.insert(std::make_pair(host, ans));
        }
    }
    if (auto ret = co_await mDnsLookup.query(std::string(host), DnsLookup::Type::CNAME); ret) {
        for (auto &ans : ret.value()) {
            mAnswers.insert(std::make_pair(host, ans));
        }
    }
    if (auto var = _findCache(host); var) {
        co_return std::move(var.value());
    }
    co_return Unexpected(Error::NoDataRecord);
}
inline auto Resolver::_findCache(std::string_view what) -> Result<std::vector<IPAddress> > {
    auto range = mAnswers.equal_range(what);
    if (range.first == range.second) {
        return Unexpected(Error::NoDataRecord);
    }
    std::vector<IPAddress> addrs;

    for (auto it = range.first; it != range.second;) {
        if (it->second.isExpired()) {
            it = mAnswers.erase(it);
        } 
        else {
            if (it->second.address().isValid()) {
                addrs.push_back(it->second.address());
            }
            ++it;
        }
    }
    return addrs;
}
#if 0
inline auto Resolver::_run(Receiver<DnsQuery> recv) -> Task<void> {
    std::map<uint16_t, Sender<DnsResponse> > clients;
    uint16_t currentId = 0;
    uint8_t buffer[1024];
    while (auto value = co_await recv.recv()) {
        std::vector<uint8_t> sendBuffer;
        value->fillBuffer(currentId, sendBuffer);
        if (auto err = co_await mClient.sendto(sendBuffer.data(), sendBuffer.size(), mServer); !err) {
            
        }
    }
    co_return Result<void>();
}
#endif

ILIAS_NS_END