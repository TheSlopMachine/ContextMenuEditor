/* Registry model: where context menu entries live, loading, saving, deleting. */
#ifndef REGISTRY_H
#define REGISTRY_H
#include "common.h"

typedef enum { MODE_VERBS = 0, MODE_HANDLERS = 1 } ViewMode;

typedef struct { const wchar_t *name, *sub; } Loc;      /* sub == NULL: user-typed file extension */
extern const Loc LOCS[];
extern const int NLOCS;

#define NCOLS 5   /* list columns, see item_cell() */

typedef struct {
    HKEY    hive;                 /* HKEY_CURRENT_USER or HKEY_LOCAL_MACHINE */
    wchar_t name[256];            /* registry key name */
    wchar_t text[512];            /* raw menu text */
    wchar_t disp[512];            /* resolved menu text (@dll,-id expanded) */
    wchar_t cmd[1024];
    wchar_t icon[512];
    wchar_t pos[16];              /* "", "Top" or "Bottom" */
    BOOL    shift, off, hasMUI;
    int     kind;                 /* 0 normal, 1 cascading submenu, 2 COM handler */
    DWORD   flags;                /* CommandFlags (0x20 separator above, 0x40 below) */
    wchar_t clsid[64];            /* shell extensions */
    wchar_t module[MAX_PATH];     /* shell extensions: DLL/EXE implementing the handler */
    int     img;                  /* image list index, owned by the UI */
} Item;

typedef struct { Item *v; int n, cap; } ItemList;

void items_clear(ItemList *l);
void items_load(ItemList *l, ViewMode mode, const wchar_t *sub);
void items_sort(ItemList *l, ViewMode mode, int col, BOOL desc);
/* Text for list column `col`; returns either a field of `it` or `buf`. */
const wchar_t *item_cell(const Item *it, ViewMode mode, int col, wchar_t *buf, size_t n);

const wchar_t *hive_short(HKEY h);                      /* "HKCU" / "HKLM" */
void item_key_path(wchar_t *out, size_t n, ViewMode mode, const wchar_t *sub, const Item *it);

BOOL verb_exists(HKEY hive, const wchar_t *sub, const wchar_t *name);
LONG verb_save(HKEY hive, const wchar_t *sub, const wchar_t *name, const wchar_t *text,
               const wchar_t *cmd, const wchar_t *icon, const wchar_t *pos,
               BOOL shift, BOOL sepAbove, BOOL sepBelow, const Item *old);
/* Hide/show a command (LegacyDisable) or block/unblock a shell extension.
 * Returns ERROR_CANCELLED for an extension without a CLSID. */
LONG item_set_enabled(ViewMode mode, const wchar_t *sub, const Item *it, BOOL enable);
LONG key_delete(HKEY root, const wchar_t *path);

BOOL classic_menu_enabled(void);                        /* Windows 11 classic menu override */
LONG classic_menu_set(BOOL on);
void regedit_jump(HKEY hive, const wchar_t *path);

#endif
