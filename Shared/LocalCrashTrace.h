#pragma once

static LONG WINAPI WriteLocalCrashTrace(EXCEPTION_POINTERS* exception)
{
    FILE* output = fopen("local-crash.txt", "w");
    if (output)
    {
        fprintf(output, "Exception %08lX at %p\n", exception->ExceptionRecord->ExceptionCode,
            exception->ExceptionRecord->ExceptionAddress);
        const CONTEXT* context = exception->ContextRecord;
        fprintf(output, "EIP=%08lX EBP=%08lX ESP=%08lX EAX=%08lX ECX=%08lX\n",
            context->Eip, context->Ebp, context->Esp, context->Eax, context->Ecx);
        DWORD* stack = reinterpret_cast<DWORD*>(context->Esp);
        for (int index = 0; index < 64; ++index)
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (!VirtualQuery(stack + index, &info, sizeof(info)) || info.State != MEM_COMMIT)
                break;
            const DWORD address = stack[index];
            MEMORY_BASIC_INFORMATION moduleInfo = {};
            if (VirtualQuery(reinterpret_cast<void*>(address), &moduleInfo, sizeof(moduleInfo)) && moduleInfo.Type == MEM_IMAGE)
            {
                char module[MAX_PATH] = {};
                GetModuleFileNameA(static_cast<HMODULE>(moduleInfo.AllocationBase), module, MAX_PATH);
                fprintf(output, "stack[%d] %08lX %s + %08lX\n", index, address, module,
                    address - reinterpret_cast<DWORD>(moduleInfo.AllocationBase));
            }
        }
        fclose(output);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
