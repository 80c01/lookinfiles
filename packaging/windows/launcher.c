/*
 * Single-file Windows launcher for LookinFiles.
 *
 * The portable zip (build_windows_zip.sh) is embedded as resource
 * PAYLOAD_RESOURCE. On the first run the launcher extracts it into
 * %LOCALAPPDATA%\lookinfiles-runtime\<PAYLOAD_ID>\ with the tar.exe of
 * Windows (bsdtar, Windows 10 1803 and later, which reads zip files), then
 * every run starts <that folder>\<DIST_NAME>\lookinfiles.exe with the same
 * arguments and returns its exit code. Folders of other builds are removed.
 *
 * Built by build_windows_exe.sh, which defines PAYLOAD_ID and DIST_NAME.
 * The settings of the application (%LOCALAPPDATA%\lookinfiles) are not
 * touched.
 */
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>

#include "launcher.h"

#ifndef PAYLOAD_ID
#error "PAYLOAD_ID must be defined (build_windows_exe.sh)"
#endif
#ifndef DIST_NAME
#error "DIST_NAME must be defined (build_windows_exe.sh)"
#endif

#define WIDEN2(s) L##s
#define WIDEN(s) WIDEN2(s)

#define RUNTIME_DIR_NAME L"lookinfiles-runtime"
#define DONE_MARKER L".extracted"
#define PATH_LEN 1024

static void fail(const wchar_t *what)
{
    wchar_t msg[1024];
    DWORD err = GetLastError();
    wchar_t *sys = NULL;

    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, (wchar_t *)&sys, 0, NULL);
    _snwprintf(msg, 1023, L"%ls\n\n%ls", what, sys ? sys : L"");
    msg[1023] = L'\0';
    if (sys)
        LocalFree(sys);
    MessageBoxW(NULL, msg, L"LookinFiles", MB_OK | MB_ICONERROR);
}

static BOOL file_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/* Delete a folder and everything in it; failures (files in use) are ignored. */
static void remove_tree(const wchar_t *dir)
{
    wchar_t pattern[PATH_LEN];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    _snwprintf(pattern, PATH_LEN - 1, L"%ls\\*", dir);
    pattern[PATH_LEN - 1] = L'\0';
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child[PATH_LEN];

            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            _snwprintf(child, PATH_LEN - 1, L"%ls\\%ls", dir, fd.cFileName);
            child[PATH_LEN - 1] = L'\0';
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                remove_tree(child);
            } else {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RemoveDirectoryW(child);
                else
                    DeleteFileW(child);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static BOOL starts_with(const wchar_t *s, const wchar_t *prefix)
{
    return wcsncmp(s, prefix, wcslen(prefix)) == 0;
}

/*
 * Remove the folders of other builds under the runtime folder. A folder is
 * first renamed to trash-<pid>-<n>: Windows refuses that while a program in
 * it runs, so a build still in use is kept whole. Extractions in progress
 * (tmp-<pid>) are left alone.
 */
static void remove_old_builds(const wchar_t *root)
{
    wchar_t pattern[PATH_LEN];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    unsigned n = 0;

    _snwprintf(pattern, PATH_LEN - 1, L"%ls\\*", root);
    pattern[PATH_LEN - 1] = L'\0';
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        wchar_t child[PATH_LEN], trash[PATH_LEN];

        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") ||
            !wcscmp(fd.cFileName, WIDEN(PAYLOAD_ID)) || starts_with(fd.cFileName, L"tmp-"))
            continue;
        _snwprintf(child, PATH_LEN - 1, L"%ls\\%ls", root, fd.cFileName);
        child[PATH_LEN - 1] = L'\0';
        if (starts_with(fd.cFileName, L"trash-")) {
            remove_tree(child);
            continue;
        }
        _snwprintf(trash, PATH_LEN - 1, L"%ls\\trash-%lu-%u", root,
                   (unsigned long)GetCurrentProcessId(), n++);
        trash[PATH_LEN - 1] = L'\0';
        if (MoveFileExW(child, trash, 0))
            remove_tree(trash);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static BOOL write_payload(const wchar_t *zip_path)
{
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(PAYLOAD_RESOURCE), MAKEINTRESOURCEW(10));
    HGLOBAL data;
    const char *bytes;
    DWORD size, done = 0;
    HANDLE f;

    if (!res || !(data = LoadResource(NULL, res)) || !(bytes = LockResource(data))) {
        fail(L"The embedded application archive is missing.");
        return FALSE;
    }
    size = SizeofResource(NULL, res);
    f = CreateFileW(zip_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        fail(L"Cannot write the application archive.");
        return FALSE;
    }
    while (done < size) {
        DWORD chunk = size - done > (1u << 24) ? (1u << 24) : size - done;
        DWORD written = 0;

        if (!WriteFile(f, bytes + done, chunk, &written, NULL) || written == 0) {
            fail(L"Cannot write the application archive.");
            CloseHandle(f);
            return FALSE;
        }
        done += written;
    }
    CloseHandle(f);
    return TRUE;
}

/* Run a command line without a console window and return its exit code. */
static DWORD run_hidden(wchar_t *cmdline, const wchar_t *what)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        fail(what);
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

/*
 * Extract the payload into a fresh temporary folder next to build_dir, then
 * rename it to build_dir: an interrupted extraction never looks complete,
 * and two launchers started together do not mix their files.
 */
static BOOL extract(const wchar_t *root, const wchar_t *build_dir)
{
    wchar_t tmp[PATH_LEN], zip[PATH_LEN], tar[MAX_PATH], marker[PATH_LEN];
    wchar_t *cmd;
    DWORD code;
    HANDLE f;
    UINT n;

    _snwprintf(tmp, PATH_LEN - 1, L"%ls\\tmp-%lu", root, (unsigned long)GetCurrentProcessId());
    tmp[PATH_LEN - 1] = L'\0';
    remove_tree(tmp);
    if (!CreateDirectoryW(tmp, NULL)) {
        fail(L"Cannot create the application folder.");
        return FALSE;
    }
    _snwprintf(zip, PATH_LEN - 1, L"%ls\\payload.zip", tmp);
    zip[PATH_LEN - 1] = L'\0';
    if (!write_payload(zip)) {
        remove_tree(tmp);
        return FALSE;
    }

    n = GetSystemDirectoryW(tar, MAX_PATH);
    if (n == 0 || n + 9 > MAX_PATH) {
        fail(L"Cannot find the Windows system folder.");
        remove_tree(tmp);
        return FALSE;
    }
    wcscat(tar, L"\\tar.exe");
    if (!file_exists(tar)) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        fail(L"tar.exe is missing: LookinFiles needs Windows 10 version 1809 or later.");
        remove_tree(tmp);
        return FALSE;
    }

    cmd = HeapAlloc(GetProcessHeap(), 0, 3 * PATH_LEN * sizeof(wchar_t));
    if (!cmd) {
        remove_tree(tmp);
        return FALSE;
    }
    _snwprintf(cmd, 3 * PATH_LEN - 1, L"\"%ls\" -xf \"%ls\" -C \"%ls\"", tar, zip, tmp);
    cmd[3 * PATH_LEN - 1] = L'\0';
    code = run_hidden(cmd, L"Cannot run tar.exe to extract the application.");
    HeapFree(GetProcessHeap(), 0, cmd);
    DeleteFileW(zip);
    if (code != 0) {
        SetLastError(ERROR_BAD_FORMAT);
        fail(L"Extracting the application failed.");
        remove_tree(tmp);
        return FALSE;
    }

    _snwprintf(marker, PATH_LEN - 1, L"%ls\\" DONE_MARKER, tmp);
    marker[PATH_LEN - 1] = L'\0';
    f = CreateFileW(marker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
        CloseHandle(f);

    if (!MoveFileExW(tmp, build_dir, 0)) {
        _snwprintf(marker, PATH_LEN - 1, L"%ls\\" DONE_MARKER, build_dir);
        marker[PATH_LEN - 1] = L'\0';
        if (!file_exists(marker)) {
            /* A folder left by an interrupted older launcher: replace it. */
            remove_tree(build_dir);
            if (!MoveFileExW(tmp, build_dir, 0)) {
                fail(L"Cannot install the application folder.");
                remove_tree(tmp);
                return FALSE;
            }
            return TRUE;
        }
        /* Another launcher finished first: use its folder. */
        remove_tree(tmp);
    }
    return TRUE;
}

/* The command line of this process without its first argument (the program). */
static const wchar_t *arguments(void)
{
    const wchar_t *p = GetCommandLineW();

    if (*p == L'"') {
        for (p++; *p && *p != L'"'; p++)
            ;
        if (*p)
            p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t')
            p++;
    }
    while (*p == L' ' || *p == L'\t')
        p++;
    return p;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show)
{
    wchar_t *local = NULL;
    wchar_t root[PATH_LEN], build_dir[PATH_LEN], marker[PATH_LEN], exe[PATH_LEN];
    wchar_t *cmd;
    const wchar_t *args = arguments();
    size_t len;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 0;

    (void)inst;
    (void)prev;
    (void)cmdline;
    (void)show;

    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local))) {
        fail(L"Cannot find the local application data folder.");
        return 1;
    }
    _snwprintf(root, PATH_LEN - 1, L"%ls\\" RUNTIME_DIR_NAME, local);
    root[PATH_LEN - 1] = L'\0';
    CoTaskMemFree(local);
    _snwprintf(build_dir, PATH_LEN - 1, L"%ls\\%ls", root, WIDEN(PAYLOAD_ID));
    build_dir[PATH_LEN - 1] = L'\0';
    _snwprintf(marker, PATH_LEN - 1, L"%ls\\" DONE_MARKER, build_dir);
    marker[PATH_LEN - 1] = L'\0';
    _snwprintf(exe, PATH_LEN - 1, L"%ls\\%ls\\lookinfiles.exe", build_dir, WIDEN(DIST_NAME));
    exe[PATH_LEN - 1] = L'\0';

    if (!file_exists(marker) || !file_exists(exe)) {
        CreateDirectoryW(root, NULL);
        if (!extract(root, build_dir))
            return 1;
        remove_old_builds(root);
        if (!file_exists(exe)) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            fail(L"lookinfiles.exe is missing from the embedded archive.");
            return 1;
        }
    }

    len = wcslen(exe) + wcslen(args) + 4;
    cmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
    if (!cmd)
        return 1;
    _snwprintf(cmd, len - 1, L"\"%ls\" %ls", exe, args);
    cmd[len - 1] = L'\0';

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fail(L"Cannot start LookinFiles.");
        HeapFree(GetProcessHeap(), 0, cmd);
        return 1;
    }
    HeapFree(GetProcessHeap(), 0, cmd);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
