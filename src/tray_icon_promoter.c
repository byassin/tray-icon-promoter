#include <windows.h>

#define APP_NAME L"Tray Icon Promoter"
#define APP_VERSION L"1.0.0"
#define REGISTRY_PATH L"Control Panel\\NotifyIconSettings"
#define RUN_KEY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE_NAME L"TrayIconPromoter"
#define PROMOTED_VALUE_NAME L"IsPromoted"
#define REFRESH_VALUE_NAME L"_temp_tray_icon_promoter_refresh"
#define INSTALL_DIRECTORY_NAME L"TrayIconPromoter"
#define EXECUTABLE_NAME L"TrayIconPromoter.exe"
#define MUTEX_NAME L"Local\\TrayIconPromoter-8D37B978-83D7-4E36-A39C-F08BDB8B5911"
#define STOP_EVENT_NAME L"Local\\TrayIconPromoter.Stop-8D37B978-83D7-4E36-A39C-F08BDB8B5911"
#define SELF_TEST_KEY_NAME L"_TrayIconPromoterSelfTest_8D37B978"
#define DEBOUNCE_MILLISECONDS 150
#define ROOT_RETRY_MILLISECONDS 2000
#define STOP_WAIT_ATTEMPTS 50

typedef struct PromotionStats {
    DWORD total;
    DWORD promoted;
    DWORD changed;
    DWORD errors;
} PromotionStats;

typedef enum RequestedAction {
    ACTION_DEFAULT,
    ACTION_WATCH,
    ACTION_INSTALL,
    ACTION_UNINSTALL,
    ACTION_ONCE,
    ACTION_STATUS,
    ACTION_SELF_TEST
} RequestedAction;

typedef int (WINAPI *MessageBoxWFunction)(HWND, LPCWSTR, LPCWSTR, UINT);

static BOOL StringEquals(const WCHAR *left, const WCHAR *right)
{
    return lstrcmpiW(left, right) == 0;
}

static BOOL AppendPathPart(WCHAR *path, DWORD capacity, const WCHAR *part)
{
    DWORD pathLength = (DWORD)lstrlenW(path);
    DWORD partLength = (DWORD)lstrlenW(part);

    if (pathLength + partLength + 1 > capacity) {
        return FALSE;
    }

    CopyMemory(path + pathLength, part, (partLength + 1) * sizeof(WCHAR));
    return TRUE;
}

static BOOL AppendUnsigned(WCHAR *text, DWORD capacity, DWORD value)
{
    WCHAR reversed[16];
    DWORD count = 0;
    DWORD index;

    do {
        reversed[count++] = (WCHAR)(L'0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < (DWORD)(sizeof(reversed) / sizeof(reversed[0])));

    if ((DWORD)lstrlenW(text) + count + 1 > capacity) {
        return FALSE;
    }

    for (index = count; index > 0; index--) {
        WCHAR digit[2];
        digit[0] = reversed[index - 1];
        digit[1] = L'\0';
        if (!AppendPathPart(text, capacity, digit)) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL GetModulePath(WCHAR *path, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(NULL, path, capacity);
    return length > 0 && length < capacity;
}

static BOOL GetInstallPaths(
    WCHAR *directory,
    DWORD directoryCapacity,
    WCHAR *executable,
    DWORD executableCapacity)
{
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, directoryCapacity);

    if (length == 0 || length >= directoryCapacity) {
        return FALSE;
    }

    if (!AppendPathPart(directory, directoryCapacity, L"\\" INSTALL_DIRECTORY_NAME)) {
        return FALSE;
    }

    if ((DWORD)lstrlenW(directory) + 1 > executableCapacity) {
        return FALSE;
    }

    lstrcpynW(executable, directory, (int)executableCapacity);
    return AppendPathPart(executable, executableCapacity, L"\\" EXECUTABLE_NAME);
}

static void ShowMessage(BOOL silent, UINT flags, const WCHAR *message)
{
    if (!silent) {
        HMODULE user32 = LoadLibraryW(L"user32.dll");
        if (user32 != NULL) {
            union {
                FARPROC generic;
                MessageBoxWFunction messageBox;
            } function;

            function.generic = GetProcAddress(user32, "MessageBoxW");
            if (function.messageBox != NULL) {
                function.messageBox(NULL, message, APP_NAME, MB_OK | flags | MB_SETFOREGROUND);
            }
            FreeLibrary(user32);
        }
    }
}

static void ShowWin32Error(BOOL silent, const WCHAR *operation, DWORD error)
{
    WCHAR message[512];
    WCHAR systemMessage[320];
    DWORD formatted;

    if (silent) {
        return;
    }

    systemMessage[0] = L'\0';
    formatted = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error,
        0,
        systemMessage,
        (DWORD)(sizeof(systemMessage) / sizeof(systemMessage[0])),
        NULL);

    message[0] = L'\0';
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), operation);
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L" failed.\n\n");

    if (formatted == 0) {
        AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"Windows error ");
        AppendUnsigned(message, (DWORD)(sizeof(message) / sizeof(message[0])), error);
    } else {
        AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), systemMessage);
    }

    ShowMessage(FALSE, MB_ICONERROR, message);
}

static BOOL TouchIconKey(HKEY iconKey)
{
    static const WCHAR emptyString[] = L"";
    LSTATUS setResult;
    LSTATUS deleteResult;

    setResult = RegSetValueExW(
        iconKey,
        REFRESH_VALUE_NAME,
        0,
        REG_SZ,
        (const BYTE *)emptyString,
        sizeof(emptyString));

    if (setResult != ERROR_SUCCESS) {
        return FALSE;
    }

    deleteResult = RegDeleteValueW(iconKey, REFRESH_VALUE_NAME);
    return deleteResult == ERROR_SUCCESS || deleteResult == ERROR_FILE_NOT_FOUND;
}

static BOOL PromoteAll(HKEY root, BOOL touchEveryEntry, PromotionStats *stats)
{
    DWORD index = 0;
    WCHAR subKeyName[256];
    BOOL completed = TRUE;

    ZeroMemory(stats, sizeof(*stats));

    for (;;) {
        DWORD nameLength = (DWORD)(sizeof(subKeyName) / sizeof(subKeyName[0]));
        FILETIME lastWriteTime;
        LSTATUS enumResult = RegEnumKeyExW(
            root,
            index,
            subKeyName,
            &nameLength,
            NULL,
            NULL,
            NULL,
            &lastWriteTime);

        if (enumResult == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (enumResult != ERROR_SUCCESS) {
            stats->errors++;
            completed = FALSE;
            index++;
            continue;
        }

        stats->total++;

        {
            HKEY iconKey = NULL;
            LSTATUS openResult = RegOpenKeyExW(
                root,
                subKeyName,
                0,
                KEY_QUERY_VALUE | KEY_SET_VALUE,
                &iconKey);

            if (openResult == ERROR_SUCCESS) {
                DWORD value = 0;
                DWORD valueSize = sizeof(value);
                DWORD valueType = 0;
                LSTATUS queryResult = RegQueryValueExW(
                    iconKey,
                    PROMOTED_VALUE_NAME,
                    NULL,
                    &valueType,
                    (BYTE *)&value,
                    &valueSize);
                BOOL alreadyPromoted =
                    queryResult == ERROR_SUCCESS &&
                    valueType == REG_DWORD &&
                    valueSize == sizeof(value) &&
                    value == 1;

                if (!alreadyPromoted) {
                    value = 1;
                    if (RegSetValueExW(
                            iconKey,
                            PROMOTED_VALUE_NAME,
                            0,
                            REG_DWORD,
                            (const BYTE *)&value,
                            sizeof(value)) == ERROR_SUCCESS) {
                        stats->changed++;
                        stats->promoted++;
                    } else {
                        stats->errors++;
                        completed = FALSE;
                    }
                } else {
                    stats->promoted++;
                }

                if ((touchEveryEntry || !alreadyPromoted) && !TouchIconKey(iconKey)) {
                    stats->errors++;
                    completed = FALSE;
                }

                RegCloseKey(iconKey);
            } else if (openResult != ERROR_FILE_NOT_FOUND) {
                stats->errors++;
                completed = FALSE;
            }
        }

        index++;
    }

    return completed;
}

static BOOL OpenNotificationRoot(REGSAM access, HKEY *root)
{
    return RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, access, root) == ERROR_SUCCESS;
}

static BOOL PromoteOnce(BOOL touchEveryEntry, PromotionStats *stats)
{
    HKEY root = NULL;
    BOOL result;

    if (!OpenNotificationRoot(KEY_ENUMERATE_SUB_KEYS, &root)) {
        ZeroMemory(stats, sizeof(*stats));
        return FALSE;
    }

    result = PromoteAll(root, touchEveryEntry, stats);
    RegCloseKey(root);
    return result;
}

static BOOL CollectStatus(PromotionStats *stats)
{
    HKEY root = NULL;
    DWORD index = 0;
    WCHAR subKeyName[256];
    BOOL completed = TRUE;

    ZeroMemory(stats, sizeof(*stats));

    if (!OpenNotificationRoot(KEY_ENUMERATE_SUB_KEYS, &root)) {
        return FALSE;
    }

    for (;;) {
        DWORD nameLength = (DWORD)(sizeof(subKeyName) / sizeof(subKeyName[0]));
        LSTATUS enumResult = RegEnumKeyExW(
            root,
            index,
            subKeyName,
            &nameLength,
            NULL,
            NULL,
            NULL,
            NULL);

        if (enumResult == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (enumResult != ERROR_SUCCESS) {
            stats->errors++;
            completed = FALSE;
            index++;
            continue;
        }

        stats->total++;

        {
            HKEY iconKey = NULL;
            LSTATUS openResult = RegOpenKeyExW(root, subKeyName, 0, KEY_QUERY_VALUE, &iconKey);

            if (openResult == ERROR_SUCCESS) {
                DWORD value = 0;
                DWORD valueSize = sizeof(value);
                DWORD valueType = 0;
                LSTATUS queryResult = RegQueryValueExW(
                    iconKey,
                    PROMOTED_VALUE_NAME,
                    NULL,
                    &valueType,
                    (BYTE *)&value,
                    &valueSize);

                if (queryResult == ERROR_SUCCESS &&
                    valueType == REG_DWORD &&
                    valueSize == sizeof(value) &&
                    value == 1) {
                    stats->promoted++;
                }

                RegCloseKey(iconKey);
            } else if (openResult != ERROR_FILE_NOT_FOUND) {
                stats->errors++;
                completed = FALSE;
            }
        }

        index++;
    }

    RegCloseKey(root);
    return completed;
}

static BOOL IsWatcherRunning(void)
{
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME);
    if (mutex == NULL) {
        return FALSE;
    }

    CloseHandle(mutex);
    return TRUE;
}

static void SignalWatcherToStop(void)
{
    HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, STOP_EVENT_NAME);
    if (stopEvent != NULL) {
        SetEvent(stopEvent);
        CloseHandle(stopEvent);
    }
}

static BOOL WaitForWatcherToStop(void)
{
    DWORD attempt;

    for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
        if (!IsWatcherRunning()) {
            return TRUE;
        }
        Sleep(100);
    }

    return !IsWatcherRunning();
}

static int WatchForever(void)
{
    HANDLE mutex;
    HANDLE stopEvent;
    BOOL firstPass = TRUE;

    mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (mutex == NULL) {
        return 10;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    stopEvent = CreateEventW(NULL, TRUE, FALSE, STOP_EVENT_NAME);
    if (stopEvent == NULL) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 11;
    }

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
        HKEY root = NULL;
        HANDLE changedEvent;
        HANDLE waitHandles[2];
        LSTATUS notifyResult;
        DWORD waitResult;
        PromotionStats ignored;

        if (!OpenNotificationRoot(KEY_NOTIFY | KEY_ENUMERATE_SUB_KEYS, &root)) {
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        changedEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (changedEvent == NULL) {
            RegCloseKey(root);
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        notifyResult = RegNotifyChangeKeyValue(
            root,
            TRUE,
            REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
            changedEvent,
            TRUE);

        if (notifyResult != ERROR_SUCCESS) {
            CloseHandle(changedEvent);
            RegCloseKey(root);
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        PromoteAll(root, firstPass, &ignored);
        firstPass = FALSE;

        waitHandles[0] = stopEvent;
        waitHandles[1] = changedEvent;
        waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        CloseHandle(changedEvent);
        RegCloseKey(root);

        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        if (waitResult != WAIT_OBJECT_0 + 1) {
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        if (WaitForSingleObject(stopEvent, DEBOUNCE_MILLISECONDS) == WAIT_OBJECT_0) {
            break;
        }
    }

    CloseHandle(stopEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

static BOOL SetStartupCommand(const WCHAR *installedExecutable)
{
    HKEY runKey = NULL;
    WCHAR command[MAX_PATH + 32];
    LSTATUS result;

    if ((DWORD)lstrlenW(installedExecutable) + 13 >=
        (DWORD)(sizeof(command) / sizeof(command[0]))) {
        return FALSE;
    }

    command[0] = L'"';
    command[1] = L'\0';
    AppendPathPart(command, (DWORD)(sizeof(command) / sizeof(command[0])), installedExecutable);
    AppendPathPart(command, (DWORD)(sizeof(command) / sizeof(command[0])), L"\" --watch");

    result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        RUN_KEY_PATH,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        NULL,
        &runKey,
        NULL);

    if (result != ERROR_SUCCESS) {
        SetLastError((DWORD)result);
        return FALSE;
    }

    result = RegSetValueExW(
        runKey,
        RUN_VALUE_NAME,
        0,
        REG_SZ,
        (const BYTE *)command,
        ((DWORD)lstrlenW(command) + 1) * sizeof(WCHAR));

    RegCloseKey(runKey);
    if (result != ERROR_SUCCESS) {
        SetLastError((DWORD)result);
    }
    return result == ERROR_SUCCESS;
}

static BOOL RemoveStartupCommand(void)
{
    HKEY runKey = NULL;
    LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &runKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        return TRUE;
    }

    if (result != ERROR_SUCCESS) {
        SetLastError((DWORD)result);
        return FALSE;
    }

    result = RegDeleteValueW(runKey, RUN_VALUE_NAME);
    RegCloseKey(runKey);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        SetLastError((DWORD)result);
    }
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

static BOOL StartInstalledWatcher(const WCHAR *installedExecutable)
{
    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION processInfo;
    WCHAR commandLine[MAX_PATH + 32];
    BOOL result;

    if ((DWORD)lstrlenW(installedExecutable) + 13 >=
        (DWORD)(sizeof(commandLine) / sizeof(commandLine[0]))) {
        return FALSE;
    }

    commandLine[0] = L'"';
    commandLine[1] = L'\0';
    AppendPathPart(commandLine, (DWORD)(sizeof(commandLine) / sizeof(commandLine[0])), installedExecutable);
    AppendPathPart(commandLine, (DWORD)(sizeof(commandLine) / sizeof(commandLine[0])), L"\" --watch");

    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&processInfo, sizeof(processInfo));

    result = CreateProcessW(
        installedExecutable,
        commandLine,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &startupInfo,
        &processInfo);

    if (result) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }

    return result;
}

static int Install(BOOL silent)
{
    WCHAR source[MAX_PATH];
    WCHAR installDirectory[MAX_PATH];
    WCHAR installedExecutable[MAX_PATH];
    DWORD attempt;

    if (!GetModulePath(source, (DWORD)(sizeof(source) / sizeof(source[0]))) ||
        !GetInstallPaths(
            installDirectory,
            (DWORD)(sizeof(installDirectory) / sizeof(installDirectory[0])),
            installedExecutable,
            (DWORD)(sizeof(installedExecutable) / sizeof(installedExecutable[0])))) {
        ShowMessage(silent, MB_ICONERROR, L"Could not determine the installation path.");
        return 20;
    }

    SignalWatcherToStop();
    WaitForWatcherToStop();

    if (!CreateDirectoryW(installDirectory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ShowWin32Error(silent, L"Creating the installation directory", GetLastError());
        return 21;
    }

    if (!StringEquals(source, installedExecutable)) {
        BOOL copied = FALSE;
        for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
            if (CopyFileW(source, installedExecutable, FALSE)) {
                copied = TRUE;
                break;
            }
            Sleep(100);
        }

        if (!copied) {
            ShowWin32Error(silent, L"Copying the executable", GetLastError());
            return 22;
        }
    }

    if (!SetStartupCommand(installedExecutable)) {
        ShowWin32Error(silent, L"Creating the startup entry", GetLastError());
        return 23;
    }

    if (!StartInstalledWatcher(installedExecutable)) {
        ShowWin32Error(silent, L"Starting the watcher", GetLastError());
        return 24;
    }

    ShowMessage(
        silent,
        MB_ICONINFORMATION,
        L"Tray Icon Promoter is installed and running.\n\n"
        L"It will start automatically when you sign in and will keep new tray icons visible.");
    return 0;
}

static int Uninstall(BOOL silent)
{
    WCHAR currentExecutable[MAX_PATH];
    WCHAR installDirectory[MAX_PATH];
    WCHAR installedExecutable[MAX_PATH];
    BOOL removedFile = FALSE;

    SignalWatcherToStop();
    WaitForWatcherToStop();

    if (!RemoveStartupCommand()) {
        ShowWin32Error(silent, L"Removing the startup entry", GetLastError());
        return 30;
    }

    if (GetModulePath(currentExecutable, (DWORD)(sizeof(currentExecutable) / sizeof(currentExecutable[0]))) &&
        GetInstallPaths(
            installDirectory,
            (DWORD)(sizeof(installDirectory) / sizeof(installDirectory[0])),
            installedExecutable,
            (DWORD)(sizeof(installedExecutable) / sizeof(installedExecutable[0])))) {
        if (!StringEquals(currentExecutable, installedExecutable)) {
            removedFile = DeleteFileW(installedExecutable) || GetLastError() == ERROR_FILE_NOT_FOUND;
            if (removedFile) {
                RemoveDirectoryW(installDirectory);
            }
        }
    }

    if (removedFile) {
        ShowMessage(silent, MB_ICONINFORMATION, L"Tray Icon Promoter was completely uninstalled.");
    } else {
        ShowMessage(
            silent,
            MB_ICONINFORMATION,
            L"Tray Icon Promoter is stopped and disabled at sign-in.\n\n"
            L"To remove the remaining executable, delete:\n"
            L"%LOCALAPPDATA%\\TrayIconPromoter");
    }

    return 0;
}

static int ShowStatus(BOOL silent)
{
    PromotionStats stats;
    BOOL running = IsWatcherRunning();
    BOOL scanned = CollectStatus(&stats);
    WCHAR message[512];

    if (!scanned) {
        ZeroMemory(&stats, sizeof(stats));
    }

    message[0] = L'\0';
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"Version: ");
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), APP_VERSION);
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"\nWatcher: ");
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), running ? L"running" : L"stopped");
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"\nTray records: ");
    AppendUnsigned(message, (DWORD)(sizeof(message) / sizeof(message[0])), stats.total);
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"\nVisible records: ");
    AppendUnsigned(message, (DWORD)(sizeof(message) / sizeof(message[0])), stats.promoted);
    AppendPathPart(message, (DWORD)(sizeof(message) / sizeof(message[0])), L"\nErrors: ");
    AppendUnsigned(message, (DWORD)(sizeof(message) / sizeof(message[0])), stats.errors);

    ShowMessage(silent, running && stats.errors == 0 ? MB_ICONINFORMATION : MB_ICONWARNING, message);
    return running && scanned && stats.errors == 0 && stats.total == stats.promoted ? 0 : 1;
}

static int SelfTest(BOOL silent)
{
    HKEY root = NULL;
    HKEY testKey = NULL;
    DWORD disposition;
    DWORD value = 0;
    DWORD resultValue = 0;
    DWORD valueType = 0;
    DWORD valueSize = sizeof(resultValue);
    PromotionStats stats;
    BOOL passed = FALSE;

    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            REGISTRY_PATH,
            0,
            NULL,
            REG_OPTION_NON_VOLATILE,
            KEY_ENUMERATE_SUB_KEYS | KEY_CREATE_SUB_KEY,
            NULL,
            &root,
            &disposition) != ERROR_SUCCESS) {
        ShowMessage(silent, MB_ICONERROR, L"Self-test could not open the notification registry key.");
        return 40;
    }

    RegDeleteTreeW(root, SELF_TEST_KEY_NAME);

    if (RegCreateKeyExW(
            root,
            SELF_TEST_KEY_NAME,
            0,
            NULL,
            REG_OPTION_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE,
            NULL,
            &testKey,
            &disposition) == ERROR_SUCCESS) {
        if (RegSetValueExW(
                testKey,
                PROMOTED_VALUE_NAME,
                0,
                REG_DWORD,
                (const BYTE *)&value,
                sizeof(value)) == ERROR_SUCCESS) {
            RegCloseKey(testKey);
            testKey = NULL;

            if (PromoteAll(root, FALSE, &stats) &&
                RegOpenKeyExW(root, SELF_TEST_KEY_NAME, 0, KEY_QUERY_VALUE, &testKey) == ERROR_SUCCESS &&
                RegQueryValueExW(
                    testKey,
                    PROMOTED_VALUE_NAME,
                    NULL,
                    &valueType,
                    (BYTE *)&resultValue,
                    &valueSize) == ERROR_SUCCESS &&
                valueType == REG_DWORD &&
                resultValue == 1) {
                passed = TRUE;
            }
        }
    }

    if (testKey != NULL) {
        RegCloseKey(testKey);
    }
    RegDeleteTreeW(root, SELF_TEST_KEY_NAME);
    RegCloseKey(root);

    ShowMessage(
        silent,
        passed ? MB_ICONINFORMATION : MB_ICONERROR,
        passed ? L"Self-test passed." : L"Self-test failed.");
    return passed ? 0 : 41;
}

static BOOL IsCommandLineSpace(WCHAR character)
{
    return character == L' ' || character == L'\t' || character == L'\r' || character == L'\n';
}

static const WCHAR *ReadCommandLineToken(
    const WCHAR *cursor,
    WCHAR *token,
    DWORD tokenCapacity)
{
    DWORD length = 0;
    BOOL inQuotes = FALSE;

    while (*cursor != L'\0' && IsCommandLineSpace(*cursor)) {
        cursor++;
    }

    if (*cursor == L'\0') {
        token[0] = L'\0';
        return NULL;
    }

    while (*cursor != L'\0') {
        if (*cursor == L'"') {
            inQuotes = !inQuotes;
            cursor++;
            continue;
        }

        if (!inQuotes && IsCommandLineSpace(*cursor)) {
            break;
        }

        if (length + 1 < tokenCapacity) {
            token[length++] = *cursor;
        }
        cursor++;
    }

    token[length] = L'\0';
    return cursor;
}

static RequestedAction ParseAction(const WCHAR *commandLine, BOOL *silent)
{
    const WCHAR *cursor = commandLine;
    WCHAR token[MAX_PATH];
    RequestedAction action = ACTION_DEFAULT;

    *silent = FALSE;

    cursor = ReadCommandLineToken(cursor, token, (DWORD)(sizeof(token) / sizeof(token[0])));
    while (cursor != NULL) {
        cursor = ReadCommandLineToken(cursor, token, (DWORD)(sizeof(token) / sizeof(token[0])));
        if (cursor == NULL && token[0] == L'\0') {
            break;
        }

        if (StringEquals(token, L"--silent")) {
            *silent = TRUE;
        } else if (StringEquals(token, L"--watch")) {
            action = ACTION_WATCH;
        } else if (StringEquals(token, L"--install")) {
            action = ACTION_INSTALL;
        } else if (StringEquals(token, L"--uninstall")) {
            action = ACTION_UNINSTALL;
        } else if (StringEquals(token, L"--once")) {
            action = ACTION_ONCE;
        } else if (StringEquals(token, L"--status")) {
            action = ACTION_STATUS;
        } else if (StringEquals(token, L"--self-test")) {
            action = ACTION_SELF_TEST;
        }
    }

    return action;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR commandLine, int showCommand)
{
    RequestedAction action;
    BOOL silent;
    WCHAR currentExecutable[MAX_PATH];
    WCHAR installDirectory[MAX_PATH];
    WCHAR installedExecutable[MAX_PATH];
    PromotionStats stats;
    int result = 1;

    (void)instance;
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    action = ParseAction(GetCommandLineW(), &silent);

    if (action == ACTION_DEFAULT) {
        if (GetModulePath(currentExecutable, (DWORD)(sizeof(currentExecutable) / sizeof(currentExecutable[0]))) &&
            GetInstallPaths(
                installDirectory,
                (DWORD)(sizeof(installDirectory) / sizeof(installDirectory[0])),
                installedExecutable,
                (DWORD)(sizeof(installedExecutable) / sizeof(installedExecutable[0])))) {
            action = StringEquals(currentExecutable, installedExecutable) ? ACTION_WATCH : ACTION_INSTALL;
        } else {
            action = ACTION_INSTALL;
        }
    }

    switch (action) {
        case ACTION_WATCH:
            result = WatchForever();
            break;
        case ACTION_INSTALL:
            result = Install(silent);
            break;
        case ACTION_UNINSTALL:
            result = Uninstall(silent);
            break;
        case ACTION_ONCE:
            result = PromoteOnce(TRUE, &stats) ? 0 : 1;
            break;
        case ACTION_STATUS:
            result = ShowStatus(silent);
            break;
        case ACTION_SELF_TEST:
            result = SelfTest(silent);
            break;
        default:
            result = 2;
            break;
    }

    return result;
}
