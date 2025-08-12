#include <ilias/io/system_error.hpp>
#include <ilias/io/stream.hpp>
#include <ilias/tls.hpp>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

ILIAS_NS_BEGIN

namespace tls {
namespace openssl {

class SslBio {

};

} // namespace openssl

// Tls Context...
auto context::make() -> void * {
	return SSL_CTX_new(TLS_method());
}

auto context::destroy(void *ptr) -> void {
	SSL_CTX_free(static_cast<SSL_CTX *>(ptr));
}

// Tls Data

} // namespace tls

ILIAS_NS_END