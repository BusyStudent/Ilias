#pragma once

#include "ilias.hpp"
#include "ilias_expected.hpp"

#include <vector>

ILIAS_NS_BEGIN

/**
 * @brief A struct for common dns query / response header
 * 
 */
struct DnsHeader {
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
    DnsQuery(const char *hostname);
    DnsQuery();

    /**
     * @brief Fill the query request data to buffer
     * 
     * @return true 
     * @return false 
     */
    bool fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const;
private:
    void _fillQuestion(uint8_t **current, uint16_t class_, uint16_t type) const;

    std::string mHostname;     //< Human readable name, www.google.com
    std::string mEncodedName;  //< Encoded name, 3www6google3com0
};
/**
 * @brief A Dns Response
 * 
 */
class DnsResponse {
    static auto parse(void *buffer, size_t n) -> Expected<DnsResponse, int>;
};

/**
 * @brief A Reslover for manage query and response
 * 
 */
class Resolver {
public:
    using Query = DnsQuery;

    Resolver();
    ~Resolver();
private:

};

// --- DnsQuery Impl
inline DnsQuery::DnsQuery(const char *name) : mHostname(name) {
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
        mEncodedName += char(pos - prev);
        mEncodedName.append(mHostname, prev, pos - prev);
    }
}
inline DnsQuery::~DnsQuery() { }

bool DnsQuery::fillBuffer(uint16_t transId, std::vector<uint8_t> &buffer) const {
    if (!mEncodedName.empty()) {
        return false;
    }
    // DnsHeader + 2 * Question(Hostname + Request Type + Request Class)
    buffer.resize(sizeof(DnsHeader) + 2 * (mEncodedName.size() + 1 + sizeof(uint16_t) * 2));

    auto header = reinterpret_cast<DnsHeader*>(buffer.data());
    ::memset(header, 0, sizeof(DnsHeader));

    header->id = transId;
    header->rd = 1;
    header->questionCount = htons(2); //< Make 2 question

    auto question = buffer.data() + sizeof(DnsHeader);
    _fillQuestion(&question, 1,  1); //< A
    _fillQuestion(&question, 28, 1); //< AAAA

    return true;
}

ILIAS_NS_END