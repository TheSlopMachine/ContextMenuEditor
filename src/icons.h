/* Small (16px-ish) icon lookup for menu entries. */
#ifndef ICONS_H
#define ICONS_H
#include "common.h"

/* Resolve the icon Explorer would show: the "Icon" value ("path[,index]"), else the
 * icon of the command's executable, else of `module`. Returns NULL if none is found.
 * The caller owns the HICON (DestroyIcon). Any argument may be NULL or empty. */
HICON icon_load(const wchar_t *iconSpec, const wchar_t *command, const wchar_t *module);

/* A generic application icon (a fresh copy; caller owns it). */
HICON icon_generic(void);

#endif
