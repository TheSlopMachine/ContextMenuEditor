# Context Menu Editor

A small native Windows tool (pure C, Win32 API, no dependencies) for editing Explorer's right-click menu.

## Features

- Browse, add, edit, hide/show and delete context menu commands for files, folders, folder background,
  desktop, drives, This PC, Recycle Bin, unknown types, or any file extension
- Icons shown next to every entry, with a live icon preview in the editor
- Per-user (no admin) and system-wide (admin) entries listed together
- Templates: Notepad, Command Prompt / PowerShell / Windows Terminal here
- Icon, position (top/bottom), Shift-only, separators; key names are generated for you
- Shell extension viewer: see third-party handlers and block/unblock them
- Export to `.reg`, import `.reg`, automatic `.reg` backup before every delete
- Windows 11 classic-menu toggle, Restart Explorer, relaunch as administrator
- Sortable columns, filter box, right-click menu, keyboard shortcuts, remembers window and last location

| Shortcut | Action | Shortcut | Action |
|---|---|---|---|
| Ctrl+N | Add command | F5 | Refresh |
| F2 / double-click | Edit | Ctrl+F | Filter |
| Del | Delete | Ctrl+1 / Ctrl+2 | Commands / Extensions |
| Ctrl+E | Export `.reg` | | |

## Build

Requires MinGW-w64 `gcc`, `windres` and GNU `make` (`mingw32-make` on some installs).

```
make build            # -> build/ContextMenuEditor.exe
make build DEBUG=1    # unoptimised, with symbols
make clean
make build CROSS=x86_64-w64-mingw32-   # cross-compile from Linux/macOS
```

## Project layout

```
Makefile
src/
  main.c       main window, menus, list, actions
  dialog.c     add/edit dialog
  registry.c   where entries live; load/save/delete/enable
  regfile.c    .reg export
  icons.c      icon lookup for list and preview
  util.c       shared helpers
  *.h          module interfaces (common.h is included everywhere)
res/           icon, manifest (visual styles, DPI), version info
tools/         make_icon.py regenerates res/app.ico (stdlib only)
```

## Where data is stored

- Commands: `HKCU` or `HKLM` `\Software\Classes\<location>\shell\<key>`
- Shell extensions: `...\<location>\shellex\ContextMenuHandlers`; blocking uses the per-user
  `Shell Extensions\Blocked` list
- Settings: `HKCU\Software\ContextMenuEditor`
- Backups: `%APPDATA%\ContextMenuEditor\Backups`

Editing the registry can affect how Explorer behaves. Use at your own risk; deletes are backed up.
