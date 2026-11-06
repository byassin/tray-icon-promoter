#include "../src/tray_icon_promoter.c"
#include <stdio.h>
#include <wchar.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); goto cleanup; \
} } while (0)

typedef struct WatchTestContext {
    WCHAR registryPath[160];
    WCHAR mutexName[160];
    WCHAR stopName[160];
    WCHAR readyName[160];
} WatchTestContext;

static DWORD WINAPI RunTestWatcher(void *parameter)
{
    WatchTestContext *context = (WatchTestContext *)parameter;
    return (DWORD)WatchRegistryTree(context->registryPath, context->mutexName,
        context->stopName, context->readyName);
}

static BOOL SetTestValue(HKEY root, const WCHAR *name, DWORD type, const void *data, DWORD size)
{
    HKEY icon = NULL;
    LSTATUS result = RegCreateKeyExW(root, name, 0, NULL, REG_OPTION_VOLATILE,
        KEY_SET_VALUE, NULL, &icon, NULL);
    if (result != ERROR_SUCCESS) {
        return FALSE;
    }
    result = RegSetValueExW(icon, PROMOTED_VALUE_NAME, 0, type, (const BYTE *)data, size);
    RegCloseKey(icon);
    return result == ERROR_SUCCESS;
}

static BOOL WaitForIcon(const WCHAR *registryPath)
{
    DWORD attempt;
    for (attempt = 0; attempt < 80; attempt++) {
        HKEY root = NULL;
        PromotionStats stats;
        BOOL promoted = FALSE;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, registryPath, 0, KEY_ENUMERATE_SUB_KEYS, &root) == ERROR_SUCCESS) {
            promoted = CollectStatusFromRoot(root, &stats) && stats.total == 1 && stats.promoted == 1;
            RegCloseKey(root);
        }
        if (promoted) {
            return TRUE;
        }
        Sleep(100);
    }
    return FALSE;
}

static BOOL WaitForReadyState(HANDLE ready, BOOL signaled)
{
    DWORD attempt;
    for (attempt = 0; attempt < 80; attempt++) {
        if ((WaitForSingleObject(ready, 0) == WAIT_OBJECT_0) == signaled) {
            return TRUE;
        }
        Sleep(100);
    }
    return FALSE;
}

int main(void)
{
    WatchTestContext context;
    HKEY root = NULL;
    HKEY missing = NULL;
    HANDLE watcher = NULL;
    HANDLE stop = NULL;
    HANDLE ready = NULL;
    PromotionStats stats;
    DWORD zero = 0;
    DWORD one = 1;
    DWORD threadExit = 1;
    ULONGLONG started;
    const WCHAR malformed[] = L"not-a-dword";
    const BYTE binary[] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    int result = 1;
    unsigned long processId = (unsigned long)GetCurrentProcessId();

    ZeroMemory(&context, sizeof(context));
    swprintf(context.registryPath, 160, L"Software\\TrayIconPromoterRegistryTests-%lu", processId);
    swprintf(context.mutexName, 160, L"Local\\TrayIconPromoter.RegistryTests.Mutex-%lu", processId);
    swprintf(context.stopName, 160, L"Local\\TrayIconPromoter.RegistryTests.Stop-%lu", processId);
    swprintf(context.readyName, 160, L"Local\\TrayIconPromoter.RegistryTests.Ready-%lu", processId);

    /* This unique volatile test subtree is never the real notification tree. */
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, context.registryPath, 0, NULL,
        REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &root, NULL) == ERROR_SUCCESS);
    CHECK(RegCreateKeyExW(root, L"Missing", 0, NULL, REG_OPTION_VOLATILE,
        KEY_SET_VALUE, NULL, &missing, NULL) == ERROR_SUCCESS);
    RegCloseKey(missing);
    missing = NULL;
    CHECK(SetTestValue(root, L"WrongType", REG_SZ, malformed, sizeof(malformed)));
    CHECK(SetTestValue(root, L"WrongSize", REG_BINARY, binary, sizeof(binary)));
    CHECK(SetTestValue(root, L"Correct", REG_DWORD, &one, sizeof(one)));
    CHECK(CollectStatusFromRoot(root, &stats) && stats.total == 4 && stats.promoted == 1);
    CHECK(PromoteAll(root, FALSE, &stats) && stats.changed == 3 && stats.promoted == 4 && stats.errors == 0);
    CHECK(CollectStatusFromRoot(root, &stats) && stats.total == 4 && stats.promoted == 4);

    /* A deleted key handle used to cause an unbounded enumeration loop. */
    CHECK(RegDeleteTreeW(HKEY_CURRENT_USER, context.registryPath) == ERROR_SUCCESS);
    started = GetTickCount64();
    CHECK(!PromoteAll(root, FALSE, &stats) && stats.errors == 1);
    CHECK(!CollectStatusFromRoot(root, &stats) && stats.errors == 1);
    CHECK(GetTickCount64() - started < 1000);
    RegCloseKey(root);
    root = NULL;

    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, context.registryPath, 0, NULL,
        REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &root, NULL) == ERROR_SUCCESS);
    CHECK(SetTestValue(root, L"Icon", REG_DWORD, &zero, sizeof(zero)));
    RegCloseKey(root);
    root = NULL;

    /* Retaining a signaled stop event must not prevent a fresh watcher. */
    stop = CreateEventW(NULL, TRUE, TRUE, context.stopName);
    ready = CreateEventW(NULL, TRUE, FALSE, context.readyName);
    CHECK(stop != NULL && ready != NULL);
    watcher = CreateThread(NULL, 0, RunTestWatcher, &context, 0, NULL);
    CHECK(watcher != NULL);
    CHECK(WaitForReadyState(ready, TRUE) && WaitForIcon(context.registryPath));

    /* Delete/recreate only the isolated root, then verify autonomous recovery. */
    CHECK(RegDeleteTreeW(HKEY_CURRENT_USER, context.registryPath) == ERROR_SUCCESS);
    CHECK(WaitForReadyState(ready, FALSE));
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, context.registryPath, 0, NULL,
        REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &root, NULL) == ERROR_SUCCESS);
    CHECK(SetTestValue(root, L"Icon", REG_DWORD, &zero, sizeof(zero)));
    RegCloseKey(root);
    root = NULL;
    CHECK(WaitForReadyState(ready, TRUE) && WaitForIcon(context.registryPath));
    CHECK(SetEvent(stop));
    CHECK(WaitForSingleObject(watcher, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(watcher, &threadExit) && threadExit == 0);
    CHECK(WaitForSingleObject(ready, 0) == WAIT_TIMEOUT);
    result = 0;
    puts("Registry corruption, deleted-root bounds, stop reset, and watcher recovery tests passed.");

cleanup:
    if (missing != NULL) RegCloseKey(missing);
    if (root != NULL) RegCloseKey(root);
    if (watcher != NULL) {
        if (stop != NULL) SetEvent(stop);
        if (WaitForSingleObject(watcher, 10000) != WAIT_OBJECT_0) {
            /* Fail fast rather than returning while the thread uses this stack. */
            ExitProcess(1);
        }
        CloseHandle(watcher);
    }
    if (stop != NULL) CloseHandle(stop);
    if (ready != NULL) CloseHandle(ready);
    RegDeleteTreeW(HKEY_CURRENT_USER, context.registryPath);
    return result;
}
