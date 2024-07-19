#pragma once

#include "ilias.hpp"
#include <span>

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
    #include "ssl/openssl.hpp"
#elif !defined(ILIAS_NO_SCHANNEL)
    #define ILIAS_SSL_USE_SCHANNEL
    #include "ssl/schannel.hpp"
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

template <typename T>
concept SslAlpnExtension = requires(T t) {
    t.setAlpn(std::span<const char *> {});
    t.alpnSelected();
};

ILIAS_NS_END

#endif