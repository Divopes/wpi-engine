#ifndef WPI_H
#define WPI_H

#include <windows.h>

#define WPI_NAME    L"Windows Post-Install 2.0"

/* Состояние программы в очереди установки */
enum { ST_NONE, ST_QUEUED, ST_RUNNING, ST_OK, ST_REBOOT, ST_FAIL, ST_ERROR, ST_SKIPPED };

/* Уведомления от потока установки: wParam — индекс программы, lParam — ST_* */
#define WM_APP_ITEM (WM_APP + 1)
#define WM_APP_DONE (WM_APP + 2)

typedef struct {
    wchar_t *name, *cat, *desc, *exe, *flag;
    wchar_t *silent_args, *normal_args;
    wchar_t *path;              /* полный путь к установщику */
    int install_dflt, silent_supported, silent_dflt, lock_silent;
    const wchar_t *blocked;     /* причина, по которой программу нельзя выбрать, или NULL */

    int silent, queued;         /* выбор пользователя на момент запуска */
    int status;                 /* ST_* */
    DWORD exit_code;
    wchar_t *cmd;               /* последняя выполненная командная строка */
} Item;

typedef struct {
    wchar_t *title;
    int timer;                  /* автозапуск через N секунд, 0 — выключен */
    int reboot;                 /* галочка «перезагрузить» по умолчанию */
    Item *items;
    int count;
} Config;

typedef struct {
    DWORD major, minor, build;
    int x64;
} SysInfo;

extern wchar_t g_root[MAX_PATH];
extern SysInfo g_sys;

void wfmt(wchar_t *buf, size_t n, const wchar_t *fmt, ...);

void sys_detect(void);
BOOL config_load(const wchar_t *file, Config *cfg, wchar_t *err, int errlen);

void install_start(HWND notify, Config *cfg);
void install_stop(void);

#endif
