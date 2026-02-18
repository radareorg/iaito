# Agentic Coding Guidelines for the iaito project

## Overview

iaito is the official Qt-based GUI for radare2. It uses qmake for building and requires radare2 and Qt5/6.

## Locations

- Source code is in `./src/` (widgets, dialogs, menus, common, core, plugins)
- Qt project file: `./src/Iaito.pro`
- Plugin interface: `./src/plugins/IaitoPlugin.h`
- Sample plugins: `./src/plugins/sample-cpp/` and `./src/plugins/sample-python/`
- UI forms: `./src/dialogs/*.ui` and `./src/widgets/*.ui`
- Resources: `./src/resources.qrc`, `./src/themes/`

## Formatting Style

- Follow Qt Creator coding style (see `.clang-format`)
- Indent with **4 spaces** (no tabs)
- Column limit: 100 characters
- Braces on same line for control statements, new line for functions/classes
- Use `clang-format -i file.cpp` or `make indent`

## Coding Rules

- C++20 standard
- Use Qt signals/slots for communication between components
- Widgets inherit from `IaitoDockWidget` or `AddressableDockWidget`
- Access radare2 via the `Core()` singleton (defined in `core/Iaito.h`)
- Use `CutterCore::cmd()` and `CutterCore::cmdj()` for r2 commands

## Actions

- Configure: `./configure`
- Build: `make -j8 > /dev/null`
  - Never run qmake or compilers directly
  - Limit to 8 jobs to avoid resource exhaustion
  - Redirect stdout to /dev/null to reduce noise
- Run: `make run`
- Clean: `make clean`
- Install to home: `make user-install`
- Build r2 plugin: `make plugin`

## Dependencies

Requires radare2 installed system-wide:
```sh
git clone https://github.com/radareorg/radare2 && radare2/sys/install.sh
```

Qt5 packages (Debian/Ubuntu):
```sh
sudo apt install qttools5-dev qtbase5-dev libqt5svg5-dev
```

## Adding Widgets

1. Create `.h`, `.cpp`, and `.ui` files in `src/widgets/`
2. Add to `SOURCES`, `HEADERS`, `FORMS` in `src/Iaito.pro`
3. Register in `core/MainWindow.cpp`

## Adding Plugins

- C++ plugins: see `src/plugins/sample-cpp/`
- Python plugins: see `src/plugins/sample-python/`
- Implement `IaitoPlugin` interface from `plugins/IaitoPlugin.h`

## Debugging

- Run with: `R2_DEBUG=1 make run`
- Debug with gdb: `make gdb`
- Debug with lldb: `make lldb`
