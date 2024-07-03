#pragma once

#include "ilias.hpp"

#if !defined(ILIAS_NO_SSL)

// --- Check current env

// --- OpenSSL
#if !defined(ILIAS_NO_OPENSSL)
#if !__has_include(<openssl/ssl.h>)
    #define ILIAS_NO_OPENSSL
#endif
#endif

// --- Schannel
#if !defined(ILIAS_NO_SCHANNEL)
#if !defined(_WIN32)
    #define ILIAS_NO_SCHANNEL
#endif
#endif


// --- Import SSL
#if   !defined(ILIAS_NO_OPENSSL)
    #define ILIAS_SSL_USE_OPENSSL
    #include "ilias_ssl_openssl.hpp"
#elif !defined(ILIAS_NO_SCHANNEL)
    #define ILIAS_SSL_USE_SCHANNEL
    #include "ilias_ssl_schannel.hpp"
#else
    //< We can not found any support ssl backend
    #define ILIAS_NO_SSL
#endif


// --- Basic SSL Extension concept
ILIAS_NS_BEGIN

template <typename T>
concept SslSniExtension = requires(T t) {
    t.setHostname("example.com");
};

ILIAS_NS_END

#endif