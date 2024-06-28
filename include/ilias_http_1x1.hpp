#pragma once

#include "ilias_http_core.hpp"
#include "ilias_mutex.hpp"

ILIAS_NS_BEGIN

class Http1Connection;
class Http1Stream;


/**
 * @brief Impl the http1 protocol
 * 
 */
class Http1Stream final : public HttpStream {
public:

};

class Http1Connection final : public HttpConnection {
public:

private:
    IStreamClient mClient;
    Mutex mMutex; //< For Http1 keep-alive, at one time, only a single request can be processed
    bool mBroken = false; //< False on physical connection close
};

ILIAS_NS_END