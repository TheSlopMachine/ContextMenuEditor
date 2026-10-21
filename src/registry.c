#include "common.h"
#include "registry.h"

#define CLASSIC_KEY L"Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}"
#define BLOCKED_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked"

const Loc LOCS[] = {
    { L"All files",              L"*" },
    { L"Files and folders",      L"AllFilesystemObjects" },
    { L"Folders",                L"Directory" },
    { L"Folder background",      L"Directory\\Background" },
    { L"Desktop background",     L"DesktopBackground" },
    { L"Drives",                 L"Drive" },
    { L"This PC",                L"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}" },
    { L"Recycle Bin",            L"CLSID\\{645FF040-5081-101B-9F08-00AA002F954E}" },
    { L"Unknown file types",     L"Unknown" },
    { L"Specific file type\x2026", NULL },
};
const int NLOCS = (int)(sizeof(LOCS) / sizeof(LOCS[0]));

/* ------------------------------------------------------------ low level */
static BOOL rd_str(HKEY root, const wchar_t *sub, const wchar_t *val, wchar_t *out, DWORD cch)
{
    HKEY k = root; BOOL close = FALSE; DWORD cb = cch * sizeof(wchar_t); LONG r;
    out[0] = 0;
    if (sub) {
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | WOW, &k) != ERROR_SUCCESS) return FALSE;
        close = TRUE;
    }
    r = RegGetValueW(k, NULL, val, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, NULL, out, &cb);
    if (close) RegCloseKey(k);
    if (r != ERROR_SUCCESS) { out[0] = 0; return FALSE; }
    return TRUE;
}

static BOOL val_exists(HKEY k, const wchar_t *name)
{
    return RegQueryValueExW(k, name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
}

static LONG set_str(HKEY k, const wchar_t *name, const wchar_t *v)
{
    return RegSetValueExW(k, name, 0, has_envvar(v) ? REG_EXPAND_SZ : REG_SZ,
                          (const BYTE *)v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
}

static LONG del_val(HKEY k, const wchar_t *name)
{
    LONG r = RegDeleteValueW(k, name);
    return r == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : r;
}

static BOOL blocked_in(HKEY root, const wchar_t *clsid)
{
    HKEY k; BOOL r = FALSE;
    if (RegOpenKeyExW(root, BLOCKED_KEY, 0, KEY_QUERY_VALUE | WOW, &k) == ERROR_SUCCESS) {
        r = val_exists(k, clsid);
        RegCloseKey(k);
    }
    return r;
}

LONG key_delete(HKEY root, const wchar_t *path)
{
    wchar_t parent[800];
    wf(parent, 800, L"%ls", path);
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (!slash) return ERROR_INVALID_PARAMETER;
    *slash = 0;
    HKEY p;
    LONG r = RegOpenKeyExW(root, parent, 0, KEY_ALL_ACCESS | WOW, &p);
    if (r != ERROR_SUCCESS) return r;
    r = RegDeleteTreeW(p, slash + 1);
    RegCloseKey(p);
    return r;
}

/* ------------------------------------------------------------ item list */
static Item *new_item(ItemList *l)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        Item *v = (Item *)realloc(l->v, (size_t)cap * sizeof(Item));
        if (!v) { MessageBoxW(NULL, L"Out of memory.", APP_TITLE, MB_ICONERROR); ExitProcess(1); }
        l->v = v; l->cap = cap;
    }
    Item *it = &l->v[l->n++];
    memset(it, 0, sizeof *it);
    return it;
}

void items_clear(ItemList *l) { l->n = 0; }

const wchar_t *hive_short(HKEY h) { return h == HKEY_CURRENT_USER ? L"HKCU" : L"HKLM"; }

static void item_parent(wchar_t *out, size_t n, ViewMode mode, const wchar_t *sub)
{
    wf(out, n, mode == MODE_VERBS ? L"Software\\Classes\\%ls\\shell"
                                  : L"Software\\Classes\\%ls\\shellex\\ContextMenuHandlers", sub);
}

void item_key_path(wchar_t *out, size_t n, ViewMode mode, const wchar_t *sub, const Item *it)
{
    wchar_t p[600];
    item_parent(p, 600, mode, sub);
    wf(out, n, L"%ls\\%ls", p, it->name);
}

static void read_verb(HKEY parent, Item *it)
{
    HKEY v;
    if (RegOpenKeyExW(parent, it->name, 0, KEY_READ | WOW, &v) != ERROR_SUCCESS) {
        wf(it->text, 512, L"%ls", it->name);
        wf(it->disp, 512, L"%ls", it->name);
        return;
    }
    if (rd_str(v, NULL, L"MUIVerb", it->text, 512)) it->hasMUI = TRUE;
    else rd_str(v, NULL, NULL, it->text, 512);
    if (!it->text[0]) wf(it->text, 512, L"%ls", it->name);

    if (it->text[0] == L'@' || it->hasMUI) {
        if (FAILED(SHLoadIndirectString(it->text, it->disp, 512, NULL))) wf(it->disp, 512, L"%ls", it->text);
    } else wf(it->disp, 512, L"%ls", it->text);

    rd_str(v, L"command", NULL, it->cmd, 1024);
    if (!it->cmd[0]) {
        if (val_exists(v, L"SubCommands")) it->kind = 1;
        else if (val_exists(v, L"ExplorerCommandHandler")) it->kind = 2;
        else if (RegGetValueW(v, L"command", L"DelegateExecute", RRF_RT_ANY, NULL, NULL, NULL) == ERROR_SUCCESS)
            it->kind = 2;
    }
    rd_str(v, NULL, L"Icon", it->icon, 512);
    rd_str(v, NULL, L"Position", it->pos, 16);
    it->shift = val_exists(v, L"Extended");
    it->off   = val_exists(v, L"LegacyDisable");
    DWORD cb = sizeof(DWORD);
    RegGetValueW(v, NULL, L"CommandFlags", RRF_RT_REG_DWORD, NULL, &it->flags, &cb);
    RegCloseKey(v);
}

static void read_handler(HKEY parent, Item *it)
{
    wchar_t p[400];
    rd_str(parent, it->name, NULL, it->clsid, 64);
    if (it->clsid[0] != L'{') {
        if (it->name[0] == L'{') wf(it->clsid, 64, L"%ls", it->name);
        else it->clsid[0] = 0;
    }
    if (it->clsid[0]) {
        wf(p, 400, L"CLSID\\%ls", it->clsid);
        rd_str(HKEY_CLASSES_ROOT, p, NULL, it->text, 512);
        wf(p, 400, L"CLSID\\%ls\\InprocServer32", it->clsid);
        rd_str(HKEY_CLASSES_ROOT, p, NULL, it->module, MAX_PATH);
        if (!it->module[0]) {
            wf(p, 400, L"CLSID\\%ls\\LocalServer32", it->clsid);
            rd_str(HKEY_CLASSES_ROOT, p, NULL, it->module, MAX_PATH);
        }
        it->off = blocked_in(HKEY_CURRENT_USER, it->clsid) || blocked_in(HKEY_LOCAL_MACHINE, it->clsid);
    }
    wf(it->disp, 512, L"%ls", it->text[0] ? it->text : it->name);
}

void items_load(ItemList *l, ViewMode mode, const wchar_t *sub)
{
    HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    wchar_t path[600], nm[256];
    item_parent(path, 600, mode, sub);
    for (int r = 0; r < 2; r++) {
        HKEY k;
        if (RegOpenKeyExW(roots[r], path, 0, KEY_READ | WOW, &k) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; i++) {
            DWORD cch = 256;
            if (RegEnumKeyExW(k, i, nm, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            Item *it = new_item(l);
            it->hive = roots[r];
            wf(it->name, 256, L"%ls", nm);
            if (mode == MODE_VERBS) read_verb(k, it);
            else read_handler(k, it);
        }
        RegCloseKey(k);
    }
}

const wchar_t *item_cell(const Item *it, ViewMode mode, int col, wchar_t *buf, size_t n)
{
    buf[0] = 0;
    switch (col) {
    case 0: return it->disp;
    case 1: return it->name;
    case 2: return it->hive == HKEY_CURRENT_USER ? L"User" : L"System";
    }
    if (mode == MODE_HANDLERS) return col == 3 ? it->module : (it->off ? L"Blocked" : L"Active");

    if (col == 3) {
        if (it->cmd[0]) return it->cmd;
        return it->kind == 1 ? L"(cascading submenu)" : it->kind == 2 ? L"(shell handler)" : L"";
    }
    /* options */
    const wchar_t *parts[4]; int np = 0;
    if (it->pos[0])         parts[np++] = it->pos;
    if (it->shift)          parts[np++] = L"Shift-only";
    if (it->flags & 0x20)   parts[np++] = L"Separator above";
    if (it->flags & 0x40)   parts[np++] = L"Separator below";
    for (int i = 0; i < np; i++) {
        size_t len = wcslen(buf);
        wf(buf + len, n - len, L"%ls%ls", i ? L", " : L"", parts[i]);
    }
    return buf;
}

static ViewMode s_mode; static int s_col; static BOOL s_desc;

static int cmp_item(const void *a, const void *b)
{
    const Item *x = (const Item *)a, *y = (const Item *)b;
    wchar_t bx[200], by[200];
    int r = _wcsicmp(item_cell(x, s_mode, s_col, bx, 200), item_cell(y, s_mode, s_col, by, 200));
    if (!r) r = _wcsicmp(x->disp, y->disp);
    if (!r) r = (x->hive == HKEY_CURRENT_USER ? 0 : 1) - (y->hive == HKEY_CURRENT_USER ? 0 : 1);
    return s_desc ? -r : r;
}

void items_sort(ItemList *l, ViewMode mode, int col, BOOL desc)
{
    s_mode = mode; s_col = col; s_desc = desc;
    if (l->n > 1) qsort(l->v, (size_t)l->n, sizeof(Item), cmp_item);
}

/* -------------------------------------------------------------- writing */
BOOL verb_exists(HKEY hive, const wchar_t *sub, const wchar_t *name)
{
    wchar_t path[800]; HKEY k;
    wf(path, 800, L"Software\\Classes\\%ls\\shell\\%ls", sub, name);
    if (RegOpenKeyExW(hive, path, 0, KEY_READ | WOW, &k) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(k);
    return TRUE;
}

LONG verb_save(HKEY hive, const wchar_t *sub, const wchar_t *name, const wchar_t *text,
               const wchar_t *cmd, const wchar_t *icon, const wchar_t *pos,
               BOOL shift, BOOL sepAbove, BOOL sepBelow, const Item *old)
{
    wchar_t path[800]; HKEY k;
    wf(path, 800, L"Software\\Classes\\%ls\\shell\\%ls", sub, name);
    LONG r = RegCreateKeyExW(hive, path, 0, NULL, 0, KEY_READ | KEY_WRITE | WOW, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return r;

    r = set_str(k, (old && old->hasMUI) ? L"MUIVerb" : NULL, text);
    if (!r) r = icon[0] ? set_str(k, L"Icon", icon) : del_val(k, L"Icon");
    if (!r) r = pos[0]  ? set_str(k, L"Position", pos) : del_val(k, L"Position");
    if (!r) r = shift   ? set_str(k, L"Extended", L"") : del_val(k, L"Extended");
    if (!r) {
        DWORD f = (old ? old->flags : 0) & ~0x60u;
        if (sepAbove) f |= 0x20;
        if (sepBelow) f |= 0x40;
        r = f ? RegSetValueExW(k, L"CommandFlags", 0, REG_DWORD, (const BYTE *)&f, sizeof f)
              : del_val(k, L"CommandFlags");
    }
    if (!r && cmd[0]) {
        HKEY c;
        r = RegCreateKeyExW(k, L"command", 0, NULL, 0, KEY_WRITE | WOW, NULL, &c, NULL);
        if (!r) { r = set_str(c, NULL, cmd); RegCloseKey(c); }
    }
    RegCloseKey(k);
    return r;
}

LONG item_set_enabled(ViewMode mode, const wchar_t *sub, const Item *it, BOOL enable)
{
    HKEY k; LONG r;
    if (mode == MODE_VERBS) {
        wchar_t path[800];
        item_key_path(path, 800, mode, sub, it);
        r = RegOpenKeyExW(it->hive, path, 0, KEY_SET_VALUE | WOW, &k);
        if (r != ERROR_SUCCESS) return r;
        r = enable ? del_val(k, L"LegacyDisable")
                   : RegSetValueExW(k, L"LegacyDisable", 0, REG_SZ, (const BYTE *)L"", sizeof(wchar_t));
        RegCloseKey(k);
        return r;
    }
    if (!it->clsid[0]) return ERROR_CANCELLED;
    r = ERROR_SUCCESS;
    if (enable) {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, BLOCKED_KEY, 0, KEY_SET_VALUE | WOW, &k) == ERROR_SUCCESS) {
            r = del_val(k, it->clsid);
            RegCloseKey(k);
        }
        if (!r && blocked_in(HKEY_LOCAL_MACHINE, it->clsid)) {
            r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, BLOCKED_KEY, 0, KEY_SET_VALUE | WOW, &k);
            if (!r) { r = del_val(k, it->clsid); RegCloseKey(k); }
        }
    } else {
        r = RegCreateKeyExW(HKEY_CURRENT_USER, BLOCKED_KEY, 0, NULL, 0, KEY_SET_VALUE | WOW, NULL, &k, NULL);
        if (!r) { r = RegSetValueExW(k, it->clsid, 0, REG_SZ, (const BYTE *)L"", sizeof(wchar_t)); RegCloseKey(k); }
    }
    return r;
}

/* ------------------------------------------------------------- Windows 11 */
BOOL classic_menu_enabled(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CLASSIC_KEY L"\\InprocServer32", 0, KEY_READ | WOW, &k) != ERROR_SUCCESS)
        return FALSE;
    RegCloseKey(k);
    return TRUE;
}

LONG classic_menu_set(BOOL on)
{
    if (!on) return key_delete(HKEY_CURRENT_USER, CLASSIC_KEY);
    HKEY k;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, CLASSIC_KEY L"\\InprocServer32", 0, NULL, 0,
                             KEY_WRITE | WOW, NULL, &k, NULL);
    if (r == ERROR_SUCCESS) {
        r = RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)L"", sizeof(wchar_t));
        RegCloseKey(k);
    }
    return r;
}

/* Pre-select a key in Regedit (it opens at its "LastKey"), then launch it. */
void regedit_jump(HKEY hive, const wchar_t *path)
{
    wchar_t full[1000]; HKEY k;
    wf(full, 1000, L"Computer\\%ls\\%ls",
       hive == HKEY_CURRENT_USER ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE", path);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"LastKey", 0, REG_SZ, (const BYTE *)full, (DWORD)((wcslen(full) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    ShellExecuteW(NULL, L"open", L"regedit.exe", NULL, NULL, SW_SHOWNORMAL);
}
