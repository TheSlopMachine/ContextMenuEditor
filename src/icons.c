#include "common.h"
#include "icons.h"

static HICON s_generic;

HICON icon_generic(void)
{
    if (!s_generic) {
        SHFILEINFOW sfi = { 0 };
        if (SHGetFileInfoW(L"file.exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                           SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
            s_generic = sfi.hIcon;
    }
    return s_generic ? CopyIcon(s_generic) : NULL;
}

/* Expand %VARS%, then find the file as given or on the search path. out: MAX_PATH. */
static BOOL resolve(const wchar_t *in, wchar_t *out)
{
    wchar_t t[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(in, t, MAX_PATH * 2);
    if (n == 0 || n > MAX_PATH * 2 || wcslen(t) >= MAX_PATH) return FALSE;
    if (PathFileExistsW(t)) { wcscpy(out, t); return TRUE; }
    return SearchPathW(NULL, t, L".exe", MAX_PATH, out, NULL) != 0;
}

static HICON from_file(const wchar_t *path, int index)
{
    HICON small = NULL;
    UINT n = ExtractIconExW(path, index, NULL, &small, 1);
    if (n != 0 && n != (UINT)-1 && small) return small;
    return NULL;
}

static HICON shell_icon(const wchar_t *path)
{
    SHFILEINFOW sfi = { 0 };
    if (SHGetFileInfoW(path, 0, &sfi, sizeof sfi, SHGFI_ICON | SHGFI_SMALLICON)) return sfi.hIcon;
    return NULL;
}

/* "path", "\"path\"", "path,3", "path,-101" */
static HICON from_spec(const wchar_t *spec)
{
    wchar_t s[MAX_PATH * 2], path[MAX_PATH];
    int index = 0;
    wf(s, MAX_PATH * 2, L"%ls", spec);
    StrTrimW(s, L" \t\"");
    wchar_t *comma = wcsrchr(s, L',');
    if (comma) {
        wchar_t *end;
        long v = wcstol(comma + 1, &end, 10);
        if (end != comma + 1 && *end == 0) { index = (int)v; *comma = 0; StrTrimW(s, L" \t\""); }
    }
    if (!resolve(s, path)) return NULL;
    HICON h = from_file(path, index);
    return h ? h : shell_icon(path);
}

/* Find the executable at the start of a command line (handles unquoted paths with spaces). */
static BOOL exe_from_command(const wchar_t *cmd, wchar_t *out)
{
    wchar_t s[1024];
    wf(s, 1024, L"%ls", cmd);
    StrTrimW(s, L" \t");
    if (s[0] == L'"') {
        wchar_t *e = wcschr(s + 1, L'"');
        if (e) *e = 0;
        return resolve(s + 1, out);
    }
    wchar_t *p = s;
    for (int tries = 0; tries < 10; tries++) {
        wchar_t *sp = wcschr(p, L' ');
        if (sp) *sp = 0;
        BOOL ok = resolve(s, out);
        if (sp) *sp = L' ';
        if (ok) return TRUE;
        if (!sp) break;
        p = sp + 1;
    }
    return FALSE;
}

HICON icon_load(const wchar_t *iconSpec, const wchar_t *command, const wchar_t *module)
{
    HICON h = NULL;
    wchar_t exe[MAX_PATH];
    if (iconSpec && iconSpec[0] && iconSpec[0] != L'@') h = from_spec(iconSpec);
    if (!h && command && command[0] && exe_from_command(command, exe)) {
        h = from_file(exe, 0);
        if (!h) h = shell_icon(exe);
    }
    if (!h && module && module[0]) h = from_spec(module);
    return h;
}
