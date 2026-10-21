/* Shared includes, constants and small helpers used by every module. */
#ifndef COMMON_H
#define COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#undef  WINVER
#define WINVER 0x0601

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

#define APP_TITLE   L"Context Menu Editor"
#define APP_VERSION L"1.1.0"

/* Always use the 64-bit registry view, even from a 32-bit build. */
#define WOW KEY_WOW64_64KEY

extern HINSTANCE g_inst;
extern HFONT     g_font;
extern int       g_dpi;
#define S(x) MulDiv((x), g_dpi, 96)          /* scale 96-dpi pixels to the screen */

/* printf into a wide buffer; always NUL-terminated. Use %ls for wide strings. */
void wf(wchar_t *buf, size_t n, const wchar_t *fmt, ...);
void err_box(HWND owner, const wchar_t *what, LONG code);
BOOL is_elevated(void);
BOOL has_envvar(const wchar_t *s);
BOOL browse_file(HWND owner, wchar_t *path /* MAX_PATH */, const wchar_t *filter, BOOL save, const wchar_t *defext);
BOOL clipboard_set(HWND owner, const wchar_t *text);
BOOL CALLBACK font_cb(HWND h, LPARAM font);
/* CreateWindowEx for a child control; coordinates are 96-dpi and get scaled. */
HWND ctl(HWND parent, DWORD ex, const wchar_t *cls, const wchar_t *text, DWORD style,
         int x, int y, int w, int h, int id);

#endif
