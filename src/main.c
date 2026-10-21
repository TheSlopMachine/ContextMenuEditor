/*
 * Context Menu Editor - main window.
 *
 * Layout: location list on the left; on the right a mode switch (menu commands /
 * shell extensions), action buttons, the entry list (with icons), and a details strip.
 * Registry access lives in registry.c, the add/edit dialog in dialog.c.
 */
#include "common.h"
#include "dialog.h"
#include "icons.h"
#include "regfile.h"
#include "registry.h"
#include "resource.h"

/* --------------------------------------------------------------- ids */
enum {
    IDC_LOCLBL = 100, IDC_LOC, IDC_EXTLBL, IDC_EXT, IDC_MODE_CMD, IDC_MODE_EXT, IDC_FILTER,
    IDC_ADD, IDC_EDIT, IDC_DEL, IDC_REFRESH, IDC_LIST, IDC_DET1, IDC_DET2, IDC_STATUS
};
enum {
    IDM_IMPORT = 300, IDM_EXPORT, IDM_BACKUPS, IDM_EXIT,
    IDM_ADD, IDM_EDIT, IDM_TOGGLE, IDM_DELETE, IDM_COPYCMD, IDM_REGEDIT,
    IDM_VIEW_CMD, IDM_VIEW_EXT, IDM_REFRESH, IDM_FIND,
    IDM_CLASSIC, IDM_RESTART, IDM_ADMIN, IDM_ABOUT
};
#define TIMER_RELOAD 1
#define SETTINGS_KEY L"Software\\ContextMenuEditor"

/* ------------------------------------------------------------- state */
static HWND g_main, g_loclbl, g_loc, g_extlbl, g_ext, g_mode_cmd, g_mode_ext, g_filter;
static HWND g_list, g_det1, g_det2, g_status;
static HMENU g_menu;
static HIMAGELIST g_il;
static ViewMode g_mode = MODE_VERBS;
static ItemList g_items;
static int g_sortcol, g_builtmode = -1;
static BOOL g_sortdesc, g_admin, g_loading;

typedef struct {
    DWORD version; RECT rc; DWORD maximized;
    int loc, mode, sortcol, sortdesc;
} Settings;
static Settings g_cfg;

/* ---------------------------------------------------------- settings */
static BOOL settings_load(void)
{
    Settings t; DWORD cb = sizeof t;
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"Settings", RRF_RT_REG_BINARY, NULL, &t, &cb) == ERROR_SUCCESS
        && cb == sizeof t && t.version == 1
        && MonitorFromRect(&t.rc, MONITOR_DEFAULTTONULL) != NULL) {
        g_cfg = t;
        return TRUE;
    }
    return FALSE;
}

static void settings_save(void)
{
    WINDOWPLACEMENT wp = { 0 }; HKEY k;
    wp.length = sizeof wp;
    GetWindowPlacement(g_main, &wp);
    Settings s = { 0 };
    s.version = 1; s.rc = wp.rcNormalPosition; s.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    s.loc = (int)SendMessageW(g_loc, LB_GETCURSEL, 0, 0);
    s.mode = g_mode; s.sortcol = g_sortcol; s.sortdesc = g_sortdesc;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"Settings", 0, REG_BINARY, (const BYTE *)&s, sizeof s);
        RegCloseKey(k);
    }
}

/* --------------------------------------------------------- selection */
static void set_status(const wchar_t *s) { SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)s); }

/* Registry sub-path for the selected location; FALSE if a file extension is still missing. */
static BOOL cur_sub(wchar_t *out, size_t n)
{
    int i = (int)SendMessageW(g_loc, LB_GETCURSEL, 0, 0);
    if (i < 0) i = 0;
    if (LOCS[i].sub) { wf(out, n, L"%ls", LOCS[i].sub); return TRUE; }
    wchar_t e[64];
    GetWindowTextW(g_ext, e, 64);
    StrTrimW(e, L" \t*");
    if (!e[0]) return FALSE;
    wf(out, n, L"SystemFileAssociations\\%ls%ls", e[0] == L'.' ? L"" : L".", e);
    return TRUE;
}

static Item *item_at(int row)
{
    LVITEMW lv = { 0 };
    lv.mask = LVIF_PARAM; lv.iItem = row;
    if (!ListView_GetItem(g_list, &lv)) return NULL;
    return &g_items.v[lv.lParam];
}

static int sel_row(void) { return ListView_GetNextItem(g_list, -1, LVNI_SELECTED); }
static Item *sel_item(void) { int r = sel_row(); return r < 0 ? NULL : item_at(r); }

/* ------------------------------------------------------------ list UI */
static void setup_columns(void)
{
    static const wchar_t *c0[NCOLS] = { L"Menu text", L"Key", L"Scope", L"Command", L"Options" };
    static const wchar_t *c1[NCOLS] = { L"Name", L"Key", L"Scope", L"Module", L"Status" };
    static const int w[NCOLS] = { 260, 150, 70, 420, 200 };
    if (g_builtmode == (int)g_mode) return;                 /* keep user-resized widths */
    g_builtmode = (int)g_mode;
    while (ListView_DeleteColumn(g_list, 0)) {}
    for (int i = 0; i < NCOLS; i++) {
        LVCOLUMNW col = { 0 };
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)(g_mode == MODE_VERBS ? c0[i] : c1[i]);
        col.cx = S(w[i]);
        ListView_InsertColumn(g_list, i, &col);
    }
}

static void update_sort_arrow(void)
{
    HWND hdr = ListView_GetHeader(g_list);
    for (int i = 0; i < NCOLS; i++) {
        HDITEMW hd = { 0 };
        hd.mask = HDI_FORMAT;
        if (!Header_GetItem(hdr, i, &hd)) continue;
        hd.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g_sortcol) hd.fmt |= g_sortdesc ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(hdr, i, &hd);
    }
}

static void build_icons(void)
{
    ImageList_RemoveAll(g_il);
    for (int i = 0; i < g_items.n; i++) {
        Item *it = &g_items.v[i];
        HICON ic = g_mode == MODE_VERBS ? icon_load(it->icon, it->cmd, NULL)
                                        : icon_load(NULL, NULL, it->module);
        if (!ic) ic = icon_generic();
        it->img = ic ? ImageList_ReplaceIcon(g_il, -1, ic) : -1;
        if (ic) DestroyIcon(ic);
    }
}

static void fill_list(void)
{
    wchar_t flt[128], buf[200];
    GetWindowTextW(g_filter, flt, 128);
    StrTrimW(flt, L" \t");
    g_loading = TRUE;
    ListView_DeleteAllItems(g_list);
    int shown = 0;
    for (int i = 0; i < g_items.n; i++) {
        Item *it = &g_items.v[i];
        if (flt[0] && !StrStrIW(it->disp, flt) && !StrStrIW(it->name, flt) && !StrStrIW(it->cmd, flt)
            && !StrStrIW(it->clsid, flt) && !StrStrIW(it->module, flt))
            continue;
        LVITEMW lv = { 0 };
        lv.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lv.iItem = shown; lv.pszText = it->disp; lv.lParam = i;
        lv.iImage = it->img >= 0 ? it->img : I_IMAGENONE;
        int idx = ListView_InsertItem(g_list, &lv);
        for (int c = 1; c < NCOLS; c++) {
            const wchar_t *t = item_cell(it, g_mode, c, buf, 200);
            ListView_SetItemText(g_list, idx, c, (LPWSTR)t);
        }
        ListView_SetCheckState(g_list, idx, !it->off);
        shown++;
    }
    g_loading = FALSE;

    wchar_t sub[300], s[300];
    if (!cur_sub(sub, 300))
        set_status(L"Type a file extension (for example .txt) to see its entries.");
    else if (g_items.n == 0)
        set_status(g_mode == MODE_VERBS ? L"No custom commands here yet. Click Add to create one."
                                        : L"No third-party shell extensions are registered here.");
    else {
        wf(s, 300, L"%d of %d entries shown.  Untick an entry to hide it from the menu.", shown, g_items.n);
        set_status(s);
    }
}

static void update_details(void)
{
    Item *it = sel_item();
    wchar_t sub[300], path[800], l1[900], l2[1200];
    if (!it || !cur_sub(sub, 300)) { SetWindowTextW(g_det1, L""); SetWindowTextW(g_det2, L""); return; }
    item_key_path(path, 800, g_mode, sub, it);
    wf(l1, 900, L"Registry key:  %ls\\%ls", hive_short(it->hive), path);
    if (g_mode == MODE_VERBS) wf(l2, 1200, L"Command:  %ls", it->cmd[0] ? it->cmd : L"(none - submenu or shell handler)");
    else wf(l2, 1200, L"CLSID:  %ls     Module:  %ls", it->clsid[0] ? it->clsid : L"(none)",
            it->module[0] ? it->module : L"(unknown)");
    SetWindowTextW(g_det1, l1);
    SetWindowTextW(g_det2, l2);
}

static void update_ui_state(void)
{
    wchar_t sub[300];
    BOOL loc = cur_sub(sub, 300), verbs = g_mode == MODE_VERBS;
    Item *it = sel_item();
    BOOL sel = it != NULL;
    EnableWindow(GetDlgItem(g_main, IDC_ADD),  verbs && loc);
    EnableWindow(GetDlgItem(g_main, IDC_EDIT), verbs && sel);
    EnableWindow(GetDlgItem(g_main, IDC_DEL),  sel);
    #define EN(id, on) EnableMenuItem(g_menu, (id), MF_BYCOMMAND | ((on) ? MF_ENABLED : MF_GRAYED))
    EN(IDM_ADD, verbs && loc);   EN(IDM_EDIT, verbs && sel);  EN(IDM_TOGGLE, sel);
    EN(IDM_DELETE, sel);         EN(IDM_EXPORT, sel);         EN(IDM_COPYCMD, sel);
    EN(IDM_REGEDIT, sel);        EN(IDM_ADMIN, !g_admin);
    #undef EN
    CheckMenuRadioItem(g_menu, IDM_VIEW_CMD, IDM_VIEW_EXT, verbs ? IDM_VIEW_CMD : IDM_VIEW_EXT, MF_BYCOMMAND);
    CheckRadioButton(g_main, IDC_MODE_CMD, IDC_MODE_EXT, verbs ? IDC_MODE_CMD : IDC_MODE_EXT);
    update_details();
}

static void reload(void)
{
    wchar_t sub[300], keepName[256] = L"";
    HKEY keepHive = NULL;
    HCURSOR old = SetCursor(LoadCursorW(NULL, IDC_WAIT));

    Item *cur = sel_item();                                  /* restore the selection afterwards */
    if (cur) { wf(keepName, 256, L"%ls", cur->name); keepHive = cur->hive; }

    items_clear(&g_items);
    setup_columns();
    if (cur_sub(sub, 300)) {
        items_load(&g_items, g_mode, sub);
        items_sort(&g_items, g_mode, g_sortcol, g_sortdesc);
    }
    build_icons();
    fill_list();
    update_sort_arrow();

    if (keepName[0]) {
        for (int r = 0, n = ListView_GetItemCount(g_list); r < n; r++) {
            Item *it = item_at(r);
            if (it && it->hive == keepHive && !wcscmp(it->name, keepName)) {
                ListView_SetItemState(g_list, r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_list, r, FALSE);
                break;
            }
        }
    }
    update_ui_state();
    SetCursor(old);
}

static void set_mode(ViewMode m)
{
    if (g_mode == m) { update_ui_state(); return; }
    g_mode = m;
    reload();
}

/* ----------------------------------------------------------- actions */
static void do_add(void)
{
    wchar_t sub[300];
    if (g_mode == MODE_VERBS && cur_sub(sub, 300) && verb_dialog(g_main, NULL, sub)) reload();
}

static void do_edit(void)
{
    wchar_t sub[300]; Item *it = sel_item();
    if (!it || g_mode != MODE_VERBS || !cur_sub(sub, 300)) return;
    Item copy = *it;
    if (verb_dialog(g_main, &copy, sub)) reload();
}

static void do_toggle(void)                  /* flips the checkbox; the notification does the work */
{
    int row = sel_row();
    if (row < 0) return;
    BOOL on = ListView_GetCheckState(g_list, row) ? TRUE : FALSE;
    ListView_SetCheckState(g_list, row, !on);
}

static void apply_toggle(int row, BOOL enable)
{
    Item *it = item_at(row);
    wchar_t sub[300];
    if (!it || !cur_sub(sub, 300)) return;
    LONG r = item_set_enabled(g_mode, sub, it, enable);
    if (r == ERROR_SUCCESS) {
        it->off = !enable;
        if (g_mode == MODE_HANDLERS) { ListView_SetItemText(g_list, row, 4, it->off ? L"Blocked" : L"Active"); }
        InvalidateRect(g_list, NULL, FALSE);
        set_status(g_mode == MODE_VERBS ? (enable ? L"Entry enabled." : L"Entry hidden from the menu.")
                                        : L"Saved. Restart Explorer for the change to take effect.");
    } else {
        if (r == ERROR_CANCELLED) MessageBoxW(g_main, L"This extension has no CLSID, so it can't be blocked.", APP_TITLE, MB_ICONINFORMATION);
        else err_box(g_main, L"Could not change the entry.", r);
        g_loading = TRUE;
        ListView_SetCheckState(g_list, row, !enable);
        g_loading = FALSE;
    }
}

static void backup_dir(wchar_t *out, size_t n)
{
    wchar_t base[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH)) GetTempPathW(MAX_PATH, base);
    wf(out, n, L"%ls\\ContextMenuEditor", base);         CreateDirectoryW(out, NULL);
    wf(out, n, L"%ls\\ContextMenuEditor\\Backups", base); CreateDirectoryW(out, NULL);
}

static void do_delete(void)
{
    wchar_t sub[300], path[800], msg[900], bak[MAX_PATH + 100], dir[MAX_PATH], safe[64];
    Item *it = sel_item();
    if (!it || !cur_sub(sub, 300)) return;
    item_key_path(path, 800, g_mode, sub, it);

    wf(msg, 900, L"Delete \"%ls\" (%ls, %ls)?\n\nA .reg backup is saved first so you can restore it.",
       it->disp, it->name, it->hive == HKEY_CURRENT_USER ? L"User" : L"System");
    if (MessageBoxW(g_main, msg, APP_TITLE, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    wf(safe, 64, L"%ls", it->name);
    for (wchar_t *p = safe; *p; p++) if (!iswalnum(*p) && *p != L'-' && *p != L'_') *p = L'_';
    backup_dir(dir, MAX_PATH);
    wf(bak, MAX_PATH + 100, L"%ls\\%ls_%04d%02d%02d_%02d%02d%02d.reg", dir, safe,
       t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    if (!regfile_write(bak, it->hive, path)) {
        if (MessageBoxW(g_main, L"Could not write the backup file. Delete anyway?", APP_TITLE,
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        bak[0] = 0;
    }
    LONG r = key_delete(it->hive, path);
    if (r != ERROR_SUCCESS) { err_box(g_main, L"Could not delete the entry.", r); return; }
    reload();
    if (bak[0]) { wf(msg, 900, L"Deleted. Backup saved to %ls", bak); set_status(msg); }
}

static void do_export(void)
{
    wchar_t sub[300], path[800], f[MAX_PATH];
    Item *it = sel_item();
    if (!it || !cur_sub(sub, 300)) return;
    item_key_path(path, 800, g_mode, sub, it);
    wf(f, MAX_PATH, L"%ls.reg", it->name);
    if (!browse_file(g_main, f, L"Registry files (*.reg)\0*.reg\0All files\0*.*\0", TRUE, L"reg")) return;
    if (regfile_write(f, it->hive, path)) set_status(L"Exported.");
    else MessageBoxW(g_main, L"Could not write the file.", APP_TITLE, MB_ICONERROR);
}

static void do_import(void)
{
    wchar_t f[MAX_PATH] = L"", args[MAX_PATH + 16];
    if (!browse_file(g_main, f, L"Registry files (*.reg)\0*.reg\0All files\0*.*\0", FALSE, NULL)) return;
    wf(args, MAX_PATH + 16, L"/s \"%ls\"", f);
    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.hwnd = g_main; sei.lpVerb = L"open";
    sei.lpFile = L"regedit.exe"; sei.lpParameters = args; sei.nShow = SW_HIDE;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 20000);
        CloseHandle(sei.hProcess);
        reload();
        set_status(L"Import finished.");
    }
}

static void do_copy(void)
{
    Item *it = sel_item();
    if (!it) return;
    const wchar_t *t = g_mode == MODE_VERBS ? it->cmd : it->clsid;
    if (t[0] && clipboard_set(g_main, t)) set_status(g_mode == MODE_VERBS ? L"Command copied." : L"CLSID copied.");
}

static void do_regedit(void)
{
    wchar_t sub[300], path[800];
    Item *it = sel_item();
    if (!it || !cur_sub(sub, 300)) return;
    item_key_path(path, 800, g_mode, sub, it);
    regedit_jump(it->hive, path);
}

static void open_backups(void)
{
    wchar_t dir[MAX_PATH];
    backup_dir(dir, MAX_PATH);
    ShellExecuteW(g_main, L"open", dir, NULL, NULL, SW_SHOWNORMAL);
}

static void restart_explorer(void)
{
    if (MessageBoxW(g_main, L"Explorer will restart now. Open folder windows will close.\n\nContinue?",
                    APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    wchar_t cmd[] = L"cmd.exe /c taskkill /f /im explorer.exe & start \"\" explorer.exe";
    STARTUPINFOW si = { 0 }; PROCESS_INFORMATION pi;
    si.cb = sizeof si;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static void toggle_classic(void)
{
    BOOL want = !classic_menu_enabled();
    LONG r = classic_menu_set(want);
    if (r != ERROR_SUCCESS) { err_box(g_main, L"Could not change the setting.", r); return; }
    CheckMenuItem(g_menu, IDM_CLASSIC, MF_BYCOMMAND | (want ? MF_CHECKED : MF_UNCHECKED));
    set_status(L"Saved. Restart Explorer for the change to take effect.");
    restart_explorer();
}

static void relaunch_admin(void)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    if ((INT_PTR)ShellExecuteW(g_main, L"runas", exe, NULL, NULL, SW_SHOWNORMAL) > 32) DestroyWindow(g_main);
}

static void show_about(void)
{
    MessageBoxW(g_main,
        APP_TITLE L" " APP_VERSION L"\n\n"
        L"Edit the items in Explorer's right-click menu: add commands, hide or remove entries, "
        L"and block third-party shell extensions.\n\n"
        L"Changes apply to the registry immediately. Deleted entries are backed up as .reg files in "
        L"%APPDATA%\\ContextMenuEditor\\Backups.",
        L"About", MB_ICONINFORMATION);
}

static void show_popup(POINT pt)
{
    Item *it = sel_item();
    if (!it) return;
    BOOL verbs = g_mode == MODE_VERBS;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (verbs ? 0 : MF_GRAYED), IDM_EDIT, L"Edit\x2026\tF2");
    AppendMenuW(m, MF_STRING, IDM_TOGGLE, it->off ? (verbs ? L"Enable" : L"Unblock")
                                                  : (verbs ? L"Disable (hide from menu)" : L"Block"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_DELETE, L"Delete\tDel");
    AppendMenuW(m, MF_STRING, IDM_EXPORT, L"Export as .reg\x2026\tCtrl+E");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_COPYCMD, verbs ? L"Copy command" : L"Copy CLSID");
    AppendMenuW(m, MF_STRING, IDM_REGEDIT, L"Show in Registry Editor");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, NULL);
    DestroyMenu(m);
}

/* ------------------------------------------------------------ layout */
static void layout(void)
{
    RECT rc; GetClientRect(g_main, &rc);
    int W = rc.right, H = rc.bottom, m = S(10), g = S(6), bh = S(26), rh = S(24);

    int parts[2] = { W - S(210), -1 };
    SendMessageW(g_status, WM_SIZE, 0, 0);
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
    RECT sr; GetWindowRect(g_status, &sr);
    int sbh = sr.bottom - sr.top;

    int top = m, bottom = H - sbh - m, lw = S(220);
    int ey = bottom - rh;
    MoveWindow(g_loclbl, m, top, lw, S(18), TRUE);
    MoveWindow(g_loc, m, top + S(22), lw, ey - S(28) - (top + S(22)), TRUE);
    MoveWindow(g_extlbl, m, ey - S(20), lw, S(18), TRUE);
    MoveWindow(g_ext, m, ey, lw, rh, TRUE);

    int rx = m + lw + m, rw = W - rx - m, y = top;
    MoveWindow(g_mode_cmd, rx, y, S(130), bh, TRUE);
    MoveWindow(g_mode_ext, rx + S(133), y, S(130), bh, TRUE);
    int fw = S(230);
    MoveWindow(g_filter, rx + rw - fw, y + S(1), fw, rh, TRUE);
    y += bh + g;

    static const int ids[] = { IDC_ADD, IDC_EDIT, IDC_DEL, IDC_REFRESH };
    for (int i = 0; i < 4; i++) MoveWindow(GetDlgItem(g_main, ids[i]), rx + i * S(90), y, S(86), bh, TRUE);
    y += bh + g;

    int d2 = bottom - S(18), d1 = d2 - S(20);
    MoveWindow(g_det1, rx, d1, rw, S(18), TRUE);
    MoveWindow(g_det2, rx, d2, rw, S(18), TRUE);
    MoveWindow(g_list, rx, y, rw, d1 - g - y, TRUE);
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), f = CreatePopupMenu(), e = CreatePopupMenu(), v = CreatePopupMenu(),
          t = CreatePopupMenu(), h = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, IDM_IMPORT,  L"&Import .reg file\x2026");
    AppendMenuW(f, MF_STRING, IDM_EXPORT,  L"&Export selected as .reg\x2026\tCtrl+E");
    AppendMenuW(f, MF_STRING, IDM_BACKUPS, L"Open &backups folder");
    AppendMenuW(f, MF_SEPARATOR, 0, NULL);
    AppendMenuW(f, MF_STRING, IDM_EXIT,    L"E&xit");

    AppendMenuW(e, MF_STRING, IDM_ADD,     L"&Add command\x2026\tCtrl+N");
    AppendMenuW(e, MF_STRING, IDM_EDIT,    L"&Edit\x2026\tF2");
    AppendMenuW(e, MF_STRING, IDM_TOGGLE,  L"&Enable / disable");
    AppendMenuW(e, MF_STRING, IDM_DELETE,  L"&Delete\tDel");
    AppendMenuW(e, MF_SEPARATOR, 0, NULL);
    AppendMenuW(e, MF_STRING, IDM_COPYCMD, L"&Copy command / CLSID");
    AppendMenuW(e, MF_STRING, IDM_REGEDIT, L"Show in &Registry Editor");

    AppendMenuW(v, MF_STRING, IDM_VIEW_CMD, L"&Menu commands\tCtrl+1");
    AppendMenuW(v, MF_STRING, IDM_VIEW_EXT, L"Shell &extensions\tCtrl+2");
    AppendMenuW(v, MF_SEPARATOR, 0, NULL);
    AppendMenuW(v, MF_STRING, IDM_FIND,     L"&Filter\tCtrl+F");
    AppendMenuW(v, MF_STRING, IDM_REFRESH,  L"&Refresh\tF5");

    AppendMenuW(t, MF_STRING, IDM_CLASSIC,  L"Use the classic &Windows 10 menu on Windows 11");
    AppendMenuW(t, MF_STRING, IDM_RESTART,  L"Restart &Explorer");
    AppendMenuW(t, MF_SEPARATOR, 0, NULL);
    AppendMenuW(t, MF_STRING, IDM_ADMIN,    L"Run as &administrator");

    AppendMenuW(h, MF_STRING, IDM_ABOUT,    L"&About");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)f, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)e, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)v, L"&View");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)t, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)h, L"&Help");
    CheckMenuItem(bar, IDM_CLASSIC, MF_BYCOMMAND | (classic_menu_enabled() ? MF_CHECKED : MF_UNCHECKED));
    return bar;
}

static void on_create(HWND h, BOOL haveCfg)
{
    g_main = h;
    NONCLIENTMETRICSW ncm = { 0 };
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);

    g_menu = build_menu();
    SetMenu(h, g_menu);

    g_loclbl = ctl(h, 0, L"STATIC", L"Where the menu appears", SS_NOPREFIX, 0, 0, 10, 10, IDC_LOCLBL);
    g_loc = ctl(h, WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, IDC_LOC);
    for (int i = 0; i < NLOCS; i++) SendMessageW(g_loc, LB_ADDSTRING, 0, (LPARAM)LOCS[i].name);
    g_extlbl = ctl(h, 0, L"STATIC", L"File extension (for \"Specific file type\"):", SS_NOPREFIX, 0, 0, 10, 10, IDC_EXTLBL);
    g_ext = ctl(h, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, IDC_EXT);
    SendMessageW(g_ext, EM_SETCUEBANNER, TRUE, (LPARAM)L".txt");

    g_mode_cmd = ctl(h, 0, L"BUTTON", L"Menu commands",
                     WS_GROUP | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 10, 10, IDC_MODE_CMD);
    g_mode_ext = ctl(h, 0, L"BUTTON", L"Shell extensions",
                     WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, 0, 10, 10, IDC_MODE_EXT);
    g_filter = ctl(h, WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, IDC_FILTER);
    SendMessageW(g_filter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter entries (Ctrl+F)");

    ctl(h, 0, L"BUTTON", L"Add\x2026", WS_TABSTOP, 0, 0, 10, 10, IDC_ADD);
    ctl(h, 0, L"BUTTON", L"Edit\x2026", WS_TABSTOP, 0, 0, 10, 10, IDC_EDIT);
    ctl(h, 0, L"BUTTON", L"Delete", WS_TABSTOP, 0, 0, 10, 10, IDC_DEL);
    ctl(h, 0, L"BUTTON", L"Refresh", WS_TABSTOP, 0, 0, 10, 10, IDC_REFRESH);

    g_list = ctl(h, WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                 WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0, 10, 10, IDC_LIST);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(g_list, L"Explorer", NULL);
    g_il = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), ILC_COLOR32 | ILC_MASK, 32, 32);
    ListView_SetImageList(g_list, g_il, LVSIL_SMALL);

    g_det1 = ctl(h, 0, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 10, 10, IDC_DET1);
    g_det2 = ctl(h, 0, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 10, 10, IDC_DET2);
    g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                               h, (HMENU)(INT_PTR)IDC_STATUS, g_inst, NULL);
    EnumChildWindows(h, font_cb, (LPARAM)g_font);
    SendMessageW(g_status, SB_SETTEXTW, 1 | SBT_NOBORDERS, (LPARAM)(g_admin ? L"Administrator" : L"Standard user (system entries are read-only)"));

    int li = (haveCfg && g_cfg.loc >= 0 && g_cfg.loc < NLOCS - 1) ? g_cfg.loc : 0;
    SendMessageW(g_loc, LB_SETCURSEL, (WPARAM)li, 0);
    if (haveCfg) {
        g_mode = g_cfg.mode == MODE_HANDLERS ? MODE_HANDLERS : MODE_VERBS;
        g_sortcol = (g_cfg.sortcol >= 0 && g_cfg.sortcol < NCOLS) ? g_cfg.sortcol : 0;
        g_sortdesc = g_cfg.sortdesc != 0;
    }
    layout();
    reload();
}

static LRESULT on_notify(NMHDR *nh)
{
    if (nh->hwndFrom != g_list) return 0;
    switch (nh->code) {
    case NM_DBLCLK:
        if (g_mode == MODE_VERBS && sel_item()) do_edit();
        break;
    case LVN_KEYDOWN:
        if (((NMLVKEYDOWN *)nh)->wVKey == VK_DELETE) do_delete();
        break;
    case LVN_COLUMNCLICK: {
        int c = ((NMLISTVIEW *)nh)->iSubItem;
        if (c == g_sortcol) g_sortdesc = !g_sortdesc; else { g_sortcol = c; g_sortdesc = FALSE; }
        reload();
        break; }
    case LVN_ITEMCHANGED: {
        NMLISTVIEW *nm = (NMLISTVIEW *)nh;
        if (g_loading || !(nm->uChanged & LVIF_STATE)) break;
        UINT os = (nm->uOldState & LVIS_STATEIMAGEMASK) >> 12, ns = (nm->uNewState & LVIS_STATEIMAGEMASK) >> 12;
        if (os != ns && os != 0 && ns != 0) apply_toggle(nm->iItem, ns == 2);
        else if ((nm->uOldState ^ nm->uNewState) & LVIS_SELECTED) update_ui_state();
        break; }
    case NM_CUSTOMDRAW: {                                   /* grey out disabled entries */
        NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)nh;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            int idx = (int)cd->nmcd.lItemlParam;
            if (idx >= 0 && idx < g_items.n && g_items.v[idx].off) cd->clrText = GetSysColor(COLOR_GRAYTEXT);
            return CDRF_NEWFONT;
        }
        break; }
    }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        on_create(h, ((CREATESTRUCTW *)l)->lpCreateParams != NULL);
        return 0;
    case WM_SIZE:
        if (g_status) layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = S(880); mm->ptMinTrackSize.y = S(520);
        return 0; }
    case WM_NOTIFY:
        return on_notify((NMHDR *)l);
    case WM_CONTEXTMENU:
        if ((HWND)w == g_list) {
            POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
            if (l == -1) {                                    /* keyboard: Shift+F10 / menu key */
                RECT rc; int r = sel_row();
                GetCursorPos(&pt);
                if (r >= 0 && ListView_GetItemRect(g_list, r, &rc, LVIR_LABEL)) { pt.x = rc.left; pt.y = rc.bottom; ClientToScreen(g_list, &pt); }
            } else {
                LVHITTESTINFO hi = { 0 };
                hi.pt = pt;
                ScreenToClient(g_list, &hi.pt);
                int r = ListView_HitTest(g_list, &hi);
                if (r < 0) return 0;
                ListView_SetItemState(g_list, r, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
            show_popup(pt);
            return 0;
        }
        break;
    case WM_TIMER:
        if (w == TIMER_RELOAD) { KillTimer(h, TIMER_RELOAD); reload(); }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_LOC:
            if (HIWORD(w) == LBN_SELCHANGE) {
                if (SendMessageW(g_loc, LB_GETCURSEL, 0, 0) == NLOCS - 1) SetFocus(g_ext);
                reload();
            }
            break;
        case IDC_EXT:
            if (HIWORD(w) == EN_CHANGE) {
                if (SendMessageW(g_loc, LB_GETCURSEL, 0, 0) != NLOCS - 1) SendMessageW(g_loc, LB_SETCURSEL, (WPARAM)(NLOCS - 1), 0);
                SetTimer(h, TIMER_RELOAD, 350, NULL);
            }
            break;
        case IDC_FILTER:
            if (HIWORD(w) == EN_CHANGE) SetTimer(h, TIMER_RELOAD, 350, NULL);
            break;
        case IDC_MODE_CMD: case IDM_VIEW_CMD: set_mode(MODE_VERBS); break;
        case IDC_MODE_EXT: case IDM_VIEW_EXT: set_mode(MODE_HANDLERS); break;
        case IDC_ADD:  case IDM_ADD:     do_add(); break;
        case IDC_EDIT: case IDM_EDIT:    do_edit(); break;
        case IDC_DEL:  case IDM_DELETE:  do_delete(); break;
        case IDC_REFRESH: case IDM_REFRESH: reload(); break;
        case IDM_TOGGLE:  do_toggle(); break;
        case IDM_EXPORT:  do_export(); break;
        case IDM_IMPORT:  do_import(); break;
        case IDM_COPYCMD: do_copy(); break;
        case IDM_REGEDIT: do_regedit(); break;
        case IDM_BACKUPS: open_backups(); break;
        case IDM_FIND:    SetFocus(g_filter); SendMessageW(g_filter, EM_SETSEL, 0, -1); break;
        case IDM_CLASSIC: toggle_classic(); break;
        case IDM_RESTART: restart_explorer(); break;
        case IDM_ADMIN:   relaunch_admin(); break;
        case IDM_ABOUT:   show_about(); break;
        case IDM_EXIT:    PostMessageW(h, WM_CLOSE, 0, 0); break;
        }
        return 0;
    case WM_CLOSE:
        settings_save();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        free(g_items.v);
        if (g_il) ImageList_Destroy(g_il);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmdline, int show)
{
    (void)hp; (void)cmdline;
    g_inst = hi;
    HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    g_admin = is_elevated();

    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&ic);
    BOOL haveCfg = settings_load();

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon   = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"CtxEdMain";
    RegisterClassExW(&wc);
    dialog_init(hi);

    static ACCEL acc[] = {
        { FVIRTKEY | FCONTROL, 'N', IDM_ADD },       { FVIRTKEY, VK_F2, IDM_EDIT },
        { FVIRTKEY | FCONTROL, 'E', IDM_EXPORT },    { FVIRTKEY, VK_F5, IDM_REFRESH },
        { FVIRTKEY | FCONTROL, 'F', IDM_FIND },      { FVIRTKEY | FCONTROL, '1', IDM_VIEW_CMD },
        { FVIRTKEY | FCONTROL, '2', IDM_VIEW_EXT },
    };
    HACCEL haccel = CreateAcceleratorTableW(acc, (int)(sizeof acc / sizeof acc[0]));

    wchar_t title[128];
    wf(title, 128, L"%ls%ls", APP_TITLE, g_admin ? L"  (Administrator)" : L"");
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = S(1120), h = S(700);
    if (haveCfg) { x = g_cfg.rc.left; y = g_cfg.rc.top; w = g_cfg.rc.right - g_cfg.rc.left; h = g_cfg.rc.bottom - g_cfg.rc.top; }
    HWND win = CreateWindowExW(0, L"CtxEdMain", title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h,
                               NULL, NULL, hi, haveCfg ? (LPVOID)&g_cfg : NULL);
    if (!win) return 1;
    ShowWindow(win, (haveCfg && g_cfg.maximized) ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(win);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (TranslateAcceleratorW(win, haccel, &msg)) continue;
        if (IsDialogMessageW(win, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
