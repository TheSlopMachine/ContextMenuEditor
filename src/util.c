#include "common.h"

HINSTANCE g_inst;
HFONT     g_font;
int       g_dpi = 96;

void wf(wchar_t *buf, size_t n, const wchar_t *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, n, fmt, ap);
    va_end(ap);
    buf[n - 1] = 0;
}

void err_box(HWND owner, const wchar_t *what, LONG code)
{
    wchar_t sys[512] = L"", full[1200];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)code, 0, sys, 512, NULL);
    wf(full, 1200, L"%ls\n\n%ls(error %ld)%ls", what, sys, code,
       code == ERROR_ACCESS_DENIED
         ? L"\n\nSystem-wide entries need administrator rights. Use Tools \x2192 Run as administrator."
         : L"");
    MessageBoxW(owner, full, APP_TITLE, MB_ICONERROR);
}

BOOL is_elevated(void)
{
    HANDLE t; TOKEN_ELEVATION e; DWORD n; BOOL r = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) {
        if (GetTokenInformation(t, TokenElevation, &e, sizeof e, &n)) r = e.TokenIsElevated;
        CloseHandle(t);
    }
    return r;
}

/* TRUE if the string contains %NAME% (two or more name characters). */
BOOL has_envvar(const wchar_t *s)
{
    for (const wchar_t *p = wcschr(s, L'%'); p; p = wcschr(p + 1, L'%')) {
        const wchar_t *q = p + 1; int n = 0;
        while (iswalnum(*q) || *q == L'_') { q++; n++; }
        if (n >= 2 && *q == L'%') return TRUE;
    }
    return FALSE;
}

BOOL browse_file(HWND owner, wchar_t *path, const wchar_t *filter, BOOL save, const wchar_t *defext)
{
    OPENFILENAMEW o = { 0 };
    o.lStructSize = sizeof o;
    o.hwndOwner = owner; o.lpstrFile = path; o.nMaxFile = MAX_PATH;
    o.lpstrFilter = filter; o.lpstrDefExt = defext;
    o.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
}

BOOL clipboard_set(HWND owner, const wchar_t *text)
{
    size_t cb = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, cb);
    if (!g) return FALSE;
    void *p = GlobalLock(g);
    if (!p) { GlobalFree(g); return FALSE; }
    memcpy(p, text, cb);
    GlobalUnlock(g);
    if (!OpenClipboard(owner)) { GlobalFree(g); return FALSE; }
    EmptyClipboard();
    BOOL ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
    if (!ok) GlobalFree(g);
    CloseClipboard();
    return ok;
}

BOOL CALLBACK font_cb(HWND h, LPARAM font)
{
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

HWND ctl(HWND parent, DWORD ex, const wchar_t *cls, const wchar_t *text, DWORD style,
         int x, int y, int w, int h, int id)
{
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           parent, (HMENU)(INT_PTR)id, g_inst, NULL);
}
