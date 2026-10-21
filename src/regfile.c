#include "common.h"
#include "regfile.h"

typedef struct { wchar_t *p; size_t n, cap; } Buf;

static void bput(Buf *b, const wchar_t *s)
{
    size_t l = wcslen(s);
    if (b->n + l + 1 > b->cap) {
        b->cap = (b->n + l + 1) * 2 + 4096;
        b->p = (wchar_t *)realloc(b->p, b->cap * sizeof(wchar_t));
        if (!b->p) ExitProcess(1);
    }
    memcpy(b->p + b->n, s, (l + 1) * sizeof(wchar_t));
    b->n += l;
}

static void bput_esc(Buf *b, const wchar_t *s)
{
    wchar_t t[2] = { 0, 0 };
    for (; *s; s++) {
        if (*s == L'\\' || *s == L'"') bput(b, L"\\");
        t[0] = *s;
        bput(b, t);
    }
}

static void export_key(Buf *b, HKEY root, const wchar_t *rootName, const wchar_t *path)
{
    HKEY k; DWORD maxSub = 0, maxName = 0, maxData = 0, i;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | WOW, &k) != ERROR_SUCCESS) return;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, &maxSub, NULL, NULL, &maxName, &maxData, NULL, NULL);

    bput(b, L"["); bput(b, rootName); bput(b, L"\\"); bput(b, path); bput(b, L"]\r\n");

    wchar_t *vn = (wchar_t *)malloc((maxName + 2) * sizeof(wchar_t));
    BYTE *vd = (BYTE *)malloc(maxData + 4);
    if (vn && vd) {
        for (i = 0;; i++) {
            DWORD cn = maxName + 1, cd = maxData, type = 0;
            LONG r = RegEnumValueW(k, i, vn, &cn, NULL, &type, vd, &cd);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            if (!vn[0]) bput(b, L"@=");
            else { bput(b, L"\""); bput_esc(b, vn); bput(b, L"\"="); }
            if (type == REG_SZ) {
                vd[cd] = vd[cd + 1] = 0;
                bput(b, L"\""); bput_esc(b, (wchar_t *)vd); bput(b, L"\"");
            } else if (type == REG_DWORD && cd == 4) {
                DWORD dw; memcpy(&dw, vd, 4);
                wchar_t t[24]; wf(t, 24, L"dword:%08lx", (unsigned long)dw); bput(b, t);
            } else {
                wchar_t t[16];
                if (type == REG_BINARY) bput(b, L"hex:");
                else { wf(t, 16, L"hex(%lx):", (unsigned long)type); bput(b, t); }
                for (DWORD j = 0; j < cd; j++) {
                    wf(t, 16, j ? L",%02x" : L"%02x", (unsigned)vd[j]);
                    bput(b, t);
                }
            }
            bput(b, L"\r\n");
        }
    }
    free(vn); free(vd);
    bput(b, L"\r\n");

    wchar_t *nm = (wchar_t *)malloc((maxSub + 2) * sizeof(wchar_t));
    if (nm) {
        for (i = 0;; i++) {
            DWORD cn = maxSub + 1;
            LONG r = RegEnumKeyExW(k, i, nm, &cn, NULL, NULL, NULL, NULL);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            size_t len = wcslen(path) + cn + 2;
            wchar_t *sp = (wchar_t *)malloc(len * sizeof(wchar_t));
            if (!sp) break;
            wf(sp, len, L"%ls\\%ls", path, nm);
            export_key(b, root, rootName, sp);
            free(sp);
        }
        free(nm);
    }
    RegCloseKey(k);
}

BOOL regfile_write(const wchar_t *file, HKEY root, const wchar_t *path)
{
    Buf b = { 0 };
    bput(&b, L"Windows Registry Editor Version 5.00\r\n\r\n");
    export_key(&b, root, root == HKEY_CURRENT_USER ? L"HKEY_CURRENT_USER" : L"HKEY_LOCAL_MACHINE", path);
    FILE *f = _wfopen(file, L"wb");
    if (!f) { free(b.p); return FALSE; }
    unsigned short bom = 0xFEFF;                       /* UTF-16LE byte-order mark */
    fwrite(&bom, 2, 1, f);
    fwrite(b.p, sizeof(wchar_t), b.n, f);
    fclose(f);
    free(b.p);
    return TRUE;
}
