/* Export a registry subtree as a Windows Registry Editor 5.00 (.reg) file. */
#ifndef REGFILE_H
#define REGFILE_H
#include "common.h"
BOOL regfile_write(const wchar_t *file, HKEY root, const wchar_t *path);
#endif
