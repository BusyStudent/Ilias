#include "ntdll.hpp"

ILIAS_NS_BEGIN

auto win32::ntdll() -> NtDll & {
    static NtDll dll;
    return dll;
}

ILIAS_NS_END