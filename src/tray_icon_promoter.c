#include <windows.h>

#define APP_NAME L"Tray Icon Promoter"
#define APP_VERSION L"1.2.0"
#define REGISTRY_PATH L"Control Panel\\NotifyIconSettings"
#define RUN_KEY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define STARTUP_APPROVED_KEY_PATH L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define RUN_VALUE_NAME L"TrayIconPromoter"
#define PROMOTED_VALUE_NAME L"IsPromoted"
#define REFRESH_VALUE_NAME L"_temp_tray_icon_promoter_refresh"
#define INSTALL_DIRECTORY_NAME L"TrayIconPromoter"
#define EXECUTABLE_NAME L"TrayIconPromoter.exe"
#define MUTEX_NAME L"Local\\TrayIconPromoter-8D37B978-83D7-4E36-A39C-F08BDB8B5911"
#define STOP_EVENT_NAME L"Local\\TrayIconPromoter.Stop-8D37B978-83D7-4E36-A39C-F08BDB8B5911"
#define READY_EVENT_NAME L"Local\\TrayIconPromoter.Ready-8D37B978-83D7-4E36-A39C-F08BDB8B5911"
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
    ACTION_SELF_TEST,
    ACTION_HELP,
    ACTION_STOP,
    ACTION_RESTART,
    ACTION_STATUS_JSON,
    ACTION_INVALID
} RequestedAction;

typedef enum StartupStatus {
    STARTUP_CONFIGURED,
    STARTUP_DISABLED,
    STARTUP_MISSING,
    STARTUP_MISMATCHED,
    STARTUP_UNKNOWN
} StartupStatus;

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
            /* A deleted/inaccessible root keeps returning the same error. */
            break;
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

static BOOL CollectStatusFromRoot(HKEY root, PromotionStats *stats)
{
    DWORD index = 0;
    WCHAR subKeyName[256];
    BOOL completed = TRUE;

    ZeroMemory(stats, sizeof(*stats));

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
            break;
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
                } else if (queryResult != ERROR_SUCCESS &&
                    queryResult != ERROR_FILE_NOT_FOUND && queryResult != ERROR_MORE_DATA) {
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

static BOOL CollectStatus(PromotionStats *stats)
{
    HKEY root = NULL;
    BOOL completed;
    ZeroMemory(stats, sizeof(*stats));
    if (!OpenNotificationRoot(KEY_ENUMERATE_SUB_KEYS, &root)) {
        stats->errors = 1;
        return FALSE;
    }
    completed = CollectStatusFromRoot(root, stats);
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

static BOOL IsWatcherReady(void)
{
    HANDLE readyEvent = OpenEventW(SYNCHRONIZE, FALSE, READY_EVENT_NAME);
    BOOL ready;
    if (readyEvent == NULL) {
        return FALSE;
    }
    ready = WaitForSingleObject(readyEvent, 0) == WAIT_OBJECT_0;
    CloseHandle(readyEvent);
    return ready && IsWatcherRunning();
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

static int WatchRegistryTree(
    const WCHAR *registryPath,
    const WCHAR *mutexName,
    const WCHAR *stopEventName,
    const WCHAR *readyEventName)
{
    HANDLE mutex;
    HANDLE stopEvent;
    HANDLE readyEvent;
    BOOL firstPass = TRUE;

    mutex = CreateMutexW(NULL, TRUE, mutexName);
    if (mutex == NULL) {
        return 10;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    stopEvent = CreateEventW(NULL, TRUE, FALSE, stopEventName);
    if (stopEvent == NULL) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 11;
    }
    ResetEvent(stopEvent);

    readyEvent = CreateEventW(NULL, TRUE, FALSE, readyEventName);
    if (readyEvent == NULL) {
        CloseHandle(stopEvent);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 12;
    }
    ResetEvent(readyEvent);

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
        HKEY root = NULL;
        HANDLE changedEvent;
        HANDLE waitHandles[2];
        LSTATUS notifyResult;
        DWORD waitResult;
        PromotionStats ignored;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, registryPath, 0,
                KEY_NOTIFY | KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS) {
            ResetEvent(readyEvent);
            firstPass = TRUE;
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        changedEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (changedEvent == NULL) {
            ResetEvent(readyEvent);
            RegCloseKey(root);
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        notifyResult = RegNotifyChangeKeyValue(
            root,
            TRUE,
            REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_SECURITY,
            changedEvent,
            TRUE);

        if (notifyResult != ERROR_SUCCESS) {
            ResetEvent(readyEvent);
            firstPass = TRUE;
            CloseHandle(changedEvent);
            RegCloseKey(root);
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        if (PromoteAll(root, firstPass, &ignored)) {
            SetEvent(readyEvent);
            firstPass = FALSE;
        } else {
            ResetEvent(readyEvent);
            firstPass = TRUE;
        }

        waitHandles[0] = stopEvent;
        waitHandles[1] = changedEvent;
        waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        CloseHandle(changedEvent);
        RegCloseKey(root);

        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        if (waitResult != WAIT_OBJECT_0 + 1) {
            ResetEvent(readyEvent);
            if (WaitForSingleObject(stopEvent, ROOT_RETRY_MILLISECONDS) == WAIT_OBJECT_0) {
                break;
            }
            continue;
        }

        if (WaitForSingleObject(stopEvent, DEBOUNCE_MILLISECONDS) == WAIT_OBJECT_0) {
            break;
        }
    }

    ResetEvent(readyEvent);
    CloseHandle(readyEvent);
    CloseHandle(stopEvent);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

static int WatchForever(void)
{
    return WatchRegistryTree(REGISTRY_PATH, MUTEX_NAME, STOP_EVENT_NAME, READY_EVENT_NAME);
}

static BOOL BuildWatcherCommand(
    const WCHAR *executable,
    WCHAR *command,
    DWORD commandCapacity)
{
    if ((DWORD)lstrlenW(executable) + 13 >= commandCapacity) {
        return FALSE;
    }

    command[0] = L'"';
    command[1] = L'\0';
    return
        AppendPathPart(command, commandCapacity, executable) &&
        AppendPathPart(command, commandCapacity, L"\" --watch");
}

static BOOL RemoveStartupApprovalOverride(void)
{
    HKEY approvalKey = NULL;
    LSTATUS result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        STARTUP_APPROVED_KEY_PATH,
        0,
        KEY_SET_VALUE,
        &approvalKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        return TRUE;
    }

    if (result != ERROR_SUCCESS) {
        SetLastError((DWORD)result);
        return FALSE;
    }

    result = RegDeleteValueW(approvalKey, RUN_VALUE_NAME);
    RegCloseKey(approvalKey);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        SetLastError((DWORD)result);
    }
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

static StartupStatus GetStartupStatus(const WCHAR *installedExecutable)
{
    HKEY runKey = NULL;
    HKEY approvalKey = NULL;
    WCHAR expectedCommand[MAX_PATH + 32];
    WCHAR actualCommand[MAX_PATH + 32];
    BYTE approvalData[16];
    DWORD type = 0;
    DWORD size = sizeof(actualCommand);
    LSTATUS result;

    if (!BuildWatcherCommand(
            installedExecutable,
            expectedCommand,
            (DWORD)(sizeof(expectedCommand) / sizeof(expectedCommand[0])))) {
        return STARTUP_UNKNOWN;
    }

    result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_QUERY_VALUE, &runKey);
    if (result == ERROR_FILE_NOT_FOUND) {
        return STARTUP_MISSING;
    }
    if (result != ERROR_SUCCESS) {
        return STARTUP_UNKNOWN;
    }

    result = RegQueryValueExW(
        runKey,
        RUN_VALUE_NAME,
        NULL,
        &type,
        (BYTE *)actualCommand,
        &size);
    RegCloseKey(runKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        return STARTUP_MISSING;
    }
    if (result != ERROR_SUCCESS ||
        type != REG_SZ ||
        size < sizeof(WCHAR) ||
        size % sizeof(WCHAR) != 0 ||
        size > sizeof(actualCommand) ||
        actualCommand[(size / sizeof(WCHAR)) - 1] != L'\0' ||
        !StringEquals(actualCommand, expectedCommand)) {
        return STARTUP_MISMATCHED;
    }

    result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        STARTUP_APPROVED_KEY_PATH,
        0,
        KEY_QUERY_VALUE,
        &approvalKey);
    if (result == ERROR_FILE_NOT_FOUND) {
        return STARTUP_CONFIGURED;
    }
    if (result != ERROR_SUCCESS) {
        return STARTUP_UNKNOWN;
    }

    type = 0;
    size = sizeof(approvalData);
    result = RegQueryValueExW(
        approvalKey,
        RUN_VALUE_NAME,
        NULL,
        &type,
        approvalData,
        &size);
    RegCloseKey(approvalKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        return STARTUP_CONFIGURED;
    }
    if (result != ERROR_SUCCESS || type != REG_BINARY || size < 1) {
        return STARTUP_UNKNOWN;
    }
    if (approvalData[0] == 2) {
        return STARTUP_CONFIGURED;
    }
    if (approvalData[0] == 3) {
        return STARTUP_DISABLED;
    }
    return STARTUP_UNKNOWN;
}

static const WCHAR *StartupStatusText(StartupStatus status)
{
    switch (status) {
        case STARTUP_CONFIGURED:
            return L"configured";
        case STARTUP_DISABLED:
            return L"disabled in Startup Apps";
        case STARTUP_MISSING:
            return L"missing";
        case STARTUP_MISMATCHED:
            return L"incorrect command";
        default:
            return L"unknown";
    }
}

static BOOL SetStartupCommand(const WCHAR *installedExecutable)
{
    HKEY runKey = NULL;
    WCHAR command[MAX_PATH + 32];
    LSTATUS result;

    if (!BuildWatcherCommand(
            installedExecutable,
            command,
            (DWORD)(sizeof(command) / sizeof(command[0])))) {
        return FALSE;
    }

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
    if (result != ERROR_SUCCESS) {
        return FALSE;
    }

    return RemoveStartupApprovalOverride();
}

static BOOL RemoveStartupCommand(void)
{
    HKEY runKey = NULL;
    LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_SET_VALUE, &runKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        result = ERROR_SUCCESS;
    } else if (result != ERROR_SUCCESS) {
        SetLastError((DWORD)result);
        return FALSE;
    } else {
        result = RegDeleteValueW(runKey, RUN_VALUE_NAME);
        RegCloseKey(runKey);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            SetLastError((DWORD)result);
            return FALSE;
        }
    }

    return RemoveStartupApprovalOverride();
}

static BOOL WaitForWatcherToStart(HANDLE process, HANDLE readyEvent)
{
    DWORD attempt;

    for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
        DWORD waitResult;

        if (WaitForSingleObject(readyEvent, 0) == WAIT_OBJECT_0 &&
            WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            return TRUE;
        }

        waitResult = WaitForSingleObject(process, 100);
        if (waitResult == WAIT_OBJECT_0) {
            SetLastError(ERROR_PROCESS_ABORTED);
            return FALSE;
        }
        if (waitResult == WAIT_FAILED) {
            return FALSE;
        }
    }

    if (WaitForSingleObject(readyEvent, 0) == WAIT_OBJECT_0 &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        return TRUE;
    }

    SetLastError(ERROR_TIMEOUT);
    return FALSE;
}

static BOOL StartInstalledWatcher(const WCHAR *installedExecutable)
{
    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION processInfo;
    WCHAR commandLine[MAX_PATH + 32];
    HANDLE readyEvent;
    BOOL result;

    if (!BuildWatcherCommand(
            installedExecutable,
            commandLine,
            (DWORD)(sizeof(commandLine) / sizeof(commandLine[0])))) {
        return FALSE;
    }

    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&processInfo, sizeof(processInfo));

    readyEvent = CreateEventW(NULL, TRUE, FALSE, READY_EVENT_NAME);
    if (readyEvent == NULL) {
        return FALSE;
    }
    ResetEvent(readyEvent);

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
        DWORD startError;

        CloseHandle(processInfo.hThread);
        result = WaitForWatcherToStart(processInfo.hProcess, readyEvent);
        startError = GetLastError();
        if (!result) {
            /* Only terminate the exact child we created, never another watcher. */
            TerminateProcess(processInfo.hProcess, 24);
            WaitForSingleObject(processInfo.hProcess, 5000);
        }
        CloseHandle(processInfo.hProcess);
        if (!result) {
            SetLastError(startError);
        }
    }

    {
        DWORD startError = GetLastError();
        CloseHandle(readyEvent);
        if (!result) {
            SetLastError(startError);
        }
    }

    return result;
}

typedef struct RegistryValueSnapshot {
    BOOL exists;
    DWORD type;
    DWORD size;
    BYTE *data;
} RegistryValueSnapshot;

static void FreeRegistrySnapshot(RegistryValueSnapshot *snapshot)
{
    if (snapshot->data != NULL) {
        HeapFree(GetProcessHeap(), 0, snapshot->data);
    }
    ZeroMemory(snapshot, sizeof(*snapshot));
}

static BOOL CaptureRegistryValue(const WCHAR *path, RegistryValueSnapshot *snapshot)
{
    HKEY key = NULL;
    LSTATUS result;
    ZeroMemory(snapshot, sizeof(*snapshot));
    result = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND) return TRUE;
    if (result == ERROR_SUCCESS) {
        result = RegQueryValueExW(key, RUN_VALUE_NAME, NULL, &snapshot->type, NULL, &snapshot->size);
        if (result == ERROR_SUCCESS) {
            /* Refuse unreasonable snapshots before changing the installation. */
            if (snapshot->size > 1024 * 1024) {
                result = ERROR_MORE_DATA;
            } else {
                snapshot->data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, snapshot->size == 0 ? 1 : snapshot->size);
                if (snapshot->data == NULL) result = ERROR_NOT_ENOUGH_MEMORY;
                else {
                    result = RegQueryValueExW(key, RUN_VALUE_NAME, NULL, &snapshot->type,
                        snapshot->data, &snapshot->size);
                    snapshot->exists = result == ERROR_SUCCESS;
                }
            }
        }
        RegCloseKey(key);
    }
    if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) return TRUE;
    FreeRegistrySnapshot(snapshot);
    SetLastError((DWORD)result);
    return FALSE;
}

static BOOL RestoreRegistryValue(const WCHAR *path, const RegistryValueSnapshot *snapshot)
{
    HKEY key = NULL;
    LSTATUS result;
    if (snapshot->exists) {
        result = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, NULL, &key, NULL);
        if (result == ERROR_SUCCESS) {
            result = RegSetValueExW(key, RUN_VALUE_NAME, 0, snapshot->type, snapshot->data, snapshot->size);
            RegCloseKey(key);
        }
    } else {
        result = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_SUCCESS) {
            result = RegDeleteValueW(key, RUN_VALUE_NAME);
            RegCloseKey(key);
        }
    }
    if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) return TRUE;
    SetLastError((DWORD)result);
    return FALSE;
}

static HANDLE AcquireOperationLock(void)
{
    HANDLE mutex = CreateMutexW(NULL, FALSE,
        L"Local\\TrayIconPromoter.Setup-8D37B978-83D7-4E36-A39C-F08BDB8B5911");
    DWORD result;
    if (mutex == NULL) return NULL;
    result = WaitForSingleObject(mutex, 10000);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return mutex;
    {
        DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        CloseHandle(mutex);
        SetLastError(error);
    }
    return NULL;
}

static void ReleaseOperationLock(HANDLE mutex)
{
    ReleaseMutex(mutex);
    CloseHandle(mutex);
}

static void DeleteTemporaryExecutable(const WCHAR *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(path, attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    DeleteFileW(path);
}

static BOOL StageExecutable(const WCHAR *source, const WCHAR *destination, WCHAR *staged)
{
    WCHAR directory[MAX_PATH];
    DWORD length = (DWORD)lstrlenW(destination);
    DWORD error;
    if (length >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    lstrcpynW(directory, destination, MAX_PATH);
    while (length > 0 && directory[length - 1] != L'\\' && directory[length - 1] != L'/') length--;
    if (length == 0) {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }
    directory[length] = L'\0';
    if (GetTempFileNameW(directory, L"TIP", 0, staged) == 0) return FALSE;
    if (CopyFileW(source, staged, FALSE)) {
        DWORD attributes = GetFileAttributesW(staged);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            ((attributes & FILE_ATTRIBUTE_READONLY) == 0 ||
             SetFileAttributesW(staged, attributes & ~FILE_ATTRIBUTE_READONLY))) return TRUE;
    }
    error = GetLastError();
    DeleteTemporaryExecutable(staged);
    staged[0] = L'\0';
    SetLastError(error);
    return FALSE;
}

static BOOL CommitStagedExecutable(const WCHAR *staged, const WCHAR *destination)
{
    DWORD originalAttributes = GetFileAttributesW(destination);
    BOOL clearedReadOnly = FALSE;
    BOOL copied = FALSE;
    DWORD copyError = ERROR_SUCCESS;
    DWORD attempt;

    if (originalAttributes == INVALID_FILE_ATTRIBUTES) {
        DWORD attributesError = GetLastError();
        if (attributesError != ERROR_FILE_NOT_FOUND && attributesError != ERROR_PATH_NOT_FOUND) {
            return FALSE;
        }
    } else if ((originalAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        if (!SetFileAttributesW(destination, originalAttributes & ~FILE_ATTRIBUTE_READONLY)) {
            return FALSE;
        }
        clearedReadOnly = TRUE;
    }

    for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
        if (MoveFileExW(staged, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            copied = TRUE;
            break;
        }
        copyError = GetLastError();
        Sleep(100);
    }

    if (!copied) {
        if (clearedReadOnly) {
            SetFileAttributesW(destination, originalAttributes);
        }
        SetLastError(copyError);
        return FALSE;
    }

    /* StageExecutable already made the committed file writable. */
    return TRUE;
}

static BOOL CopyInstalledExecutable(const WCHAR *source, const WCHAR *destination)
{
    WCHAR staged[MAX_PATH];
    BOOL copied;
    DWORD error;
    if (!StageExecutable(source, destination, staged)) return FALSE;
    copied = CommitStagedExecutable(staged, destination);
    error = GetLastError();
    if (!copied) DeleteTemporaryExecutable(staged);
    if (!copied) SetLastError(error);
    return copied;
}

static int Install(BOOL silent)
{
    WCHAR source[MAX_PATH];
    WCHAR installDirectory[MAX_PATH];
    WCHAR installedExecutable[MAX_PATH];
    WCHAR staged[MAX_PATH] = L"";
    WCHAR backup[MAX_PATH] = L"";
    RegistryValueSnapshot runSnapshot;
    RegistryValueSnapshot approvalSnapshot;
    DWORD oldAttributes;
    DWORD failure = ERROR_SUCCESS;
    const WCHAR *operation = L"Preparing installation";
    BOOL wasRunning = IsWatcherRunning();
    BOOL stopped = FALSE;
    BOOL replaced = FALSE;
    BOOL startupTouched = FALSE;
    BOOL snapshotsCaptured = FALSE;
    BOOL recoveryFailed = FALSE;
    int exitCode = 20;
    ZeroMemory(&runSnapshot, sizeof(runSnapshot));
    ZeroMemory(&approvalSnapshot, sizeof(approvalSnapshot));

    if (!GetModulePath(source, (DWORD)(sizeof(source) / sizeof(source[0]))) ||
        !GetInstallPaths(
            installDirectory,
            (DWORD)(sizeof(installDirectory) / sizeof(installDirectory[0])),
            installedExecutable,
            (DWORD)(sizeof(installedExecutable) / sizeof(installedExecutable[0])))) {
        ShowMessage(silent, MB_ICONERROR, L"Could not determine the installation path.");
        return 20;
    }

    if (!CreateDirectoryW(installDirectory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ShowWin32Error(silent, L"Creating the installation directory", GetLastError());
        return 21;
    }

    oldAttributes = GetFileAttributesW(installedExecutable);
    if (oldAttributes == INVALID_FILE_ATTRIBUTES &&
        GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) goto failed;
    if (!CaptureRegistryValue(RUN_KEY_PATH, &runSnapshot) ||
        !CaptureRegistryValue(STARTUP_APPROVED_KEY_PATH, &approvalSnapshot)) goto failed;
    snapshotsCaptured = TRUE;

    if (!StringEquals(source, installedExecutable)) {
        operation = L"Staging the executable";
        exitCode = 22;
        if (!StageExecutable(source, installedExecutable, staged)) goto failed;
        if (oldAttributes != INVALID_FILE_ATTRIBUTES &&
            !StageExecutable(installedExecutable, installedExecutable, backup)) goto failed;
    }

    operation = L"Stopping the existing watcher";
    exitCode = 25;
    SignalWatcherToStop();
    if (!WaitForWatcherToStop()) {
        SetLastError(ERROR_TIMEOUT);
        goto failed;
    }
    stopped = TRUE;

    if (staged[0] != L'\0') {
        operation = L"Replacing the executable";
        exitCode = 22;
        if (!CommitStagedExecutable(staged, installedExecutable)) goto failed;
        replaced = TRUE;
        staged[0] = L'\0';
    } else if (oldAttributes != INVALID_FILE_ATTRIBUTES &&
               (oldAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        if (!SetFileAttributesW(installedExecutable, oldAttributes & ~FILE_ATTRIBUTE_READONLY)) {
            operation = L"Clearing the read-only executable attribute";
            exitCode = 22;
            goto failed;
        }
    }

    operation = L"Creating the startup entry";
    exitCode = 23;
    startupTouched = TRUE;
    if (!SetStartupCommand(installedExecutable)) {
        goto failed;
    }

    operation = L"Starting the watcher";
    exitCode = 24;
    if (!StartInstalledWatcher(installedExecutable)) {
        goto failed;
    }

    if (backup[0] != L'\0') DeleteTemporaryExecutable(backup);
    FreeRegistrySnapshot(&runSnapshot);
    FreeRegistrySnapshot(&approvalSnapshot);
    ShowMessage(
        silent,
        MB_ICONINFORMATION,
        L"Tray Icon Promoter is installed and running.\n\n"
        L"It will start automatically when you sign in and will keep new tray icons visible.");
    return 0;

failed:
    failure = GetLastError();
    {
        BOOL restored = TRUE;
        if (replaced) {
            if (backup[0] != L'\0') {
                restored = CopyInstalledExecutable(backup, installedExecutable);
                if (restored && oldAttributes != INVALID_FILE_ATTRIBUTES) {
                    restored = SetFileAttributesW(installedExecutable, oldAttributes);
                }
            } else {
                DeleteTemporaryExecutable(installedExecutable);
                restored = GetFileAttributesW(installedExecutable) == INVALID_FILE_ATTRIBUTES;
            }
        } else if (stopped && oldAttributes != INVALID_FILE_ATTRIBUTES) {
            restored = SetFileAttributesW(installedExecutable, oldAttributes);
        }
        if (snapshotsCaptured && startupTouched) {
            if (!RestoreRegistryValue(RUN_KEY_PATH, &runSnapshot)) restored = FALSE;
            if (!RestoreRegistryValue(STARTUP_APPROVED_KEY_PATH, &approvalSnapshot)) restored = FALSE;
        }
        if (stopped && wasRunning && restored && !StartInstalledWatcher(installedExecutable)) restored = FALSE;
        if (!restored) {
            WCHAR message[1024];
            recoveryFailed = TRUE;
            lstrcpyW(message, L"Installation failed and the previous state could not be fully restored.\n"
                L"Run --status, then repair from a downloaded copy.\nRecovery executable: ");
            AppendPathPart(message, 1024, backup[0] != L'\0' ? backup : installedExecutable);
            ShowMessage(silent, MB_ICONERROR, message);
            exitCode = 26;
        } else {
            ShowWin32Error(silent, operation, failure);
        }
    }
    if (staged[0] != L'\0') DeleteTemporaryExecutable(staged);
    if (!recoveryFailed && backup[0] != L'\0') DeleteTemporaryExecutable(backup);
    FreeRegistrySnapshot(&runSnapshot);
    FreeRegistrySnapshot(&approvalSnapshot);
    SetLastError(failure);
    return exitCode;
}

static int StopWatcherAction(BOOL silent)
{
    SignalWatcherToStop();
    if (!WaitForWatcherToStop()) {
        ShowWin32Error(silent, L"Stopping the watcher", ERROR_TIMEOUT);
        return 31;
    }
    ShowMessage(silent, MB_ICONINFORMATION,
        L"Tray Icon Promoter is stopped. Its startup setting is unchanged.\n"
        L"Run --restart to start it again now.");
    return 0;
}

static int RestartWatcherAction(BOOL silent)
{
    WCHAR directory[MAX_PATH];
    WCHAR executable[MAX_PATH];
    DWORD attributes;
    if (!GetInstallPaths(directory, MAX_PATH, executable, MAX_PATH)) {
        ShowMessage(silent, MB_ICONERROR, L"Could not determine the installation path.");
        return 20;
    }
    attributes = GetFileAttributesW(executable);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        ShowMessage(silent, MB_ICONERROR, L"The installed executable is missing. Run --install first.");
        return 24;
    }
    SignalWatcherToStop();
    if (!WaitForWatcherToStop()) {
        ShowWin32Error(silent, L"Stopping the watcher", ERROR_TIMEOUT);
        return 31;
    }
    if (!StartInstalledWatcher(executable)) {
        ShowWin32Error(silent, L"Restarting the watcher", GetLastError());
        return 24;
    }
    ShowMessage(silent, MB_ICONINFORMATION,
        L"Tray Icon Promoter is running again. Its startup setting is unchanged.");
    return 0;
}

static int Uninstall(BOOL silent)
{
    WCHAR currentExecutable[MAX_PATH];
    WCHAR installDirectory[MAX_PATH];
    WCHAR installedExecutable[MAX_PATH];
    BOOL removedFile = FALSE;

    if (!GetModulePath(currentExecutable, MAX_PATH) ||
        !GetInstallPaths(installDirectory, MAX_PATH, installedExecutable, MAX_PATH)) {
        ShowMessage(silent, MB_ICONERROR, L"Could not determine the installation path.");
        return 20;
    }

    SignalWatcherToStop();
    if (!WaitForWatcherToStop()) {
        SetLastError(ERROR_TIMEOUT);
        ShowWin32Error(silent, L"Stopping the watcher", GetLastError());
        return 31;
    }

    if (!RemoveStartupCommand()) {
        ShowWin32Error(silent, L"Removing the startup entry", GetLastError());
        return 30;
    }

    if (!StringEquals(currentExecutable, installedExecutable)) {
        DWORD originalAttributes = GetFileAttributesW(installedExecutable);
        DWORD attempt;
        DWORD deleteError = ERROR_SUCCESS;
        if (originalAttributes != INVALID_FILE_ATTRIBUTES &&
            (originalAttributes & FILE_ATTRIBUTE_READONLY) != 0 &&
            !SetFileAttributesW(installedExecutable, originalAttributes & ~FILE_ATTRIBUTE_READONLY)) {
            ShowWin32Error(silent, L"Removing the executable", GetLastError());
            return 32;
        }
        for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
            if (DeleteFileW(installedExecutable)) {
                removedFile = TRUE;
                break;
            }
            deleteError = GetLastError();
            if (deleteError == ERROR_FILE_NOT_FOUND || deleteError == ERROR_PATH_NOT_FOUND) {
                removedFile = TRUE;
                break;
            }
            Sleep(100);
        }
        if (!removedFile) {
            if (originalAttributes != INVALID_FILE_ATTRIBUTES) SetFileAttributesW(installedExecutable, originalAttributes);
            ShowWin32Error(silent, L"Removing the executable (watcher stopped; startup removed)", deleteError);
            return 32;
        }
        RemoveDirectoryW(installDirectory);
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

static BOOL SelfTestReadOnlyRepair(void)
{
    WCHAR source[MAX_PATH];
    WCHAR temporaryDirectory[MAX_PATH];
    WCHAR temporaryFile[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA sourceData;
    WIN32_FILE_ATTRIBUTE_DATA installedData;
    DWORD temporaryDirectoryLength;
    DWORD installedAttributes;
    BOOL passed = FALSE;

    if (!GetModulePath(source, (DWORD)(sizeof(source) / sizeof(source[0])))) {
        return FALSE;
    }

    temporaryDirectoryLength = GetTempPathW(
        (DWORD)(sizeof(temporaryDirectory) / sizeof(temporaryDirectory[0])),
        temporaryDirectory);
    if (temporaryDirectoryLength == 0 ||
        temporaryDirectoryLength >= (DWORD)(sizeof(temporaryDirectory) / sizeof(temporaryDirectory[0])) ||
        GetTempFileNameW(temporaryDirectory, L"TIP", 0, temporaryFile) == 0) {
        return FALSE;
    }

    if (SetFileAttributesW(temporaryFile, FILE_ATTRIBUTE_READONLY) &&
        CopyInstalledExecutable(source, temporaryFile) &&
        GetFileAttributesExW(source, GetFileExInfoStandard, &sourceData) &&
        GetFileAttributesExW(temporaryFile, GetFileExInfoStandard, &installedData)) {
        installedAttributes = installedData.dwFileAttributes;
        passed =
            (installedAttributes & FILE_ATTRIBUTE_READONLY) == 0 &&
            sourceData.nFileSizeHigh == installedData.nFileSizeHigh &&
            sourceData.nFileSizeLow == installedData.nFileSizeLow;
    }

    installedAttributes = GetFileAttributesW(temporaryFile);
    if (installedAttributes != INVALID_FILE_ATTRIBUTES &&
        (installedAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(temporaryFile, installedAttributes & ~FILE_ATTRIBUTE_READONLY);
    }

    return DeleteFileW(temporaryFile) && passed;
}

typedef struct AppStatus {
    PromotionStats stats;
    BOOL running;
    BOOL ready;
    BOOL scanned;
    BOOL installed;
    BOOL writable;
    BOOL healthy;
    StartupStatus startup;
    WCHAR path[MAX_PATH];
    WCHAR installedVersion[64];
    const WCHAR *recommendation;
} AppStatus;

static BOOL ReadFileVersion(const WCHAR *path, WCHAR *version, DWORD capacity)
{
    DWORD ignored;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    BYTE *data;
    VS_FIXEDFILEINFO *info = NULL;
    UINT infoSize = 0;
    BOOL result = FALSE;
    if (size == 0 || size > 1024 * 1024) return FALSE;
    data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, size);
    if (data == NULL) return FALSE;
    if (GetFileVersionInfoW(path, 0, size, data) &&
        VerQueryValueW(data, L"\\", (LPVOID *)&info, &infoSize) &&
        infoSize >= sizeof(*info) && info->dwSignature == 0xFEEF04BD) {
        version[0] = L'\0';
        result = AppendUnsigned(version, capacity, HIWORD(info->dwFileVersionMS)) &&
            AppendPathPart(version, capacity, L".") &&
            AppendUnsigned(version, capacity, LOWORD(info->dwFileVersionMS)) &&
            AppendPathPart(version, capacity, L".") &&
            AppendUnsigned(version, capacity, HIWORD(info->dwFileVersionLS));
    }
    HeapFree(GetProcessHeap(), 0, data);
    return result;
}

static void GetAppStatus(AppStatus *status)
{
    WCHAR directory[MAX_PATH];
    DWORD attributes;
    ZeroMemory(status, sizeof(*status));
    status->startup = STARTUP_UNKNOWN;
    lstrcpyW(status->installedVersion, L"unknown");
    status->running = IsWatcherRunning();
    status->ready = status->running && IsWatcherReady();
    status->scanned = CollectStatus(&status->stats);
    if (GetInstallPaths(directory, MAX_PATH, status->path, MAX_PATH)) {
        status->startup = GetStartupStatus(status->path);
        attributes = GetFileAttributesW(status->path);
        status->installed = attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        status->writable = status->installed && (attributes & FILE_ATTRIBUTE_READONLY) == 0;
        if (status->installed) ReadFileVersion(status->path, status->installedVersion, 64);
    }
    status->healthy = status->ready && status->scanned && status->stats.errors == 0 &&
        status->stats.total == status->stats.promoted && status->startup == STARTUP_CONFIGURED &&
        status->writable;
    if (!status->installed || !status->writable) {
        status->recommendation = L"Run --install from a downloaded copy to repair the installation.";
    } else if (!status->running) {
        status->recommendation = L"Run --restart to start the installed watcher.";
    } else if (!status->ready || !status->scanned || status->stats.errors != 0) {
        status->recommendation = L"Registry monitoring is not healthy; the watcher retries automatically. Use --restart if it persists.";
    } else if (status->startup == STARTUP_DISABLED) {
        status->recommendation = L"Enable Tray Icon Promoter in Windows Startup Apps.";
    } else if (status->startup != STARTUP_CONFIGURED) {
        status->recommendation = L"Run --install to repair the startup registration.";
    } else if (status->stats.total != status->stats.promoted) {
        status->recommendation = L"Run --once to refresh icons, or --restart to restart monitoring.";
    } else {
        status->recommendation = L"No action needed.";
    }
}

static BOOL AppendJsonString(WCHAR *text, DWORD capacity, const WCHAR *value)
{
    static const WCHAR hex[] = L"0123456789abcdef";
    if (!AppendPathPart(text, capacity, L"\"")) return FALSE;
    while (*value != L'\0') {
        WCHAR escaped[7];
        WCHAR character = *value++;
        if (character == L'"' || character == L'\\') {
            escaped[0] = L'\\'; escaped[1] = character; escaped[2] = L'\0';
        } else if (character < 32 || character > 126) {
            escaped[0] = L'\\'; escaped[1] = L'u';
            escaped[2] = hex[(character >> 12) & 15];
            escaped[3] = hex[(character >> 8) & 15];
            escaped[4] = hex[(character >> 4) & 15];
            escaped[5] = hex[character & 15]; escaped[6] = L'\0';
        } else {
            escaped[0] = character; escaped[1] = L'\0';
        }
        if (!AppendPathPart(text, capacity, escaped)) return FALSE;
    }
    return AppendPathPart(text, capacity, L"\"");
}

static BOOL BuildStatusJson(const AppStatus *status, WCHAR *text, DWORD capacity)
{
    text[0] = L'\0';
    return AppendPathPart(text, capacity, L"{\"version\":") &&
        AppendJsonString(text, capacity, APP_VERSION) &&
        AppendPathPart(text, capacity, L",\"installed_version\":") &&
        AppendJsonString(text, capacity, status->installedVersion) &&
        AppendPathPart(text, capacity, L",\"installed_path\":") &&
        AppendJsonString(text, capacity, status->path) &&
        AppendPathPart(text, capacity, status->running ? L",\"watcher_running\":true" : L",\"watcher_running\":false") &&
        AppendPathPart(text, capacity, status->ready ? L",\"watcher_ready\":true" : L",\"watcher_ready\":false") &&
        AppendPathPart(text, capacity, L",\"startup\":") &&
        AppendJsonString(text, capacity, StartupStatusText(status->startup)) &&
        AppendPathPart(text, capacity, status->writable ? L",\"installed_writable\":true" : L",\"installed_writable\":false") &&
        AppendPathPart(text, capacity, status->scanned ? L",\"scan_complete\":true" : L",\"scan_complete\":false") &&
        AppendPathPart(text, capacity, L",\"records\":") && AppendUnsigned(text, capacity, status->stats.total) &&
        AppendPathPart(text, capacity, L",\"promoted\":") && AppendUnsigned(text, capacity, status->stats.promoted) &&
        AppendPathPart(text, capacity, L",\"errors\":") && AppendUnsigned(text, capacity, status->stats.errors) &&
        AppendPathPart(text, capacity, status->healthy ? L",\"healthy\":true" : L",\"healthy\":false") &&
        AppendPathPart(text, capacity, L",\"recommendation\":") &&
        AppendJsonString(text, capacity, status->recommendation) && AppendPathPart(text, capacity, L"}\r\n");
}

static int ShowStatus(BOOL silent, BOOL json)
{
    AppStatus status;
    WCHAR message[4096];
    GetAppStatus(&status);
    if (json) {
        CHAR output[4096];
        DWORD size;
        DWORD written = 0;
        HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!BuildStatusJson(&status, message, 4096)) return 3;
        size = (DWORD)lstrlenW(message);
        for (written = 0; written < size; written++) output[written] = (CHAR)message[written];
        if (stdoutHandle == NULL || stdoutHandle == INVALID_HANDLE_VALUE ||
            !WriteFile(stdoutHandle, output, size, &written, NULL) || written != size) {
            ShowMessage(silent, MB_ICONERROR, L"Could not write JSON. Redirect standard output to a file with Start-Process -RedirectStandardOutput.");
            return 3;
        }
    } else {
        message[0] = L'\0';
        AppendPathPart(message, 4096, L"Diagnostic version: " APP_VERSION L"\nInstalled version: ");
        AppendPathPart(message, 4096, status.installedVersion);
        AppendPathPart(message, 4096, L"\nWatcher: ");
        AppendPathPart(message, 4096, status.ready ? L"running and ready" : (status.running ? L"running, recovering" : L"stopped"));
        AppendPathPart(message, 4096, L"\nStartup: ");
        AppendPathPart(message, 4096, StartupStatusText(status.startup));
        AppendPathPart(message, 4096, L"\nInstalled file: ");
        AppendPathPart(message, 4096, !status.installed ? L"missing" : (status.writable ? L"present and writable" : L"read-only"));
        AppendPathPart(message, 4096, L"\nInstalled path: ");
        AppendPathPart(message, 4096, status.path);
        AppendPathPart(message, 4096, L"\nTray records: "); AppendUnsigned(message, 4096, status.stats.total);
        AppendPathPart(message, 4096, L"\nPromoted records: "); AppendUnsigned(message, 4096, status.stats.promoted);
        AppendPathPart(message, 4096, L"\nScan: "); AppendPathPart(message, 4096, status.scanned ? L"complete" : L"incomplete");
        AppendPathPart(message, 4096, L"\nErrors: "); AppendUnsigned(message, 4096, status.stats.errors);
        AppendPathPart(message, 4096, L"\n\n"); AppendPathPart(message, 4096, status.recommendation);
        ShowMessage(silent, status.healthy ? MB_ICONINFORMATION : MB_ICONWARNING, message);
    }
    return status.healthy ? 0 : 1;
}

static BOOL WaitForTestPromotion(HKEY root)
{
    DWORD attempt;
    for (attempt = 0; attempt < STOP_WAIT_ATTEMPTS; attempt++) {
        HKEY testKey = NULL;
        DWORD value = 0;
        DWORD type = 0;
        DWORD size = sizeof(value);
        BOOL promoted = FALSE;
        if (RegOpenKeyExW(root, SELF_TEST_KEY_NAME, 0, KEY_QUERY_VALUE, &testKey) == ERROR_SUCCESS) {
            promoted = RegQueryValueExW(testKey, PROMOTED_VALUE_NAME, NULL, &type,
                (BYTE *)&value, &size) == ERROR_SUCCESS &&
                type == REG_DWORD && size == sizeof(value) && value == 1;
            RegCloseKey(testKey);
        }
        if (promoted) {
            return TRUE;
        }
        Sleep(100);
    }
    return FALSE;
}

static int SelfTest(BOOL silent)
{
    HKEY root = NULL;
    HKEY testKey = NULL;
    WCHAR source[MAX_PATH];
    DWORD disposition;
    DWORD value = 0;
    BOOL watcherWasRunning = IsWatcherRunning();
    BOOL startedWatcher = FALSE;
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

    if (!watcherWasRunning) {
        if (!GetModulePath(source, (DWORD)(sizeof(source) / sizeof(source[0]))) ||
            !StartInstalledWatcher(source)) {
            RegCloseKey(root);
            ShowMessage(silent, MB_ICONERROR, L"Self-test could not start the watcher.");
            return 41;
        }
        startedWatcher = TRUE;
    }

    /* Allow notifications from the initial scan to drain before changing data. */
    Sleep(DEBOUNCE_MILLISECONDS * 4);
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

            passed = WaitForTestPromotion(root);
            if (passed) {
                /* Verify a later value change as well as a new registration. */
                Sleep(DEBOUNCE_MILLISECONDS * 4);
                if (RegOpenKeyExW(root, SELF_TEST_KEY_NAME, 0, KEY_SET_VALUE, &testKey) == ERROR_SUCCESS) {
                    passed = RegSetValueExW(testKey, PROMOTED_VALUE_NAME, 0, REG_DWORD,
                        (const BYTE *)&value, sizeof(value)) == ERROR_SUCCESS;
                    RegCloseKey(testKey);
                    testKey = NULL;
                    passed = passed && WaitForTestPromotion(root);
                } else {
                    passed = FALSE;
                }
            }
        }
    }

    if (startedWatcher) {
        SignalWatcherToStop();
        if (!WaitForWatcherToStop()) {
            passed = FALSE;
        }
    }

    if (testKey != NULL) {
        RegCloseKey(testKey);
    }
    RegDeleteTreeW(root, SELF_TEST_KEY_NAME);
    RegCloseKey(root);

    passed = SelfTestReadOnlyRepair() && passed;

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
    DWORD tokenCapacity,
    BOOL *valid)
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
        } else {
            *valid = FALSE;
        }
        cursor++;
    }

    token[length] = L'\0';
    if (inQuotes) {
        *valid = FALSE;
    }
    return cursor;
}

static RequestedAction ParseAction(const WCHAR *commandLine, BOOL *silent)
{
    const WCHAR *cursor = commandLine;
    WCHAR token[MAX_PATH];
    RequestedAction action = ACTION_DEFAULT;
    BOOL invalid = FALSE;
    BOOL valid = TRUE;

    *silent = FALSE;

    cursor = ReadCommandLineToken(cursor, token, (DWORD)(sizeof(token) / sizeof(token[0])), &valid);
    while (cursor != NULL) {
        cursor = ReadCommandLineToken(cursor, token, (DWORD)(sizeof(token) / sizeof(token[0])), &valid);
        if (cursor == NULL && token[0] == L'\0') {
            break;
        }

        if (StringEquals(token, L"--silent")) {
            *silent = TRUE;
        } else {
            RequestedAction next = ACTION_INVALID;
            if (StringEquals(token, L"--watch")) next = ACTION_WATCH;
            else if (StringEquals(token, L"--install")) next = ACTION_INSTALL;
            else if (StringEquals(token, L"--uninstall")) next = ACTION_UNINSTALL;
            else if (StringEquals(token, L"--once")) next = ACTION_ONCE;
            else if (StringEquals(token, L"--status")) next = ACTION_STATUS;
            else if (StringEquals(token, L"--self-test")) next = ACTION_SELF_TEST;
            else if (StringEquals(token, L"--help")) next = ACTION_HELP;
            else if (StringEquals(token, L"--stop")) next = ACTION_STOP;
            else if (StringEquals(token, L"--restart")) next = ACTION_RESTART;
            else if (StringEquals(token, L"--status-json")) next = ACTION_STATUS_JSON;
            if (next == ACTION_INVALID || action != ACTION_DEFAULT) invalid = TRUE;
            action = next;
        }
    }

    return invalid || !valid ? ACTION_INVALID : action;
}

static void ShowHelp(BOOL silent, BOOL invalid)
{
    ShowMessage(silent, invalid ? MB_ICONERROR : MB_ICONINFORMATION,
        L"Tray Icon Promoter " APP_VERSION L"\n\n"
        L"Choose one action:\n"
        L"--install     Install or repair\n"
        L"--uninstall   Stop and uninstall\n"
        L"--watch       Run the watcher\n"
        L"--once        Promote current icons and exit\n"
        L"--status      Check installation health\n"
        L"--status-json Write JSON diagnostics to standard output\n"
        L"--stop        Stop until restart or next sign-in\n"
        L"--restart     Restart without changing startup settings\n"
        L"--self-test   Test notifications and repair\n"
        L"--help        Show this help\n\n"
        L"Add --silent to suppress message boxes.\n"
        L"No action: install from a downloaded copy, watch from the installed copy.\n"
        L"Unknown arguments or multiple actions return exit code 2.");
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
    HANDLE operation = NULL;

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

    if (action == ACTION_INSTALL || action == ACTION_UNINSTALL ||
        action == ACTION_STOP || action == ACTION_RESTART || action == ACTION_SELF_TEST) {
        operation = AcquireOperationLock();
        if (operation == NULL) {
            ShowWin32Error(silent, L"Waiting for another install, uninstall, or restart", GetLastError());
            return 26;
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
            result = ShowStatus(silent, FALSE);
            break;
        case ACTION_STATUS_JSON:
            result = ShowStatus(silent, TRUE);
            break;
        case ACTION_STOP:
            result = StopWatcherAction(silent);
            break;
        case ACTION_RESTART:
            result = RestartWatcherAction(silent);
            break;
        case ACTION_SELF_TEST:
            result = SelfTest(silent);
            break;
        case ACTION_HELP:
            ShowHelp(silent, FALSE);
            result = 0;
            break;
        default:
            ShowHelp(silent, TRUE);
            result = 2;
            break;
    }

    if (operation != NULL) ReleaseOperationLock(operation);
    return result;
}
