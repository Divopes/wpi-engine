#include "wpi.h"

#include <commctrl.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>

enum { IDC_INFO = 100, IDC_LIST, IDC_LOG, IDC_STATUS, IDC_PROGRESS,
       IDC_ALL, IDC_NONE, IDC_DEFAULT, IDC_REBOOT, IDC_INSTALL };
enum { COL_NAME, COL_MODE, COL_STATE };
enum { SEL_NONE, SEL_ALL, SEL_DEFAULT };

#define TIMER_COUNTDOWN 1

static Config g_cfg;
static HWND g_wnd, g_list, g_log, g_status, g_progress;
static HFONT g_font, g_bold;
static int g_dpi = 96;
static HANDLE g_logfile = INVALID_HANDLE_VALUE;

static int g_auto;          /* /auto: сразу ставим выбранное по умолчанию и выходим */
static int g_busy, g_stopping, g_countdown;
static int g_total, g_done, g_failed, g_need_reboot;

static int S(int px) { return MulDiv(px, g_dpi, 96); }
static HWND ctl(int id) { return GetDlgItem(g_wnd, id); }

static void log_line(const wchar_t *fmt, ...)
{
    wchar_t msg[2048], line[2200];
    SYSTEMTIME t;
    va_list ap;

    va_start(ap, fmt);
    _vsnwprintf(msg, ARRAYSIZE(msg) - 1, fmt, ap);
    msg[ARRAYSIZE(msg) - 1] = 0;
    va_end(ap);
    GetLocalTime(&t);
    wfmt(line, ARRAYSIZE(line), L"[%02u:%02u:%02u] %ls\r\n", t.wHour, t.wMinute, t.wSecond, msg);

    int len = GetWindowTextLengthW(g_log);
    SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)line);

    if (g_logfile != INVALID_HANDLE_VALUE) {
        char utf8[4096];
        DWORD written;
        int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof utf8, NULL, NULL);
        if (n > 1)
            WriteFile(g_logfile, utf8, (DWORD)n - 1, &written, NULL);
    }
}

static void open_logfile(void)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH - 8, path);
    if (!n || n >= MAX_PATH - 8)
        return;
    wcscat(path, L"wpi.log");
    g_logfile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
}

static const wchar_t *mode_text(const Item *it)
{
    if (!it->silent_supported)
        return L"Обычная";
    if (it->lock_silent)
        return it->silent ? L"Тихая (фикс.)" : L"Обычная (фикс.)";
    return it->silent ? L"Тихая" : L"Обычная";
}

static void state_text(const Item *it, wchar_t *buf, size_t n)
{
    static const wchar_t *const names[] = {
        L"", L"В очереди", L"Установка…", L"Готово", L"Готово, нужна перезагрузка",
        L"Ошибка, код %lu", L"Не запустился, ошибка %lu", L"Пропущено",
    };
    if (it->blocked)
        wfmt(buf, n, L"%ls", it->blocked);
    else
        wfmt(buf, n, names[it->status], it->exit_code);
}

static void refresh_row(int i)
{
    wchar_t state[256];
    state_text(&g_cfg.items[i], state, ARRAYSIZE(state));
    ListView_SetItemText(g_list, i, COL_MODE, (LPWSTR)mode_text(&g_cfg.items[i]));
    ListView_SetItemText(g_list, i, COL_STATE, state);
}

static int checked_count(void)
{
    int n = 0;
    for (int i = 0; i < g_cfg.count; i++)
        n += !g_cfg.items[i].blocked && ListView_GetCheckState(g_list, i);
    return n;
}

static void update_status(void)
{
    wchar_t text[256];
    if (g_busy)
        wfmt(text, ARRAYSIZE(text), L"Установлено %d из %d%ls", g_done, g_total,
             g_stopping ? L" — остановка после текущей программы…" : L"");
    else if (g_countdown > 0)
        wfmt(text, ARRAYSIZE(text), L"Автоматическая установка через %d с. Любое действие отменит отсчёт.",
             g_countdown);
    else
        wfmt(text, ARRAYSIZE(text), L"Выбрано программ: %d из %d", checked_count(), g_cfg.count);
    SetWindowTextW(g_status, text);
}

static void stop_countdown(void)
{
    if (g_countdown > 0) {
        KillTimer(g_wnd, TIMER_COUNTDOWN);
        g_countdown = 0;
        update_status();
    }
}

static void select_items(int how)
{
    for (int i = 0; i < g_cfg.count; i++) {
        const Item *it = &g_cfg.items[i];
        if (!it->blocked)
            ListView_SetCheckState(g_list, i, how == SEL_ALL || (how == SEL_DEFAULT && it->install_dflt));
    }
}

static void toggle_mode(int i)
{
    Item *it = &g_cfg.items[i];
    if (g_busy || it->blocked || !it->silent_supported || it->lock_silent)
        return;
    it->silent = !it->silent;
    refresh_row(i);
}

static void set_busy(int busy)
{
    static const int ids[] = { IDC_ALL, IDC_NONE, IDC_DEFAULT, IDC_REBOOT };
    g_busy = busy;
    g_stopping = 0;
    for (size_t i = 0; i < ARRAYSIZE(ids); i++)
        EnableWindow(ctl(ids[i]), !busy);
    EnableWindow(ctl(IDC_INSTALL), TRUE);
    SetWindowTextW(ctl(IDC_INSTALL), busy ? L"Остановить" : L"Установить");
    update_status();
}

static void start_install(void)
{
    stop_countdown();
    g_total = g_done = g_failed = g_need_reboot = 0;
    for (int i = 0; i < g_cfg.count; i++) {
        Item *it = &g_cfg.items[i];
        it->queued = !it->blocked && ListView_GetCheckState(g_list, i);
        it->status = it->queued ? ST_QUEUED : ST_NONE;
        g_total += it->queued;
        refresh_row(i);
    }
    if (!g_total) {
        if (g_auto)
            PostMessageW(g_wnd, WM_CLOSE, 0, 0);
        else
            MessageBoxW(g_wnd, L"Не выбрано ни одной программы.", WPI_NAME, MB_ICONINFORMATION);
        return;
    }
    SendMessageW(g_progress, PBM_SETRANGE32, 0, g_total);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    log_line(L"Начало установки, программ: %d", g_total);
    set_busy(1);
    install_start(g_wnd, &g_cfg);
}

static void reboot_system(void)
{
    HANDLE token;
    TOKEN_PRIVILEGES tp;

    ZeroMemory(&tp, sizeof tp);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(token);
    }
    if (InitiateSystemShutdownW(NULL, (LPWSTR)L"Windows Post-Install: установка завершена. "
                                L"Компьютер будет перезагружен.", 30, FALSE, TRUE))
        log_line(L"Перезагрузка через 30 секунд (отмена: shutdown -a)");
    else
        log_line(L"Не удалось запустить перезагрузку, ошибка %lu", GetLastError());
}

static void on_item(int i, int status)
{
    Item *it = &g_cfg.items[i];

    it->status = status;
    refresh_row(i);
    switch (status) {
    case ST_RUNNING:
        ListView_EnsureVisible(g_list, i, FALSE);
        log_line(L"Установка: %ls", it->name);
        log_line(L"  > %ls", it->cmd ? it->cmd : it->path);
        return;
    case ST_OK:
        log_line(L"  Готово");
        break;
    case ST_REBOOT:
        g_need_reboot = 1;
        log_line(L"  Готово, требуется перезагрузка (код %lu)", it->exit_code);
        break;
    case ST_FAIL:
        g_failed++;
        log_line(L"  Ошибка: установщик вернул код %lu", it->exit_code);
        break;
    case ST_ERROR:
        g_failed++;
        log_line(L"  Не удалось запустить, ошибка Windows %lu", it->exit_code);
        break;
    case ST_SKIPPED:
        log_line(L"Пропущено: %ls", it->name);
        break;
    }
    g_done++;
    SendMessageW(g_progress, PBM_SETPOS, (WPARAM)g_done, 0);
    update_status();
}

static void on_done(void)
{
    int reboot = IsDlgButtonChecked(g_wnd, IDC_REBOOT) == BST_CHECKED;

    set_busy(0);
    log_line(L"Установка завершена. Ошибок: %d", g_failed);
    SetWindowTextW(g_status, g_failed ? L"Установка завершена с ошибками — см. журнал."
                                      : L"Установка завершена.");
    if (!reboot && g_need_reboot && !g_auto)
        reboot = MessageBoxW(g_wnd, L"Некоторым программам нужна перезагрузка. Перезагрузить компьютер сейчас?",
                             WPI_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES;
    if (reboot)
        reboot_system();
    if (g_auto)
        PostMessageW(g_wnd, WM_CLOSE, 0, 0);
}

static void fill_list(void)
{
    ListView_EnableGroupView(g_list, TRUE);
    for (int i = 0; i < g_cfg.count; i++) {
        Item *it = &g_cfg.items[i];
        int group = i + 1;

        /* группа = первая программа с той же категорией */
        for (int j = 0; j < i; j++) {
            if (!wcscmp(g_cfg.items[j].cat, it->cat)) {
                group = j + 1;
                break;
            }
        }
        if (group == i + 1) {
            LVGROUP g = { 0 };
            g.cbSize = sizeof g;
            g.mask = LVGF_HEADER | LVGF_GROUPID;
            g.pszHeader = it->cat;
            g.iGroupId = group;
            ListView_InsertGroup(g_list, -1, &g);
        }

        LVITEMW lv = { 0 };
        lv.mask = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
        lv.iItem = i;
        lv.pszText = it->name;
        lv.lParam = i;
        lv.iGroupId = group;
        ListView_InsertItem(g_list, &lv);
        refresh_row(i);
    }
    select_items(SEL_DEFAULT);
}

static void layout(int w, int h)
{
    int m = S(10), gap = S(6), bh = S(28), bw = S(112), lh = S(18), logh = S(96), pbh = S(14);
    int y = m, bottom = h - m - bh;
    int pby = bottom - S(10) - pbh;
    int sty = pby - S(4) - lh;
    int logy = sty - gap - logh;
    int lw = w - 2 * m;

    MoveWindow(ctl(IDC_INFO), m, y, lw, lh, TRUE);
    y += lh + gap;
    MoveWindow(g_list, m, y, lw, logy - gap - y, TRUE);
    MoveWindow(g_log, m, logy, lw, logh, TRUE);
    MoveWindow(g_status, m, sty, lw, lh, TRUE);
    MoveWindow(g_progress, m, pby, lw, pbh, TRUE);

    MoveWindow(ctl(IDC_ALL), m, bottom, bw, bh, TRUE);
    MoveWindow(ctl(IDC_NONE), m + bw + gap, bottom, bw, bh, TRUE);
    MoveWindow(ctl(IDC_DEFAULT), m + 2 * (bw + gap), bottom, bw, bh, TRUE);
    MoveWindow(ctl(IDC_INSTALL), w - m - S(130), bottom, S(130), bh, TRUE);
    MoveWindow(ctl(IDC_REBOOT), w - m - S(130) - gap - S(190), bottom, S(190), bh, TRUE);

    int mode_w = S(120), state_w = S(220);
    int name_w = lw - mode_w - state_w - GetSystemMetrics(SM_CXVSCROLL) - S(6);
    ListView_SetColumnWidth(g_list, COL_NAME, name_w > S(120) ? name_w : S(120));
    ListView_SetColumnWidth(g_list, COL_MODE, mode_w);
    ListView_SetColumnWidth(g_list, COL_STATE, state_w);
}

static HWND add_control(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                             g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, FALSE);
    return h;
}

static void create_controls(void)
{
    typedef HRESULT (WINAPI *SetWindowTheme_t)(HWND, LPCWSTR, LPCWSTR);
    static const wchar_t *const cols[] = { L"Программа", L"Режим", L"Состояние" };
    wchar_t info[256];
    HMODULE uxtheme;

    wfmt(info, ARRAYSIZE(info), L"Windows %lu.%lu (сборка %lu), %ls  ·  программ в наборе: %d",
         g_sys.major, g_sys.minor, g_sys.build, g_sys.x64 ? L"x64" : L"x86", g_cfg.count);
    SendMessageW(add_control(L"STATIC", info, SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, IDC_INFO),
                 WM_SETFONT, (WPARAM)g_bold, FALSE);

    g_list = add_control(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL |
                         LVS_NOSORTHEADER, WS_EX_CLIENTEDGE, IDC_LIST);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP |
                                      LVS_EX_DOUBLEBUFFER);
    /* современный вид списка на Vista+; на XP функции нет — ничего страшного */
    if ((uxtheme = LoadLibraryW(L"uxtheme.dll"))) {
        SetWindowTheme_t set_theme = (SetWindowTheme_t)(void *)GetProcAddress(uxtheme, "SetWindowTheme");
        if (set_theme && g_sys.major >= 6)
            set_theme(g_list, L"Explorer", NULL);
    }
    for (int i = 0; i < (int)ARRAYSIZE(cols); i++) {
        LVCOLUMNW c = { 0 };
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx = S(100);
        ListView_InsertColumn(g_list, i, &c);
    }

    g_log = add_control(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                        WS_EX_CLIENTEDGE, IDC_LOG);
    SendMessageW(g_log, EM_SETLIMITTEXT, 0, 0);
    g_status = add_control(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, IDC_STATUS);
    g_progress = add_control(PROGRESS_CLASSW, L"", 0, 0, IDC_PROGRESS);

    add_control(L"BUTTON", L"Выбрать все", WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_ALL);
    add_control(L"BUTTON", L"Снять все", WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_NONE);
    add_control(L"BUTTON", L"По умолчанию", WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_DEFAULT);
    add_control(L"BUTTON", L"Перезагрузить после", WS_TABSTOP | BS_AUTOCHECKBOX, 0, IDC_REBOOT);
    add_control(L"BUTTON", L"Установить", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDC_INSTALL);
    CheckDlgButton(g_wnd, IDC_REBOOT, g_cfg.reboot ? BST_CHECKED : BST_UNCHECKED);
}

static LRESULT on_custom_draw(NMLVCUSTOMDRAW *cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int i = (int)cd->nmcd.lItemlParam;
        const Item *it;
        COLORREF color = GetSysColor(COLOR_WINDOWTEXT);

        if (i < 0 || i >= g_cfg.count)
            return CDRF_DODEFAULT;
        it = &g_cfg.items[i];
        if (it->blocked)
            color = GetSysColor(COLOR_GRAYTEXT);
        else if (cd->iSubItem == COL_STATE && it->status == ST_RUNNING)
            color = RGB(0, 90, 200);
        else if (cd->iSubItem == COL_STATE && it->status == ST_OK)
            color = RGB(0, 130, 0);
        else if (cd->iSubItem == COL_STATE && it->status == ST_REBOOT)
            color = RGB(180, 110, 0);
        else if (cd->iSubItem == COL_STATE && (it->status == ST_FAIL || it->status == ST_ERROR))
            color = RGB(200, 0, 0);
        cd->clrText = color;
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT on_list_notify(NMHDR *hdr)
{
    switch (hdr->code) {
    case LVN_ITEMCHANGING: {
        NMLISTVIEW *nm = (NMLISTVIEW *)hdr;
        if (nm->iItem < 0 || !(nm->uChanged & LVIF_STATE) ||
            !((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK))
            return FALSE;
        /* во время установки и для недоступных программ галочку не меняем */
        if (g_busy)
            return TRUE;
        return g_cfg.items[nm->iItem].blocked && (nm->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
    }
    case LVN_ITEMCHANGED: {
        NMLISTVIEW *nm = (NMLISTVIEW *)hdr;
        if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK))
            update_status();
        return 0;
    }
    case LVN_KEYDOWN:
        stop_countdown();
        return 0;
    case NM_CLICK: {
        NMITEMACTIVATE *nm = (NMITEMACTIVATE *)hdr;
        LVHITTESTINFO hit = { 0 };
        stop_countdown();
        hit.pt = nm->ptAction;
        if (ListView_SubItemHitTest(g_list, &hit) >= 0 && hit.iSubItem == COL_MODE)
            toggle_mode(hit.iItem);
        return 0;
    }
    case LVN_GETINFOTIPW: {
        NMLVGETINFOTIPW *tip = (NMLVGETINFOTIPW *)hdr;
        const Item *it = &g_cfg.items[tip->iItem];
        wfmt(tip->pszText, (size_t)tip->cchTextMax, L"%ls%ls%ls\nФайл: %ls\nКлик по «Режиму» — тихая/обычная",
             it->name, *it->desc ? L"\n" : L"", it->desc, it->path);
        return 0;
    }
    case NM_CUSTOMDRAW:
        return on_custom_draw((NMLVCUSTOMDRAW *)hdr);
    }
    return 0;
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_wnd = wnd;
        create_controls();
        fill_list();
        update_status();
        return 0;

    case WM_SIZE:
        layout(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = S(680);
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = S(460);
        return 0;

    case WM_NOTIFY:
        if (((NMHDR *)lp)->idFrom == IDC_LIST)
            return on_list_notify((NMHDR *)lp);
        break;

    case WM_COMMAND:
        if (HIWORD(wp) != BN_CLICKED)
            break;
        stop_countdown();
        switch (LOWORD(wp)) {
        case IDC_ALL:     select_items(SEL_ALL); break;
        case IDC_NONE:    select_items(SEL_NONE); break;
        case IDC_DEFAULT: select_items(SEL_DEFAULT); break;
        case IDC_INSTALL:
            if (!g_busy) {
                start_install();
            } else if (!g_stopping) {
                install_stop();
                g_stopping = 1;
                EnableWindow(ctl(IDC_INSTALL), FALSE);
                log_line(L"Остановка: текущая программа будет доустановлена, остальные пропущены");
                update_status();
            }
            break;
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_COUNTDOWN && --g_countdown <= 0) {
            KillTimer(wnd, TIMER_COUNTDOWN);
            start_install();
        } else {
            update_status();
        }
        return 0;

    case WM_APP_ITEM:
        on_item((int)wp, (int)lp);
        return 0;

    case WM_APP_DONE:
        on_done();
        return 0;

    case WM_CLOSE:
        if (g_busy && !g_auto &&
            MessageBoxW(wnd, L"Установка ещё идёт. Закрыть программу?\n\n"
                        L"Текущий установщик продолжит работу, остальные будут отменены.",
                        WPI_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return 0;
        DestroyWindow(wnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static void create_fonts(void)
{
    NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
    HDC dc = GetDC(NULL);

    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) {
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_bold = CreateFontIndirectW(&ncm.lfMessageFont);
    } else {
        g_font = g_bold = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    INITCOMMONCONTROLSEX icc = { .dwSize = sizeof icc,
                                 .dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    wchar_t config_path[MAX_PATH * 2], err[1024];
    WNDCLASSEXW wc = { .cbSize = sizeof wc };
    RECT work;
    MSG msg;
    int argc = 0;
    wchar_t **argv;

    (void)prev;
    (void)cmdline;
    sys_detect();
    InitCommonControlsEx(&icc);
    create_fonts();

    wfmt(config_path, ARRAYSIZE(config_path), L"%ls\\config.json", g_root);
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (!_wcsicmp(argv[i], L"/auto") || !_wcsicmp(argv[i], L"-auto"))
            g_auto = 1;
        else if (!_wcsnicmp(argv[i], L"/config=", 8))
            wfmt(config_path, ARRAYSIZE(config_path), L"%ls", argv[i] + 8);
        else if (argv[i][0] != L'/' && argv[i][0] != L'-')
            wfmt(config_path, ARRAYSIZE(config_path), L"%ls", argv[i]);
    }
    LocalFree(argv);

    if (!config_load(config_path, &g_cfg, err, ARRAYSIZE(err))) {
        MessageBoxW(NULL, err, WPI_NAME, MB_ICONERROR);
        return 1;
    }
    open_logfile();

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"WPI2";
    RegisterClassExW(&wc);

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = S(820), h = S(620);
    if (w > work.right - work.left) w = work.right - work.left;
    if (h > work.bottom - work.top) h = work.bottom - work.top;
    CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, g_cfg.title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                    w, h, NULL, NULL, inst, NULL);
    if (!g_wnd)
        return 1;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);

    log_line(L"%ls, конфигурация: %ls", WPI_NAME, config_path);
    if (g_auto) {
        start_install();
    } else if (g_cfg.timer > 0) {
        g_countdown = g_cfg.timer;
        SetTimer(g_wnd, TIMER_COUNTDOWN, 1000, NULL);
        update_status();
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_logfile != INVALID_HANDLE_VALUE)
        CloseHandle(g_logfile);
    return (int)msg.wParam;
}
