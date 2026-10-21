#include "common.h"
#include "dialog.h"
#include "icons.h"

enum {
    D_TPLLBL = 200, D_TPL, D_KEY, D_TEXT, D_CMD, D_CMDB, D_ICON, D_ICONB, D_PREVIEW, D_POS,
    D_SCOPELBL, D_SCOPE, D_EXT, D_SEPB, D_SEPA, D_NOTE
};
#define TIMER_PREVIEW 1

typedef struct { const Item *orig; const wchar_t *sub; BOOL done, ok, ready; } Dlg;

static const struct { const wchar_t *name, *key, *text, *cmd, *icon; } TPL[] = {
    { L"(blank)",               L"",                 L"",                          L"", L"" },
    { L"Open with Notepad",     L"OpenWithNotepad",  L"Open with Notepad",         L"notepad.exe \"%1\"", L"notepad.exe" },
    { L"Command Prompt here",   L"CmdHere",          L"Open Command Prompt here",  L"cmd.exe /s /k pushd \"%V\"", L"cmd.exe" },
    { L"PowerShell here",       L"PowerShellHere",   L"Open PowerShell here",
      L"powershell.exe -NoExit -Command Set-Location -LiteralPath '%V'", L"powershell.exe" },
    { L"Windows Terminal here", L"WindowsTerminalHere", L"Open Windows Terminal here", L"wt.exe -d \"%V\"", L"wt.exe" },
};
#define NTPL ((int)(sizeof(TPL) / sizeof(TPL[0])))

static const wchar_t *arg_token(const wchar_t *sub)
{
    return (!wcsncmp(sub, L"Directory\\Background", 20) || !wcscmp(sub, L"DesktopBackground")) ? L"%V" : L"%1";
}

static void update_preview(HWND h)
{
    wchar_t icon[512], cmd[1024];
    GetDlgItemTextW(h, D_ICON, icon, 512);
    GetDlgItemTextW(h, D_CMD, cmd, 1024);
    StrTrimW(icon, L" \t"); StrTrimW(cmd, L" \t");
    HICON ic = (icon[0] || cmd[0]) ? icon_load(icon, cmd, NULL) : NULL;
    if (!ic && (icon[0] || cmd[0])) ic = icon_generic();
    HICON old = (HICON)SendDlgItemMessageW(h, D_PREVIEW, STM_SETICON, (WPARAM)ic, 0);
    if (old) DestroyIcon(old);
}

static void dlg_build(HWND h, Dlg *d)
{
    const BOOL edit = d->orig != NULL;
    const Item *o = d->orig;
    const DWORD EDS = WS_TABSTOP | ES_AUTOHSCROLL;
    #define ROW(n) (14 + (n) * 32)

    if (!edit) {
        ctl(h, 0, L"STATIC", L"Start from:", 0, 12, ROW(0) + 4, 110, 20, D_TPLLBL);
        HWND c = ctl(h, 0, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 128, ROW(0), 330, 220, D_TPL);
        for (int i = 0; i < NTPL; i++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)TPL[i].name);
        SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
    ctl(h, 0, L"STATIC", L"Menu text:", 0, 12, ROW(1) + 4, 110, 20, 0);
    ctl(h, WS_EX_CLIENTEDGE, L"EDIT", edit ? o->text : L"", EDS, 128, ROW(1), 420, 24, D_TEXT);
    ctl(h, 0, L"STATIC", L"Command:", 0, 12, ROW(2) + 4, 110, 20, 0);
    ctl(h, WS_EX_CLIENTEDGE, L"EDIT", edit ? o->cmd : L"", EDS | ((edit && o->kind) ? WS_DISABLED : 0), 128, ROW(2), 330, 24, D_CMD);
    ctl(h, 0, L"BUTTON", L"Browse...", WS_TABSTOP | ((edit && o->kind) ? WS_DISABLED : 0), 464, ROW(2), 84, 24, D_CMDB);
    ctl(h, 0, L"STATIC", L"Icon:", 0, 12, ROW(3) + 4, 110, 20, 0);
    ctl(h, WS_EX_CLIENTEDGE, L"EDIT", edit ? o->icon : L"", EDS, 128, ROW(3), 296, 24, D_ICON);
    ctl(h, 0, L"STATIC", L"", SS_ICON | SS_CENTERIMAGE, 430, ROW(3), 28, 24, D_PREVIEW);
    ctl(h, 0, L"BUTTON", L"Browse...", WS_TABSTOP, 464, ROW(3), 84, 24, D_ICONB);

    ctl(h, 0, L"STATIC", L"Key name:", 0, 12, ROW(4) + 4, 110, 20, 0);
    HWND k = ctl(h, WS_EX_CLIENTEDGE, L"EDIT", edit ? o->name : L"", EDS | (edit ? ES_READONLY : 0), 128, ROW(4), 420, 24, D_KEY);
    if (!edit) SendMessageW(k, EM_SETCUEBANNER, TRUE, (LPARAM)L"Optional - generated from the menu text");

    ctl(h, 0, L"STATIC", L"Position:", 0, 12, ROW(5) + 4, 110, 20, 0);
    HWND pc = ctl(h, 0, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 128, ROW(5), 120, 120, D_POS);
    SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"Default");
    SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"Top");
    SendMessageW(pc, CB_ADDSTRING, 0, (LPARAM)L"Bottom");
    int pi = 0;
    if (edit && !_wcsicmp(o->pos, L"Top")) pi = 1;
    if (edit && !_wcsicmp(o->pos, L"Bottom")) pi = 2;
    SendMessageW(pc, CB_SETCURSEL, (WPARAM)pi, 0);

    if (!edit) {
        ctl(h, 0, L"STATIC", L"Scope:", 0, 270, ROW(5) + 4, 50, 20, D_SCOPELBL);
        HWND sc = ctl(h, 0, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 322, ROW(5), 226, 120, D_SCOPE);
        SendMessageW(sc, CB_ADDSTRING, 0, (LPARAM)L"Current user (no admin needed)");
        SendMessageW(sc, CB_ADDSTRING, 0, (LPARAM)L"All users (needs admin)");
        SendMessageW(sc, CB_SETCURSEL, 0, 0);
    }
    HWND b;
    b = ctl(h, 0, L"BUTTON", L"Only show when Shift is held (extended menu)", WS_TABSTOP | BS_AUTOCHECKBOX, 128, ROW(6), 420, 22, D_EXT);
    if (edit && o->shift) SendMessageW(b, BM_SETCHECK, BST_CHECKED, 0);
    b = ctl(h, 0, L"BUTTON", L"Separator above", WS_TABSTOP | BS_AUTOCHECKBOX, 128, ROW(7), 160, 22, D_SEPB);
    if (edit && (o->flags & 0x20)) SendMessageW(b, BM_SETCHECK, BST_CHECKED, 0);
    b = ctl(h, 0, L"BUTTON", L"Separator below", WS_TABSTOP | BS_AUTOCHECKBOX, 300, ROW(7), 160, 22, D_SEPA);
    if (edit && (o->flags & 0x40)) SendMessageW(b, BM_SETCHECK, BST_CHECKED, 0);

    ctl(h, 0, L"STATIC",
        (edit && o->kind) ? L"This entry is a cascading submenu or COM handler; its command can't be edited here."
                          : L"%1 = the selected item, %V = the current folder or item. Quote paths that contain spaces.",
        0, 12, ROW(8) + 2, 536, 36, D_NOTE);

    ctl(h, 0, L"BUTTON", edit ? L"Save" : L"Add", WS_TABSTOP | BS_DEFPUSHBUTTON, 370, ROW(8) + 44, 86, 28, IDOK);
    ctl(h, 0, L"BUTTON", L"Cancel", WS_TABSTOP, 462, ROW(8) + 44, 86, 28, IDCANCEL);
    EnumChildWindows(h, font_cb, (LPARAM)g_font);
    SetFocus(GetDlgItem(h, edit ? D_TEXT : D_TPL));
    d->ready = TRUE;
    update_preview(h);
    #undef ROW
}

/* "Open in Terminal!" -> "OpenInTerminal" */
static void derive_key(const wchar_t *text, wchar_t *out, size_t n)
{
    size_t j = 0;
    for (const wchar_t *p = text; *p && j < n - 1 && j < 48; p++)
        if (iswalnum(*p)) out[j++] = *p;
    out[j] = 0;
    if (!j) wf(out, n, L"Command");
}

static void dlg_ok(HWND h, Dlg *d)
{
    wchar_t key[256], text[512], cmd[1024], icon[512];
    const Item *o = d->orig;
    GetDlgItemTextW(h, D_KEY, key, 256);   StrTrimW(key, L" \t");
    GetDlgItemTextW(h, D_TEXT, text, 512); StrTrimW(text, L" \t");
    GetDlgItemTextW(h, D_CMD, cmd, 1024);  StrTrimW(cmd, L" \t");
    GetDlgItemTextW(h, D_ICON, icon, 512); StrTrimW(icon, L" \t");

    if (!text[0]) { MessageBoxW(h, L"Enter the text to show in the menu.", APP_TITLE, MB_ICONWARNING); SetFocus(GetDlgItem(h, D_TEXT)); return; }
    BOOL needCmd = !o || (o->kind == 0 && o->cmd[0]);
    if (o && o->kind) cmd[0] = 0;
    if (needCmd && !cmd[0]) { MessageBoxW(h, L"Enter a command to run.", APP_TITLE, MB_ICONWARNING); SetFocus(GetDlgItem(h, D_CMD)); return; }

    HKEY hive = o ? o->hive
                  : (SendDlgItemMessageW(h, D_SCOPE, CB_GETCURSEL, 0, 0) == 1 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER);
    if (!o) {
        if (key[0]) {
            if (wcschr(key, L'\\') || wcschr(key, L'/')) {
                MessageBoxW(h, L"The key name can't contain slashes.", APP_TITLE, MB_ICONWARNING);
                return;
            }
            if (verb_exists(hive, d->sub, key)) {
                MessageBoxW(h, L"An entry with this key name already exists in that scope.", APP_TITLE, MB_ICONWARNING);
                return;
            }
        } else {
            wchar_t base[256];
            derive_key(text, base, 256);
            wf(key, 256, L"%ls", base);
            for (int i = 2; verb_exists(hive, d->sub, key) && i < 1000; i++) wf(key, 256, L"%ls%d", base, i);
        }
    }
    static const wchar_t *POS[] = { L"", L"Top", L"Bottom" };
    int pi = (int)SendDlgItemMessageW(h, D_POS, CB_GETCURSEL, 0, 0);
    if (pi < 0 || pi > 2) pi = 0;
    LONG r = verb_save(hive, d->sub, key, text, cmd, icon, POS[pi],
                       IsDlgButtonChecked(h, D_EXT) == BST_CHECKED,
                       IsDlgButtonChecked(h, D_SEPB) == BST_CHECKED,
                       IsDlgButtonChecked(h, D_SEPA) == BST_CHECKED, o);
    if (r != ERROR_SUCCESS) { err_box(h, L"Could not save the entry.", r); return; }
    d->ok = d->done = TRUE;
}

static void browse(HWND h, Dlg *d, int id)
{
    wchar_t f[MAX_PATH] = L"", out[MAX_PATH + 40];
    if (id == D_CMDB) {
        if (!browse_file(h, f, L"Programs\0*.exe;*.bat;*.cmd;*.com;*.ps1\0All files\0*.*\0", FALSE, NULL)) return;
        wf(out, MAX_PATH + 40, L"\"%ls\" \"%ls\"", f, arg_token(d->sub));
        SetDlgItemTextW(h, D_CMD, out);
        if (!GetWindowTextLengthW(GetDlgItem(h, D_ICON))) {
            wf(out, MAX_PATH + 40, L"%ls,0", f);
            SetDlgItemTextW(h, D_ICON, out);
        }
    } else {
        if (!browse_file(h, f, L"Icons and programs\0*.ico;*.exe;*.dll\0All files\0*.*\0", FALSE, NULL)) return;
        wf(out, MAX_PATH + 40, L"%ls%ls", f, StrStrIW(f, L".ico") ? L"" : L",0");
        SetDlgItemTextW(h, D_ICON, out);
    }
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    Dlg *d = (Dlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE:
        d = (Dlg *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)d);
        dlg_build(h, d);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (!d) break;
        if (id == IDOK) { dlg_ok(h, d); return 0; }
        if (id == IDCANCEL) { d->done = TRUE; return 0; }
        if (id == D_TPL && HIWORD(w) == CBN_SELCHANGE) {
            int i = (int)SendDlgItemMessageW(h, D_TPL, CB_GETCURSEL, 0, 0);
            if (i > 0) {
                SetDlgItemTextW(h, D_KEY, TPL[i].key);   SetDlgItemTextW(h, D_TEXT, TPL[i].text);
                SetDlgItemTextW(h, D_CMD, TPL[i].cmd);   SetDlgItemTextW(h, D_ICON, TPL[i].icon);
            }
            return 0;
        }
        if (id == D_CMDB || id == D_ICONB) { browse(h, d, id); return 0; }
        if ((id == D_CMD || id == D_ICON) && HIWORD(w) == EN_CHANGE && d->ready) {
            SetTimer(h, TIMER_PREVIEW, 300, NULL);               /* debounce icon lookups */
            return 0;
        }
        break; }
    case WM_TIMER:
        if (w == TIMER_PREVIEW) { KillTimer(h, TIMER_PREVIEW); update_preview(h); }
        return 0;
    case WM_CLOSE:
        if (d) d->done = TRUE;
        return 0;
    case WM_DESTROY: {
        HICON old = (HICON)SendDlgItemMessageW(h, D_PREVIEW, STM_SETICON, 0, 0);
        if (old) DestroyIcon(old);
        break; }
    }
    return DefWindowProcW(h, m, w, l);
}

BOOL dialog_init(HINSTANCE inst)
{
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"CtxEdDlg";
    return RegisterClassExW(&wc) != 0;
}

BOOL verb_dialog(HWND parent, const Item *orig, const wchar_t *sub)
{
    Dlg d = { orig, sub, FALSE, FALSE, FALSE };
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, S(560), S(388) }, pr;
    AdjustWindowRectEx(&r, st, FALSE, WS_EX_DLGMODALFRAME);
    GetWindowRect(parent, &pr);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CtxEdDlg", orig ? L"Edit command" : L"Add command",
                               st | WS_VISIBLE, pr.left + ((pr.right - pr.left) - w) / 2,
                               pr.top + ((pr.bottom - pr.top) - h) / 2, w, h, parent, NULL, g_inst, &d);
    if (!dlg) return FALSE;
    EnableWindow(parent, FALSE);
    MSG msg = { 0 }; int rc = 1;
    while (!d.done && (rc = GetMessageW(&msg, NULL, 0, 0)) > 0)
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    EnableWindow(parent, TRUE);
    DestroyWindow(dlg);
    SetForegroundWindow(parent);
    if (rc == 0) PostQuitMessage((int)msg.wParam);
    return d.ok;
}
