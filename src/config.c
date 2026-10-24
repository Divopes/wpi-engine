#include "wpi.h"
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

wchar_t g_root[MAX_PATH];
SysInfo g_sys;

typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
typedef BOOL (WINAPI *IsWow64Process_t)(HANDLE, PBOOL);

void wfmt(wchar_t *buf, size_t n, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, n - 1, fmt, ap);
    buf[n - 1] = 0;
    va_end(ap);
}

void sys_detect(void)
{
    OSVERSIONINFOW vi = { .dwOSVersionInfoSize = sizeof vi };
    /* GetVersionEx врёт начиная с Windows 8.1, RtlGetVersion — нет */
    RtlGetVersion_t rtl_get_version =
        (RtlGetVersion_t)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (!rtl_get_version || rtl_get_version(&vi) != 0)
        GetVersionExW(&vi);
    g_sys.major = vi.dwMajorVersion;
    g_sys.minor = vi.dwMinorVersion;
    g_sys.build = vi.dwBuildNumber;

#ifdef _WIN64
    g_sys.x64 = 1;
#else
    BOOL wow = FALSE;
    IsWow64Process_t is_wow64 =
        (IsWow64Process_t)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process");
    if (is_wow64)
        is_wow64(GetCurrentProcess(), &wow);
    g_sys.x64 = wow;
#endif

    GetModuleFileNameW(NULL, g_root, MAX_PATH);
    wchar_t *slash = wcsrchr(g_root, L'\\');
    if (slash)
        *slash = 0;
    /* доступна в путях конфига и в запускаемых .bat */
    SetEnvironmentVariableW(L"WPIROOT", g_root);
}

static char *read_file(const wchar_t *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, got = 0;
    char *buf = NULL;

    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    size = GetFileSize(h, NULL);
    if (size != INVALID_FILE_SIZE && size < (16u << 20) && (buf = malloc(size + 1))) {
        if (ReadFile(h, buf, size, &got, NULL) && got == size) {
            buf[size] = 0;
        } else {
            free(buf);
            buf = NULL;
        }
    }
    CloseHandle(h);
    return buf;
}

static wchar_t *wdup(const char *utf8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof *w);
    if (!w)
        return NULL;
    w[0] = 0;
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    return w;
}

static wchar_t *resolve_path(const wchar_t *exe)
{
    wchar_t expanded[MAX_PATH * 2], full[MAX_PATH * 2], found[MAX_PATH];

    ExpandEnvironmentStringsW(exe, expanded, ARRAYSIZE(expanded));
    for (wchar_t *c = expanded; *c; c++)
        if (*c == L'/')
            *c = L'\\';

    if ((expanded[0] && expanded[1] == L':') || (expanded[0] == L'\\' && expanded[1] == L'\\'))
        wfmt(full, ARRAYSIZE(full), L"%ls", expanded);
    else
        wfmt(full, ARRAYSIZE(full), L"%ls\\%ls", g_root, expanded);

    /* "msiexec.exe", "notepad.exe" — ищем в PATH */
    if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES && !wcschr(expanded, L'\\') &&
        SearchPathW(NULL, expanded, NULL, MAX_PATH, found, NULL))
        wfmt(full, ARRAYSIZE(full), L"%ls", found);

    return _wcsdup(full);
}

static const wchar_t *check_blocked(const JNode *o, const Item *it)
{
    /* 51 — XP, 60 — Vista, 61 — 7, 62 — 8, 63 — 8.1, 100 — 10/11 */
    DWORD ver = g_sys.major * 10 + g_sys.minor;
    int xp = json_bool(o, "support_xp", 1);
    int seven = json_bool(o, "support_7", 1);

    if (!_wcsicmp(it->flag, L"x64") && !g_sys.x64)
        return L"Требуется 64-битная Windows";
    if (!_wcsicmp(it->flag, L"x86") && g_sys.x64)
        return L"Только для 32-битной Windows";
    if (ver < 60 && !xp)
        return L"Не поддерживает Windows XP";
    /* support_xp: true подразумевает и поддержку 7 */
    if (ver >= 60 && ver < 62 && !xp && !seven)
        return L"Не поддерживает Windows Vista/7";
    if (wcspbrk(it->exe, L"\t\n\r\b\f"))
        return L"Ошибка в пути: пишите \\\\ или / вместо \\";
    if (GetFileAttributesW(it->path) == INVALID_FILE_ATTRIBUTES)
        return L"Файл не найден";
    return NULL;
}

BOOL config_load(const wchar_t *file, Config *cfg, wchar_t *err, int errlen)
{
    wchar_t jerr[160];
    char *text = read_file(file);
    const char *src = text;
    const JNode *list;
    JNode *root;
    int n = 0;

    if (!text) {
        wfmt(err, (size_t)errlen, L"Не удалось прочитать файл:\n%ls", file);
        return FALSE;
    }
    if ((unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF)
        src += 3;
    root = json_parse(src, jerr, ARRAYSIZE(jerr));
    free(text);
    if (!root) {
        wfmt(err, (size_t)errlen, L"Ошибка в %ls\n%ls", file, jerr);
        return FALSE;
    }

    /* корень — либо {"software": [...]}, либо сразу массив */
    list = root->type == J_ARR ? root : json_get(root, "software");
    if (!list || list->type != J_ARR) {
        wfmt(err, (size_t)errlen, L"В %ls нет массива \"software\"", file);
        json_free(root);
        return FALSE;
    }

    cfg->title = wdup(json_str(root, "title", "Windows Post-Install"));
    cfg->timer = (int)json_num(root, "timer", 0);
    cfg->reboot = json_bool(root, "reboot", 0);

    for (const JNode *o = list->child; o; o = o->next)
        n++;
    cfg->items = calloc((size_t)(n ? n : 1), sizeof(Item));
    cfg->count = 0;

    for (const JNode *o = list->child; o; o = o->next) {
        Item *it;
        if (o->type != J_OBJ)
            continue;
        it = &cfg->items[cfg->count++];
        it->name = wdup(json_str(o, "name", "(без имени)"));
        it->cat = wdup(json_str(o, "cat", "Разное"));
        it->desc = wdup(json_str(o, "desc", ""));
        it->exe = wdup(json_str(o, "exe", ""));
        it->flag = wdup(json_str(o, "flag", ""));
        it->silent_args = wdup(json_str(o, "silent_args", ""));
        it->normal_args = wdup(json_str(o, "normal_args", ""));
        it->install_dflt = json_bool(o, "install_dflt", 0);
        it->silent_supported = json_bool(o, "silent_supported", 0);
        it->silent_dflt = json_bool(o, "silent_dflt", 1);
        it->lock_silent = json_bool(o, "lock_silent_choice", 0);
        it->path = resolve_path(it->exe);
        it->blocked = check_blocked(o, it);
        it->silent = it->silent_supported && it->silent_dflt;
    }
    json_free(root);
    return TRUE;
}
