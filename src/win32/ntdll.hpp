// INTERNAL!!!
#pragma once
#include <ilias/detail/win32defs.hpp>
#include <winternl.h>

#define NT_IMPORT(fn) decltype(::fn) *fn = reinterpret_cast<decltype(::fn)*>(::GetProcAddress(mNt, #fn))

extern "C" {
    extern NTSTATUS NTAPI NtAssociateWaitCompletionPacket (
        _In_ HANDLE WaitCompletionPacketHandle,
        _In_ HANDLE IoCompletionHandle,
        _In_ HANDLE TargetObjectHandle,
        _In_opt_ PVOID KeyContext,
        _In_opt_ PVOID ApcContext,
        _In_ NTSTATUS IoStatus,
        _In_ ULONG_PTR IoStatusInformation,
        _Out_opt_ PBOOLEAN AlreadySignaled
    );

    extern NTSTATUS NTAPI NtCancelWaitCompletionPacket (
        _In_ HANDLE WaitCompletionPacketHandle,
        _In_ BOOLEAN RemoveSignaledPacket
    );

    extern NTSTATUS NTAPI NtCreateWaitCompletionPacket (
        _Out_ PHANDLE WaitCompletionPacketHandle,
        _In_ ACCESS_MASK DesiredAccess,
        _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes
    );

    extern NTSTATUS NTAPI NtSetInformationFile(
        _In_ HANDLE FileHandle,
        _Out_ PIO_STATUS_BLOCK IoStatusBlock,
        _In_ PVOID FileInformation,
        _In_ ULONG Length,
        _In_ FILE_INFORMATION_CLASS FileInformationClass
    );

    typedef struct _FILE_COMPLETION_INFORMATION {
        HANDLE Port;
        PVOID  Key;
    } FILE_COMPLETION_INFORMATION, *PFILE_COMPLETION_INFORMATION;
}

ILIAS_NS_BEGIN

namespace win32 {
    struct NtDll {
        HMODULE mNt = ::GetModuleHandleW(L"ntdll.dll");

        NT_IMPORT(NtCreateFile);
        NT_IMPORT(RtlNtStatusToDosError);

        NT_IMPORT(NtSetInformationFile);
        
        NT_IMPORT(NtAssociateWaitCompletionPacket);
        NT_IMPORT(NtCancelWaitCompletionPacket);
        NT_IMPORT(NtCreateWaitCompletionPacket);

        auto hasWaitCompletionPacket() const noexcept -> bool {
            return NtAssociateWaitCompletionPacket &&
                   NtCancelWaitCompletionPacket &&
                   NtCreateWaitCompletionPacket;
        }
    };

    extern auto ntdll() -> NtDll &;
} // namespace win32

ILIAS_NS_END

#undef NT_IMPORT