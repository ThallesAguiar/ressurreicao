// SPDX-License-Identifier: AGPL-3.0-or-later
// Crash logging helper. Writes the exception code and fault address to
// crash.log so the information survives the console window closing.
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
    FILE* f = fopen("crash.log", "a");
    if (f != NULL) {
        void* moduleBase = (void*)GetModuleHandle(NULL);
        EXCEPTION_RECORD* rec = info->ExceptionRecord;
        void* backtrace[32];
        USHORT frames;
        USHORT i;

        fprintf(f, "EXCEPTION code=0x%08lX addr=%p moduleBase=%p\n",
                (unsigned long)rec->ExceptionCode, rec->ExceptionAddress,
                moduleBase);
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            rec->NumberParameters >= 2) {
            fprintf(f, "  access violation: %s at address 0x%p\n",
                    rec->ExceptionInformation[0] == 0 ? "read" : "write",
                    (void*)rec->ExceptionInformation[1]);
        }

        frames = RtlCaptureStackBackTrace(0, 32, backtrace, NULL);
        fprintf(f, "  backtrace (%u frames, subtract moduleBase for file "
                   "offset):\n",
                (unsigned)frames);
        for (i = 0; i < frames; i++) {
            fprintf(f, "    %p\n", backtrace[i]);
        }
        fflush(f);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void InitCrashHandler(void) {
    SetUnhandledExceptionFilter(CrashHandler);
}
#else
void InitCrashHandler(void) {}
#endif
