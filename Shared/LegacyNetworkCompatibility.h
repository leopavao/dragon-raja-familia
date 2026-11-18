#pragma once
#include <windows.h>
#include <cstring>

// The legacy DLL allocates fixed-size worker arrays from the CPU count.
// Modern machines can exceed those arrays before network initialization ends.
static void WINAPI LegacyNetworkSystemInfo(LPSYSTEM_INFO info)
{
    GetSystemInfo(info);
    if (info->dwNumberOfProcessors > 2)
    {
        info->dwNumberOfProcessors = 2;
        info->dwActiveProcessorMask &= 3;
    }
}

static bool ConfigureLegacyNetworkCompatibility()
{
    HMODULE module = GetModuleHandleA("INetwork.dll");
    if (!module)
        return false;
    BYTE* base = reinterpret_cast<BYTE*>(module);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const DWORD imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports);
    for (; descriptor->Name; ++descriptor)
    {
        if (!descriptor->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        IMAGE_THUNK_DATA* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            IMAGE_IMPORT_BY_NAME* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(name->Name), "GetSystemInfo") != 0)
                continue;
            DWORD protection = 0;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function), PAGE_READWRITE, &protection))
                return false;
            addresses->u1.Function = reinterpret_cast<ULONG_PTR>(&LegacyNetworkSystemInfo);
            DWORD unused = 0;
            VirtualProtect(&addresses->u1.Function, sizeof(addresses->u1.Function), protection, &unused);
            return true;
        }
    }
    return false;
}
