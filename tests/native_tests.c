#include "../src/tray_icon_promoter.c"
#include <stdio.h>
#include <wchar.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int main(int argc, char **argv)
{
    BOOL silent;
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    STARTUPINFOW startup;
    PROCESS_INFORMATION child;
    WCHAR executable[MAX_PATH];
    WCHAR command[MAX_PATH + 32];
    WCHAR tooLong[MAX_PATH + 80];
    DWORD index;
    AppStatus status;
    WCHAR json[4096];

    (void)argv;
    if (argc > 1) {
        return 0;
    }
    CHECK(ParseAction(L"app.exe", &silent) == ACTION_DEFAULT && !silent);
    CHECK(ParseAction(L"\"C:\\test dir\\app.exe\" --help --silent", &silent) == ACTION_HELP && silent);
    CHECK(ParseAction(L"app.exe --silent --status", &silent) == ACTION_STATUS && silent);
    CHECK(ParseAction(L"app.exe --install", &silent) == ACTION_INSTALL);
    CHECK(ParseAction(L"app.exe --uninstall", &silent) == ACTION_UNINSTALL);
    CHECK(ParseAction(L"app.exe --watch", &silent) == ACTION_WATCH);
    CHECK(ParseAction(L"app.exe --once", &silent) == ACTION_ONCE);
    CHECK(ParseAction(L"app.exe --self-test", &silent) == ACTION_SELF_TEST);
    CHECK(ParseAction(L"app.exe --stop", &silent) == ACTION_STOP);
    CHECK(ParseAction(L"app.exe --restart", &silent) == ACTION_RESTART);
    CHECK(ParseAction(L"app.exe --status-json --silent", &silent) == ACTION_STATUS_JSON && silent);
    CHECK(ParseAction(L"app.exe --stop --restart", &silent) == ACTION_INVALID);
    CHECK(ParseAction(L"app.exe --staus --silent", &silent) == ACTION_INVALID && silent);
    CHECK(ParseAction(L"app.exe --install --uninstall", &silent) == ACTION_INVALID);
    CHECK(ParseAction(L"app.exe --status --status", &silent) == ACTION_INVALID);
    CHECK(ParseAction(L"app.exe \"--watch", &silent) == ACTION_INVALID);
    lstrcpyW(tooLong, L"app.exe ");
    for (index = 8; index < (DWORD)(sizeof(tooLong) / sizeof(tooLong[0])) - 1; index++) {
        tooLong[index] = L'x';
    }
    tooLong[index] = L'\0';
    CHECK(ParseAction(tooLong, &silent) == ACTION_INVALID);

    ZeroMemory(&status, sizeof(status));
    lstrcpyW(status.path, L"C:\\test \"name\"\\\x03A9\n.exe");
    lstrcpyW(status.installedVersion, L"1.0.0");
    status.startup = STARTUP_MISSING;
    status.recommendation = L"Repair installation.";
    status.stats.errors = 4;
    CHECK(BuildStatusJson(&status, json, 4096));
    CHECK(wcsstr(json, L"\\\"name\\\"") != NULL);
    CHECK(wcsstr(json, L"\\u03a9\\u000a") != NULL);
    CHECK(wcsstr(json, L"\"errors\":4") != NULL);
    CHECK(wcsstr(json, L"\"scan_complete\":false") != NULL);
    CHECK(wcsstr(json, L"\"installed_version\":\"1.0.0\"") != NULL);
    CHECK(!BuildStatusJson(&status, json, 8));

    CHECK(ready != NULL);
    CHECK(!WaitForWatcherToStart(GetCurrentProcess(), ready));
    CHECK(GetLastError() == ERROR_TIMEOUT);
    CHECK(SetEvent(ready));
    CHECK(WaitForWatcherToStart(GetCurrentProcess(), ready));

    /* A signaled readiness event must never mask an exited child. */
    CHECK(GetModulePath(executable, MAX_PATH));
    command[0] = L'\0';
    CHECK(AppendPathPart(command, MAX_PATH + 32, L"\""));
    CHECK(AppendPathPart(command, MAX_PATH + 32, executable));
    CHECK(AppendPathPart(command, MAX_PATH + 32, L"\" --child"));
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&child, sizeof(child));
    CHECK(CreateProcessW(executable, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child));
    CHECK(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0);
    CHECK(!WaitForWatcherToStart(child.hProcess, ready));
    CHECK(GetLastError() == ERROR_PROCESS_ABORTED);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    CloseHandle(ready);
    puts("Native argument and readiness tests passed.");
    return 0;
}
