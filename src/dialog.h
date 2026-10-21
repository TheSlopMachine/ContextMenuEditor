/* Modal Add/Edit dialog for a shell command entry. */
#ifndef DIALOG_H
#define DIALOG_H
#include "common.h"
#include "registry.h"

BOOL dialog_init(HINSTANCE inst);                                     /* register window class */
/* orig == NULL adds a new entry under `sub`; otherwise edits it. TRUE if something was saved. */
BOOL verb_dialog(HWND parent, const Item *orig, const wchar_t *sub);

#endif
