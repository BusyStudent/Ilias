#include "ntdll.hpp"

ILIAS_NS_BEGIN

auto win32::ntdll() -> const NtDll & {
    static const NtDll dll;
    return dll;
}

ILIAS_NS_END