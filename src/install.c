#include "wpi.h"

#include <stdlib.h>
#include <wchar.h>

static HWND s_notify;
static Config *s_cfg;
static volatile LONG s_stop;

static const wchar_t *ext_of(const wchar_t *path)
{
    const wchar_t *dot = wcsrchr(path, L'.');
    const wchar_t *slash = wcsrchr(path, L'\\');
    return dot && (!slash || dot > slash) ? dot : L"";
}

/*
 * Каталог 64-битных системных утилит. 32-битная сборка на x64 иначе
 * запустит cmd/reg из SysWOW64, и твики реестра уйдут в WOW6432Node.
 */
static void system_dir(wchar_t *dir, size_t n)
{
    wchar_t sysnative[MAX_PATH];

    GetWindowsDirectoryW(sysnative, MAX_PATH);
    wcsncat(sysnative, L"\\Sysnative", MAX_PATH - wcslen(sysnative) - 1);
    if (g_sys.x64 && GetFileAttributesW(sysnative) != INVALID_FILE_ATTRIBUTES)
        wfmt(dir, n, L"%ls", sysnative);
    else
        GetSystemDirectoryW(dir, (UINT)n);
}

static void build_cmd(const Item *it, wchar_t *cmd, size_t n)
{
    wchar_t args[4096], sys[MAX_PATH];
    const wchar_t *ext = ext_of(it->path), *p = it->path;

    ExpandEnvironmentStringsW(it->silent ? it->silent_args : it->normal_args, args, ARRAYSIZE(args));
    system_dir(sys, ARRAYSIZE(sys));

    if (!_wcsicmp(ext, L".msi"))
        wfmt(cmd, n, L"\"%ls\\msiexec.exe\" /i \"%ls\" %ls", sys, p, args);
    else if (!_wcsicmp(ext, L".msp"))
        wfmt(cmd, n, L"\"%ls\\msiexec.exe\" /p \"%ls\" %ls", sys, p, args);
    else if (!_wcsicmp(ext, L".msu"))
        wfmt(cmd, n, L"\"%ls\\wusa.exe\" \"%ls\" %ls", sys, p, args);
    else if (!_wcsicmp(ext, L".bat") || !_wcsicmp(ext, L".cmd"))
        wfmt(cmd, n, L"\"%ls\\cmd.exe\" /c \"\"%ls\" %ls\"", sys, p, args);
    else if (!_wcsicmp(ext, L".reg"))
        wfmt(cmd, n, L"\"%ls\\reg.exe\" import \"%ls\"", sys, p);
    else if (!_wcsicmp(ext, L".ps1"))
        wfmt(cmd, n, L"\"%ls\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -ExecutionPolicy Bypass -File \"%ls\" %ls",
             sys, p, args);
    else if (!_wcsicmp(ext, L".vbs") || !_wcsicmp(ext, L".js") || !_wcsicmp(ext, L".wsf"))
        wfmt(cmd, n, L"\"%ls\\wscript.exe\" //nologo \"%ls\" %ls", sys, p, args);
    else
        wfmt(cmd, n, L"\"%ls\" %ls", p, args);
}

/* Возвращает ST_* и код выхода (или код ошибки запуска) */
static int run_item(Item *it)
{
    wchar_t cmd[8192], dir[MAX_PATH * 2];
    STARTUPINFOW si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    wchar_t *slash;
    DWORD code = 0;

    /* CreateProcessW может менять строку, поэтому работаем с копией it->cmd */
    wfmt(cmd, ARRAYSIZE(cmd), L"%ls", it->cmd);

    /* рабочий каталог — папка установщика: многие ищут файлы рядом с собой */
    wfmt(dir, ARRAYSIZE(dir), L"%ls", it->path);
    if ((slash = wcsrchr(dir, L'\\')))
        *slash = 0;

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        it->exit_code = GetLastError();
        return ST_ERROR;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    it->exit_code = code;
    if (code == 0)
        return ST_OK;
    if (code == ERROR_SUCCESS_REBOOT_REQUIRED || code == ERROR_SUCCESS_REBOOT_INITIATED) /* 3010, 1641 */
        return ST_REBOOT;
    return ST_FAIL;
}

static DWORD WINAPI worker(LPVOID unused)
{
    static wchar_t cmd[8192];

    (void)unused;
    for (int i = 0; i < s_cfg->count; i++) {
        Item *it = &s_cfg->items[i];
        if (!it->queued)
            continue;
        if (s_stop) {
            PostMessageW(s_notify, WM_APP_ITEM, (WPARAM)i, ST_SKIPPED);
            continue;
        }
        /* it->cmd готовим до ST_RUNNING: окно читает его, получив это сообщение */
        free(it->cmd);
        build_cmd(it, cmd, ARRAYSIZE(cmd));
        it->cmd = _wcsdup(cmd);
        PostMessageW(s_notify, WM_APP_ITEM, (WPARAM)i, ST_RUNNING);
        PostMessageW(s_notify, WM_APP_ITEM, (WPARAM)i, run_item(it));
    }
    PostMessageW(s_notify, WM_APP_DONE, 0, 0);
    return 0;
}

void install_start(HWND notify, Config *cfg)
{
    HANDLE thread;

    s_notify = notify;
    s_cfg = cfg;
    s_stop = 0;
    thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (thread)
        CloseHandle(thread);
    else
        PostMessageW(notify, WM_APP_DONE, 0, 0);
}

/* Текущий установщик не прерываем — это может сломать систему; останавливаемся после него */
void install_stop(void)
{
    InterlockedExchange(&s_stop, 1);
}
