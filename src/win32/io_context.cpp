// Impl some default win32 io operations
#include <ilias/io/context.hpp>
#include "ntdll.hpp"

ILIAS_NS_BEGIN

// TODO
auto IoContext::waitObject(HANDLE object) -> IoTask<void> {
    co_return Err(IoError::OperationNotSupported);    
}

auto IoContext::connectNamedPipe(IoDescriptor *fd) -> IoTask<void> {
    co_return Err(IoError::OperationNotSupported);
}

ILIAS_NS_END