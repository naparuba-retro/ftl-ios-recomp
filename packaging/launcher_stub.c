/* FTL-Recomp.exe : the program players (and RecompLauncher) start on Windows.
 * It runs  python\python.exe play.py <same arguments>  from its own folder, with no console window, passes the standard
 * handles through (RecompLauncher reads the "progress" lines of the setup step from them) and returns the exit code.
 * The children are tied to a job object: stopping this program stops the whole tree (Python, the build, the game).
 * Built by packaging/make_dist.py with Zig:  zig cc -target x86_64-windows-gnu -O2 -s -Wl,--subsystem,windows launcher_stub.c */
#include <windows.h>
#include <wchar.h>

static wchar_t *skip_argv0(wchar_t *c)
{
    if (*c == L'"') { c++; while (*c && *c != L'"') c++; if (*c) c++; }
    else while (*c && *c != L' ' && *c != L'\t') c++;
    while (*c == L' ' || *c == L'\t') c++;
    return c;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    (void)hi; (void)hp; (void)cmd; (void)show;
    wchar_t dir[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return 100;
    while (n > 0 && dir[n - 1] != L'\\' && dir[n - 1] != L'/') n--;
    if (n > 0) n--;
    dir[n] = 0;                                         /* the folder of this program */

    wchar_t *args = skip_argv0(GetCommandLineW());
    size_t cap = wcslen(dir) * 2 + wcslen(args) + 64;
    wchar_t *line = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap * sizeof(wchar_t));
    if (!line) return 101;
    wcscpy(line, L"\""); wcscat(line, dir); wcscat(line, L"\\python\\python.exe\" \"");
    wcscat(line, dir); wcscat(line, L"\\play.py\" "); wcscat(line, args);

    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE), err = GetStdHandle(STD_ERROR_HANDLE);
    if ((out && out != INVALID_HANDLE_VALUE) || (err && err != INVALID_HANDLE_VALUE)) {
        si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = in; si.hStdOutput = out; si.hStdError = err;
    }
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, NULL, dir, &si, &pi))
        return 102;
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; ZeroMemory(&li, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0; GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)rc;
}
