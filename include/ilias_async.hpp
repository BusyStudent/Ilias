#pragma once

#include "ilias.hpp"
#include <functional>

// --- Import async backend
#if 0
    #include "ilias_iocp.hpp"
#else
    #include "ilias_poll.hpp"
#endif

ILIAS_NS_BEGIN

/**
 * @brief A helper class for impl async socket
 * 
 */
class AsyncSocket {
public:
    AsyncSocket(IOContext &ctxt, Socket &&sockfd);
    AsyncSocket(const AsyncSocket &) = delete;
    ~AsyncSocket();
private:
    IOContext *mContext = nullptr;
    Socket     mSocket;
};

class TcpClient : public AsyncSocket {

};
class TcpServer : public AsyncSocket {
    
};
class UdpSocket : public AsyncSocket {

};


ILIAS_NS_END