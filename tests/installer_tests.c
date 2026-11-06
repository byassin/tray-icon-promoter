#include "../src/tray_icon_promoter.c"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed at line %d: %s (error %lu)\n", __LINE__, #condition, GetLastError()); \
    return 1; } } while (0)

static BOOL WriteTestFile(const WCHAR *path, const char *content, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL result;
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    result = WriteFile(file, content, size, &written, NULL) && written == size;
    CloseHandle(file);
    return result;
}

static BOOL FileMatches(const WCHAR *path, const char *content, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    char buffer[32];
    DWORD read = 0;
    BOOL result;
    if (file == INVALID_HANDLE_VALUE || size > sizeof(buffer)) return FALSE;
    result = ReadFile(file, buffer, sizeof(buffer), &read, NULL) && read == size &&
        memcmp(buffer, content, size) == 0;
    CloseHandle(file);
    return result;
}

int main(int argc, char **argv)
{
    WCHAR temporary[MAX_PATH];
    WCHAR directory[MAX_PATH];
    WCHAR source[MAX_PATH];
    WCHAR destination[MAX_PATH];
    WCHAR missing[MAX_PATH];
    WCHAR registryPath[160];
    HKEY key;
    RegistryValueSnapshot snapshot;
    RegistryValueSnapshot absent;
    BYTE expected[] = { 3, 0, 9, 0, 1, 0, 8, 0, 2, 0, 7, 0 };
    BYTE actual[sizeof(expected)];
    DWORD type;
    DWORD size;
    HANDLE locked;

    if (argc > 1) {
        if (strcmp(argv[1], "--failed-update") == 0) {
            HANDLE operation = AcquireOperationLock();
            int result;
            if (operation == NULL) return 26;
            result = Install(TRUE);
            ReleaseOperationLock(operation);
            return result;
        }
        /* This test executable intentionally never becomes a ready watcher. */
        return 0;
    }

    CHECK(GetTempPathW(MAX_PATH, temporary) != 0);
    CHECK(GetTempFileNameW(temporary, L"TIP", 0, directory) != 0);
    CHECK(DeleteFileW(directory));
    CHECK(CreateDirectoryW(directory, NULL));
    lstrcpyW(source, directory);
    CHECK(AppendPathPart(source, MAX_PATH, L"\\source.exe"));
    lstrcpyW(destination, directory);
    CHECK(AppendPathPart(destination, MAX_PATH, L"\\installed.exe"));
    lstrcpyW(missing, directory);
    CHECK(AppendPathPart(missing, MAX_PATH, L"\\missing.exe"));
    CHECK(WriteTestFile(source, "new", 3));
    CHECK(WriteTestFile(destination, "old", 3));
    CHECK(SetFileAttributesW(destination, FILE_ATTRIBUTE_READONLY));

    /* Failed staging never changes either the old contents or its attributes. */
    CHECK(!CopyInstalledExecutable(missing, destination));
    CHECK(FileMatches(destination, "old", 3));
    CHECK((GetFileAttributesW(destination) & FILE_ATTRIBUTE_READONLY) != 0);

    /* Failed atomic replacement must leave the original complete and read-only. */
    locked = CreateFileW(destination, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!CopyInstalledExecutable(source, destination));
    CHECK((GetFileAttributesW(destination) & FILE_ATTRIBUTE_READONLY) != 0);
    CloseHandle(locked);
    CHECK(FileMatches(destination, "old", 3));

    CHECK(CopyInstalledExecutable(source, destination));
    CHECK(FileMatches(destination, "new", 3));
    CHECK((GetFileAttributesW(destination) & FILE_ATTRIBUTE_READONLY) == 0);

    /* Snapshot/restore operates solely on this test's unique HKCU subtree. */
    lstrcpyW(registryPath, L"Software\\TrayIconPromoterInstallerTest-");
    CHECK(AppendUnsigned(registryPath, 160, GetCurrentProcessId()));
    CHECK(AppendPathPart(registryPath, 160, L"-"));
    CHECK(AppendUnsigned(registryPath, 160, GetTickCount()));
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, registryPath, 0, NULL, REG_OPTION_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS);
    CHECK(CaptureRegistryValue(registryPath, &absent) && !absent.exists);
    CHECK(RegSetValueExW(key, RUN_VALUE_NAME, 0, REG_BINARY, expected, sizeof(expected)) == ERROR_SUCCESS);
    CHECK(CaptureRegistryValue(registryPath, &snapshot) && snapshot.exists);
    CHECK(RegDeleteValueW(key, RUN_VALUE_NAME) == ERROR_SUCCESS);
    CHECK(RestoreRegistryValue(registryPath, &snapshot));
    size = sizeof(actual);
    type = 0;
    CHECK(RegQueryValueExW(key, RUN_VALUE_NAME, NULL, &type, actual, &size) == ERROR_SUCCESS);
    CHECK(type == REG_BINARY && size == sizeof(expected) && memcmp(actual, expected, size) == 0);
    CHECK(RestoreRegistryValue(registryPath, &absent));
    CHECK(RegQueryValueExW(key, RUN_VALUE_NAME, NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND);
    FreeRegistrySnapshot(&snapshot);
    FreeRegistrySnapshot(&absent);
    RegCloseKey(key);
    CHECK(RegDeleteKeyW(HKEY_CURRENT_USER, registryPath) == ERROR_SUCCESS);
    CHECK(DeleteFileW(source));
    CHECK(DeleteFileW(destination));
    CHECK(RemoveDirectoryW(directory));
    puts("Atomic replacement and registry rollback snapshot tests passed.");
    return 0;
}
