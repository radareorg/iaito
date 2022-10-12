TEMPLATE = app

TARGET = iaito

IAITO_VERSION_MAJOR = 5
IAITO_VERSION_MINOR = 7
IAITO_VERSION_PATCH = 6

CONFIG += sdk_no_version_check

unix:QMAKE_RPATHDIR += /usr/local/lib
unix:QMAKE_LFLAGS_RPATH=
unix:QMAKE_LFLAGS += "-Wl,-rpath,/usr/local/lib"

# build with thread-sanitizer
# unix:QMAKE_LFLAGS += "-fsanitize=thread"
# QMAKE_CXXFLAGS += -fsanitize=thread
QMAKE_CXXFLAGS += -g -O0
# QMAKE_CXXFLAGS += -O0 -fsanitize=address
# QMAKE_LFLAGS += -fsanitize=address


VERSION = $${IAITO_VERSION_MAJOR}.$${IAITO_VERSION_MINOR}.$${IAITO_VERSION_PATCH}

# Required QT version
lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5")
# Doesnt build for Qt6 yet... but will do soon

# Icon for OS X
ICON = img/iaito-o.icns

# Icon/resources for Windows
win32: RC_ICONS = img/iaito-o.ico

QT += core gui widgets svg network
QT_CONFIG -= no-pkg-config


!defined(IAITO_ENABLE_CRASH_REPORTS, var)      IAITO_ENABLE_CRASH_REPORTS=false
equals(IAITO_ENABLE_CRASH_REPORTS, true)       CONFIG += IAITO_ENABLE_CRASH_REPORTS

!defined(IAITO_ENABLE_PYTHON, var)             IAITO_ENABLE_PYTHON=false
equals(IAITO_ENABLE_PYTHON, true)              CONFIG += IAITO_ENABLE_PYTHON

!defined(IAITO_ENABLE_PYTHON_BINDINGS, var)    IAITO_ENABLE_PYTHON_BINDINGS=false
equals(IAITO_ENABLE_PYTHON, true) {
    equals(IAITO_ENABLE_PYTHON_BINDINGS, true) {
        CONFIG += IAITO_ENABLE_PYTHON_BINDINGS
    }
}

!defined(IAITO_BUNDLE_R2_APPBUNDLE, var)       IAITO_BUNDLE_R2_APPBUNDLE=false
equals(IAITO_BUNDLE_R2_APPBUNDLE, true)        CONFIG += IAITO_BUNDLE_R2_APPBUNDLE

!defined(IAITO_APPVEYOR_R2DEC, var)            IAITO_APPVEYOR_R2DEC=false
equals(IAITO_APPVEYOR_R2DEC, true)             CONFIG += IAITO_APPVEYOR_R2DEC

!defined(IAITO_R2GHIDRA_STATIC, var)           IAITO_R2GHIDRA_STATIC=false
equals(IAITO_R2GHIDRA_STATIC, true)            CONFIG += IAITO_R2GHIDRA_STATIC

DEFINES += IAITO_SOURCE_BUILD


# crash reports and python code has been removed and its unmaintained,
# so let's just disable the messages that are printed out
IAITO_ENABLE_CRASH_REPORTS {
    message("Crash report support enabled.")
    DEFINES += IAITO_ENABLE_CRASH_REPORTS
} else {
    # message("Crash report support disabled.")
}

IAITO_ENABLE_PYTHON {
    message("Python enabled.")
    DEFINES += IAITO_ENABLE_PYTHON
} else {
    # message("Python disabled.")
}

IAITO_ENABLE_PYTHON_BINDINGS {
    message("Python Bindings enabled.")
    DEFINES += IAITO_ENABLE_PYTHON_BINDINGS
} else {
    # message("Python Bindings disabled. (requires IAITO_ENABLE_PYTHON=true)")
}

win32:defined(IAITO_DEPS_DIR, var) {
    !defined(SHIBOKEN_EXECUTABLE, var)          SHIBOKEN_EXECUTABLE="$${IAITO_DEPS_DIR}/pyside/bin/shiboken2.exe"
    !defined(SHIBOKEN_INCLUDEDIR, var)          SHIBOKEN_INCLUDEDIR="$${IAITO_DEPS_DIR}/pyside/include/shiboken2"
    !defined(SHIBOKEN_LIBRARY, var)             SHIBOKEN_LIBRARY="$${IAITO_DEPS_DIR}/pyside/lib/shiboken2.abi3.lib"
    !defined(PYSIDE_INCLUDEDIR, var)            PYSIDE_INCLUDEDIR="$${IAITO_DEPS_DIR}/pyside/include/PySide2"
    !defined(PYSIDE_LIBRARY, var)               PYSIDE_LIBRARY="$${IAITO_DEPS_DIR}/pyside/lib/pyside2.abi3.lib"
    !defined(PYSIDE_TYPESYSTEMS, var)           PYSIDE_TYPESYSTEMS="$${IAITO_DEPS_DIR}/pyside/share/PySide2/typesystems"
}

INCLUDEPATH *= . core widgets dialogs common plugins menus

win32 {
    # Generate debug symbols in release mode
    QMAKE_CXXFLAGS_RELEASE += -Zi   # Compiler
    QMAKE_LFLAGS_RELEASE += /DEBUG  # Linker

    # Multithreaded compilation
    QMAKE_CXXFLAGS += -MP
}

macx {
    QMAKE_CXXFLAGS += -mmacosx-version-min=10.7 -std=c++17 -stdlib=libc++ -g
    QMAKE_TARGET_BUNDLE_PREFIX = org.radare
    QMAKE_BUNDLE = iaito
    QMAKE_INFO_PLIST = macos/Info.plist
}

unix:exists(/usr/local/include/libr)|bsd:exists(/usr/local/include/libr) {
    INCLUDEPATH += /usr/local/include/libr
    INCLUDEPATH += /usr/local/include/libr/sdb
}
unix:exists(/usr/include/libr) {
    INCLUDEPATH += /usr/include/libr
    INCLUDEPATH += /usr/include/libr/sdb
}
unix {
    QMAKE_LFLAGS += -rdynamic # Export dynamic symbols for plugins
}

# Libraries
include(lib_radare2.pri)

!win32 {
    CONFIG += link_pkgconfig
}

IAITO_ENABLE_PYTHON {
    win32 {
        PYTHON_EXECUTABLE = $$system("where python", lines)
        PYTHON_EXECUTABLE = $$first(PYTHON_EXECUTABLE)
        pythonpath = $$clean_path($$dirname(PYTHON_EXECUTABLE))
        LIBS += -L$${pythonpath}/libs -lpython3
        INCLUDEPATH += $${pythonpath}/include
    }

    unix|macx|bsd {
        defined(PYTHON_FRAMEWORK_DIR, var) {
            message("Using Python.framework at $$PYTHON_FRAMEWORK_DIR")
            INCLUDEPATH += $$PYTHON_FRAMEWORK_DIR/Python.framework/Headers
            LIBS += -F$$PYTHON_FRAMEWORK_DIR -framework Python
            DEFINES += MACOS_PYTHON_FRAMEWORK_BUNDLED
        } else {
            !packagesExist(python3) {
                error("ERROR: Python 3 could not be found. Make sure it is available to pkg-config.")
            }
            PKGCONFIG += python3
        }
    }

    IAITO_ENABLE_PYTHON_BINDINGS {
        isEmpty(SHIBOKEN_EXECUTABLE):!packagesExist(shiboken2) {
            error("ERROR: Shiboken2, which is required to build the Python Bindings, could not be found. Make sure it is available to pkg-config.")
        }
        isEmpty(PYSIDE_LIBRARY):!packagesExist(pyside2) {
            error("ERROR: PySide2, which is required to build the Python Bindings, could not be found. Make sure it is available to pkg-config.")
        }
        win32 {
            BINDINGS_SRC_LIST_CMD = "\"$${PYTHON_EXECUTABLE}\" bindings/src_list.py"
        } else {
            BINDINGS_SRC_LIST_CMD = "python3 bindings/src_list.py"
        }
        BINDINGS_SRC_DIR = "$${PWD}/bindings"
        BINDINGS_BUILD_DIR = "$${OUT_PWD}/bindings"
        BINDINGS_SOURCE_DIR = "$${BINDINGS_BUILD_DIR}/IaitoBindings"
        BINDINGS_SOURCE = $$system("$${BINDINGS_SRC_LIST_CMD} qmake \"$${BINDINGS_BUILD_DIR}\"")
        BINDINGS_INCLUDE_DIRS = "$$[QT_INSTALL_HEADERS]" \
                                "$$[QT_INSTALL_HEADERS]/QtCore" \
                                "$$[QT_INSTALL_HEADERS]/QtWidgets" \
                                "$$[QT_INSTALL_HEADERS]/QtGui"
        for (path, R2_INCLUDEPATH) {
           BINDINGS_INCLUDE_DIRS += "$$path"
        }
        for(path, INCLUDEPATH) {
            BINDINGS_INCLUDE_DIRS += $$absolute_path("$$path")
        }

        win32 {
            PATH_SEP = ";"
        } else {
            PATH_SEP = ":"
        }
        BINDINGS_INCLUDE_DIRS = $$join(BINDINGS_INCLUDE_DIRS, $$PATH_SEP)

        isEmpty(SHIBOKEN_EXECUTABLE) {
            SHIBOKEN_EXECUTABLE = $$system("pkg-config --variable=generator_location shiboken2")
        }

        isEmpty(PYSIDE_TYPESYSTEMS) {
            PYSIDE_TYPESYSTEMS = $$system("pkg-config --variable=typesystemdir pyside2")
        }
        isEmpty(PYSIDE_INCLUDEDIR) {
            PYSIDE_INCLUDEDIR = $$system("pkg-config --variable=includedir pyside2")
        }

        QMAKE_SUBSTITUTES += bindings/bindings.txt.in

        SHIBOKEN_OPTIONS = --project-file="$${BINDINGS_BUILD_DIR}/bindings.txt"
        defined(SHIBOKEN_EXTRA_OPTIONS, var) SHIBOKEN_OPTIONS += $${SHIBOKEN_EXTRA_OPTIONS}

        win32:SHIBOKEN_OPTIONS += --avoid-protected-hack
        bindings.target = bindings_target
        bindings.commands = $$quote($$system_path($${SHIBOKEN_EXECUTABLE})) $${SHIBOKEN_OPTIONS}
        QMAKE_EXTRA_TARGETS += bindings
        PRE_TARGETDEPS += bindings_target
        # GENERATED_SOURCES += $${BINDINGS_SOURCE} done by dummy targets bellow

        INCLUDEPATH += "$${BINDINGS_SOURCE_DIR}"

        win32:DEFINES += WIN32_LEAN_AND_MEAN

        !isEmpty(PYSIDE_LIBRARY) {
            LIBS += "$$SHIBOKEN_LIBRARY" "$$PYSIDE_LIBRARY"
            INCLUDEPATH += "$$SHIBOKEN_INCLUDEDIR"
        } else:macx {
            # Hack needed because with regular PKGCONFIG qmake will mess up everything
            QMAKE_CXXFLAGS += $$system("pkg-config --cflags shiboken2 pyside2")
            LIBS += $$system("pkg-config --libs shiboken2 pyside2")
        } else {
            PKGCONFIG += shiboken2 pyside2
        }
        INCLUDEPATH += "$$PYSIDE_INCLUDEDIR" "$$PYSIDE_INCLUDEDIR/QtCore" "$$PYSIDE_INCLUDEDIR/QtWidgets" "$$PYSIDE_INCLUDEDIR/QtGui"


        BINDINGS_DUMMY_INPUT_LIST = bindings/src_list.py

        # dummy rules to specify dependency between generated binding files and bindings_target
        bindings_h.input = BINDINGS_DUMMY_INPUT_LIST
        bindings_h.depends = bindings_target
        bindings_h.output = iaitobindings_python.h
        bindings_h.commands = "echo placeholder command ${QMAKE_FILE_OUT}"
        bindings_h.variable_out = HEADERS
        QMAKE_EXTRA_COMPILERS += bindings_h

        for(path, BINDINGS_SOURCE) {
            dummy_input = $$replace(path, .cpp, .txt)
            BINDINGS_DUMMY_INPUTS += $$dummy_input
            win32 {
                _ = $$system("mkdir \"$$dirname(dummy_input)\"; echo a >\"$$dummy_input\"")
            } else {
                _ = $$system("mkdir -p \"$$dirname(dummy_input)\"; echo a >\"$$dummy_input\"")
            }
        }

        bindings_cpp.input = BINDINGS_DUMMY_INPUTS
        bindings_cpp.depends = bindings_target
        bindings_cpp.output = "$${BINDINGS_SOURCE_DIR}/${QMAKE_FILE_IN_BASE}.cpp"
        bindings_cpp.commands = "echo placeholder command ${QMAKE_FILE_OUT}"
        bindings_cpp.variable_out = GENERATED_SOURCES
        QMAKE_EXTRA_COMPILERS += bindings_cpp
    }
}

IAITO_ENABLE_CRASH_REPORTS {
    defined(BREAKPAD_FRAMEWORK_DIR, var)|defined(BREAKPAD_SOURCE_DIR, var) {
        defined(BREAKPAD_FRAMEWORK_DIR, var) {
            INCLUDEPATH += $$BREAKPAD_FRAMEWORK_DIR/Breakpad.framework/Headers
            LIBS += -F$$BREAKPAD_FRAMEWORK_DIR -framework Breakpad
        }
        defined(BREAKPAD_SOURCE_DIR, var) {
            INCLUDEPATH += $$BREAKPAD_SOURCE_DIR
            win32 {
                LIBS += -L$$quote($$BREAKPAD_SOURCE_DIR\\client\\windows\\release\\lib) -lexception_handler -lcrash_report_sender -lcrash_generation_server -lcrash_generation_client -lcommon
            }
            unix:LIBS += -L$$BREAKPAD_SOURCE_DIR/client/linux -lbreakpad-client
            macos:error("Please use scripts\prepare_breakpad_macos.sh script to provide breakpad framework.")
        }
    } else {
        CONFIG += link_pkgconfig
        !packagesExist(breakpad-client) {
            error("ERROR: Breakpad could not be found. Make sure it is available to pkg-config.")
        }
        PKGCONFIG += breakpad-client
    }
}

macx:IAITO_BUNDLE_R2_APPBUNDLE {
    message("Using r2 rom AppBundle")
    DEFINES += MACOS_R2_BUNDLED
}

IAITO_APPVEYOR_R2DEC {
    message("Appveyor r2dec")
    DEFINES += IAITO_APPVEYOR_R2DEC
}

IAITO_R2GHIDRA_STATIC {
    message("Building with static r2ghidra support")
    DEFINES += IAITO_R2GHIDRA_STATIC
    SOURCES += $$R2GHIDRA_SOURCE/iaito-plugin/R2GhidraDecompiler.cpp
    HEADERS += $$R2GHIDRA_SOURCE/iaito-plugin/R2GhidraDecompiler.h
    INCLUDEPATH += $$R2GHIDRA_SOURCE/iaito-plugin
    LIBS += -L$$R2GHIDRA_INSTALL_PATH -lcore_ghidra -ldelayimp
    QMAKE_LFLAGS += /delayload:core_ghidra.dll
}

QMAKE_SUBSTITUTES += IaitoConfig.h.in

SOURCES += \
    Main.cpp \
    core/Iaito.cpp \
    dialogs/EditStringDialog.cpp \
    dialogs/WriteCommandsDialogs.cpp \
    widgets/DisassemblerGraphView.cpp \
    widgets/OverviewView.cpp \
    common/RichTextPainter.cpp \
    dialogs/InitialOptionsDialog.cpp \
    dialogs/AboutDialog.cpp \
    dialogs/CommentsDialog.cpp \
    dialogs/EditInstructionDialog.cpp \
    dialogs/FlagDialog.cpp \
    dialogs/RemoteDebugDialog.cpp \
    dialogs/NativeDebugDialog.cpp \
    dialogs/XrefsDialog.cpp \
    core/MainWindow.cpp \
    common/Helpers.cpp \
    common/Highlighter.cpp \
    common/MdHighlighter.cpp \
    common/DirectionalComboBox.cpp \
    dialogs/preferences/AsmOptionsWidget.cpp \
    dialogs/NewFileDialog.cpp \
    common/AnalTask.cpp \
    widgets/CommentsWidget.cpp \
    widgets/ConsoleWidget.cpp \
    widgets/Dashboard.cpp \
    widgets/EntrypointWidget.cpp \
    widgets/ExportsWidget.cpp \
    widgets/FlagsWidget.cpp \
    widgets/FunctionsWidget.cpp \
    widgets/ImportsWidget.cpp \
    widgets/Omnibar.cpp \
    widgets/RelocsWidget.cpp \
    widgets/SectionsWidget.cpp \
    widgets/SegmentsWidget.cpp \
    widgets/StringsWidget.cpp \
    widgets/SymbolsWidget.cpp \
    menus/DisassemblyContextMenu.cpp \
    menus/DecompilerContextMenu.cpp \
    widgets/DisassemblyWidget.cpp \
    widgets/HexdumpWidget.cpp \
    common/Configuration.cpp \
    common/Colors.cpp \
    dialogs/SaveProjectDialog.cpp \
    common/TempConfig.cpp \
    common/SvgIconEngine.cpp \
    common/SyntaxHighlighter.cpp \
    widgets/DecompilerWidget.cpp \
    widgets/VisualNavbar.cpp \
    widgets/GraphView.cpp \
    dialogs/preferences/PreferencesDialog.cpp \
    dialogs/preferences/AppearanceOptionsWidget.cpp \
    dialogs/preferences/GraphOptionsWidget.cpp \
    dialogs/preferences/PreferenceCategory.cpp \
    dialogs/preferences/InitializationFileEditor.cpp \
    widgets/QuickFilterView.cpp \
    widgets/ClassesWidget.cpp \
    widgets/ResourcesWidget.cpp \
    widgets/VTablesWidget.cpp \
    widgets/TypesWidget.cpp \
    widgets/HeadersWidget.cpp \
    widgets/SearchWidget.cpp \
    IaitoApplication.cpp \
    common/PythonAPI.cpp \
    dialogs/R2PluginsDialog.cpp \
    widgets/IaitoDockWidget.cpp \
    widgets/IaitoTreeWidget.cpp \
    widgets/GraphWidget.cpp \
    widgets/OverviewWidget.cpp \
    common/JsonTreeItem.cpp \
    common/JsonModel.cpp \
    dialogs/VersionInfoDialog.cpp \
    widgets/ZignaturesWidget.cpp \
    common/AsyncTask.cpp \
    dialogs/AsyncTaskDialog.cpp \
    widgets/StackWidget.cpp \
    widgets/RegistersWidget.cpp \
    widgets/ThreadsWidget.cpp \
    widgets/ProcessesWidget.cpp \
    widgets/BacktraceWidget.cpp \
    dialogs/MapFileDialog.cpp \
    common/CommandTask.cpp \
    common/ProgressIndicator.cpp \
    common/R2Task.cpp \
    dialogs/R2TaskDialog.cpp \
    widgets/DebugActions.cpp \
    widgets/MemoryMapWidget.cpp \
    dialogs/preferences/DebugOptionsWidget.cpp \
    dialogs/preferences/PluginsOptionsWidget.cpp \
    widgets/BreakpointWidget.cpp \
    dialogs/BreakpointsDialog.cpp \
    dialogs/AttachProcDialog.cpp \
    widgets/RegisterRefsWidget.cpp \
    dialogs/SetToDataDialog.cpp \
    dialogs/EditVariablesDialog.cpp \
    dialogs/EditFunctionDialog.cpp \
    widgets/IaitoTreeView.cpp \
    widgets/ComboQuickFilterView.cpp \
    dialogs/HexdumpRangeDialog.cpp \
    common/QtResImporter.cpp \
    common/IaitoSeekable.cpp \
    common/RefreshDeferrer.cpp \
    dialogs/WelcomeDialog.cpp \
    common/RunScriptTask.cpp \
    dialogs/EditMethodDialog.cpp \
    dialogs/TypesInteractionDialog.cpp \
    widgets/SdbWidget.cpp \
    common/PythonManager.cpp \
    plugins/PluginManager.cpp \
    common/BasicBlockHighlighter.cpp \
    common/BasicInstructionHighlighter.cpp \
    dialogs/LinkTypeDialog.cpp \
    widgets/ColorPicker.cpp \
    common/ColorThemeWorker.cpp \
    widgets/ColorThemeComboBox.cpp \
    widgets/ColorThemeListView.cpp \
    dialogs/preferences/ColorThemeEditDialog.cpp \
    common/UpdateWorker.cpp \
    widgets/MemoryDockWidget.cpp \
    common/CrashHandler.cpp \
    common/BugReporting.cpp \
    common/HighDpiPixmap.cpp \
    widgets/GraphGridLayout.cpp \
    widgets/HexWidget.cpp \
    common/SelectionHighlight.cpp \
    common/Decompiler.cpp \
    common/R2GhidraCmdDecompiler.cpp \
    common/R2pdcCmdDecompiler.cpp \
    common/R2retdecDecompiler.cpp \
    menus/AddressableItemContextMenu.cpp \
    common/AddressableItemModel.cpp \
    widgets/ListDockWidget.cpp \
    dialogs/MultitypeFileSaveDialog.cpp \
    widgets/BoolToggleDelegate.cpp \
    common/IOModesController.cpp \
    common/SettingsUpgrade.cpp \
    dialogs/LayoutManager.cpp \
    common/IaitoLayout.cpp \
    widgets/GraphHorizontalAdapter.cpp \
    common/ResourcePaths.cpp \
    widgets/IaitoGraphView.cpp \
    widgets/SimpleTextGraphView.cpp \
    widgets/R2GraphWidget.cpp \
    widgets/CallGraph.cpp \
    widgets/AddressableDockWidget.cpp \
    dialogs/preferences/AnalOptionsWidget.cpp \
    common/DecompilerHighlighter.cpp

GRAPHVIZ_SOURCES = \
    widgets/GraphvizLayout.cpp

HEADERS  += \
    core/Iaito.h \
    core/IaitoCommon.h \
    core/IaitoDescriptions.h \
    dialogs/EditStringDialog.h \
    dialogs/WriteCommandsDialogs.h \
    widgets/DisassemblerGraphView.h \
    widgets/OverviewView.h \
    common/RichTextPainter.h \
    common/CachedFontMetrics.h \
    dialogs/AboutDialog.h \
    dialogs/preferences/AsmOptionsWidget.h \
    dialogs/CommentsDialog.h \
    dialogs/EditInstructionDialog.h \
    dialogs/FlagDialog.h \
    dialogs/RemoteDebugDialog.h \
    dialogs/NativeDebugDialog.h \
    dialogs/XrefsDialog.h \
    common/Helpers.h \
    core/MainWindow.h \
    common/Highlighter.h \
    common/MdHighlighter.h \
    common/DirectionalComboBox.h \
    dialogs/InitialOptionsDialog.h \
    dialogs/NewFileDialog.h \
    common/AnalTask.h \
    widgets/CommentsWidget.h \
    widgets/ConsoleWidget.h \
    widgets/Dashboard.h \
    widgets/EntrypointWidget.h \
    widgets/ExportsWidget.h \
    widgets/FlagsWidget.h \
    widgets/FunctionsWidget.h \
    widgets/ImportsWidget.h \
    widgets/Omnibar.h \
    widgets/RelocsWidget.h \
    widgets/SectionsWidget.h \
    widgets/SegmentsWidget.h \
    widgets/StringsWidget.h \
    widgets/SymbolsWidget.h \
    menus/DisassemblyContextMenu.h \
    menus/DecompilerContextMenu.h \
    widgets/DisassemblyWidget.h \
    widgets/HexdumpWidget.h \
    common/Configuration.h \
    common/Colors.h \
    dialogs/SaveProjectDialog.h \
    common/TempConfig.h \
    common/SvgIconEngine.h \
    common/SyntaxHighlighter.h \
    widgets/DecompilerWidget.h \
    widgets/VisualNavbar.h \
    widgets/GraphView.h \
    dialogs/preferences/PreferencesDialog.h \
    dialogs/preferences/AppearanceOptionsWidget.h \
    dialogs/preferences/PreferenceCategory.h \
    dialogs/preferences/GraphOptionsWidget.h \
    dialogs/preferences/InitializationFileEditor.h \
    widgets/QuickFilterView.h \
    widgets/ClassesWidget.h \
    widgets/ResourcesWidget.h \
    IaitoApplication.h \
    widgets/VTablesWidget.h \
    widgets/TypesWidget.h \
    widgets/HeadersWidget.h \
    widgets/SearchWidget.h \
    common/PythonAPI.h \
    dialogs/R2PluginsDialog.h \
    widgets/IaitoDockWidget.h \
    widgets/IaitoTreeWidget.h \
    widgets/GraphWidget.h \
    widgets/OverviewWidget.h \
    common/JsonTreeItem.h \
    common/JsonModel.h \
    dialogs/VersionInfoDialog.h \
    widgets/ZignaturesWidget.h \
    common/AsyncTask.h \
    dialogs/AsyncTaskDialog.h \
    widgets/StackWidget.h \
    widgets/RegistersWidget.h \
    widgets/ThreadsWidget.h \
    widgets/ProcessesWidget.h \
    widgets/BacktraceWidget.h \
    dialogs/MapFileDialog.h \
    common/StringsTask.h \
    common/FunctionsTask.h \
    common/CommandTask.h \
    common/ProgressIndicator.h \
    plugins/IaitoPlugin.h \
    common/R2Task.h \
    dialogs/R2TaskDialog.h \
    widgets/DebugActions.h \
    widgets/MemoryMapWidget.h \
    dialogs/preferences/DebugOptionsWidget.h \
    dialogs/preferences/PluginsOptionsWidget.h \
    widgets/BreakpointWidget.h \
    dialogs/BreakpointsDialog.h \
    dialogs/AttachProcDialog.h \
    widgets/RegisterRefsWidget.h \
    dialogs/SetToDataDialog.h \
    common/InitialOptions.h \
    dialogs/EditVariablesDialog.h \
    dialogs/EditFunctionDialog.h \
    widgets/IaitoTreeView.h \
    widgets/ComboQuickFilterView.h \
    dialogs/HexdumpRangeDialog.h \
    common/QtResImporter.h \
    common/IaitoSeekable.h \
    common/RefreshDeferrer.h \
    dialogs/WelcomeDialog.h \
    common/RunScriptTask.h \
    common/Json.h \
    dialogs/EditMethodDialog.h \
    common/CrashHandler.h \
    dialogs/TypesInteractionDialog.h \
    widgets/SdbWidget.h \
    common/PythonManager.h \
    plugins/PluginManager.h \
    common/BasicBlockHighlighter.h \
    common/BasicInstructionHighlighter.h \
    common/UpdateWorker.h \
    widgets/ColorPicker.h \
    common/ColorThemeWorker.h \
    widgets/ColorThemeComboBox.h \
    widgets/MemoryDockWidget.h \
    widgets/ColorThemeListView.h \
    dialogs/preferences/ColorThemeEditDialog.h \
    dialogs/LinkTypeDialog.h \
    common/BugReporting.h \
    common/HighDpiPixmap.h \
    widgets/GraphLayout.h \
    widgets/GraphGridLayout.h \
    widgets/HexWidget.h \
    common/SelectionHighlight.h \
    common/Decompiler.h \
    common/R2GhidraCmdDecompiler.h \
    common/R2pdcCmdDecompiler.h \
    common/R2retdecDecompiler.h \
    menus/AddressableItemContextMenu.h \
    common/AddressableItemModel.h \
    widgets/ListDockWidget.h \
    widgets/AddressableItemList.h \
    dialogs/MultitypeFileSaveDialog.h \
    widgets/BoolToggleDelegate.h \
    common/IOModesController.h \
    common/SettingsUpgrade.h \
    dialogs/LayoutManager.h \
    common/IaitoLayout.h \
    common/BinaryTrees.h \
    common/LinkedListPool.h \
    widgets/GraphHorizontalAdapter.h \
    common/ResourcePaths.h \
    widgets/IaitoGraphView.h \
    widgets/SimpleTextGraphView.h \
    widgets/R2GraphWidget.h \
    widgets/CallGraph.h \
    widgets/AddressableDockWidget.h \
    dialogs/preferences/AnalOptionsWidget.h \
    common/DecompilerHighlighter.h

GRAPHVIZ_HEADERS = widgets/GraphvizLayout.h

FORMS    += \
    dialogs/AboutDialog.ui \
    dialogs/EditStringDialog.ui \
    dialogs/Base64EnDecodedWriteDialog.ui \
    dialogs/DuplicateFromOffsetDialog.ui \
    dialogs/IncrementDecrementDialog.ui \
    dialogs/preferences/AsmOptionsWidget.ui \
    dialogs/CommentsDialog.ui \
    dialogs/EditInstructionDialog.ui \
    dialogs/FlagDialog.ui \
    dialogs/RemoteDebugDialog.ui \
    dialogs/NativeDebugDialog.ui \
    dialogs/XrefsDialog.ui \
    dialogs/NewfileDialog.ui \
    dialogs/InitialOptionsDialog.ui \
    dialogs/EditFunctionDialog.ui \
    core/MainWindow.ui \
    widgets/ConsoleWidget.ui \
    widgets/Dashboard.ui \
    widgets/EntrypointWidget.ui \
    widgets/FlagsWidget.ui \
    widgets/StringsWidget.ui \
    widgets/HexdumpWidget.ui \
    dialogs/SaveProjectDialog.ui \
    dialogs/preferences/PreferencesDialog.ui \
    dialogs/preferences/AppearanceOptionsWidget.ui \
    dialogs/preferences/GraphOptionsWidget.ui \
    dialogs/preferences/InitializationFileEditor.ui \
    widgets/QuickFilterView.ui \
    widgets/DecompilerWidget.ui \
    widgets/ClassesWidget.ui \
    widgets/VTablesWidget.ui \
    widgets/TypesWidget.ui \
    widgets/SearchWidget.ui \
    dialogs/R2PluginsDialog.ui \
    dialogs/VersionInfoDialog.ui \
    widgets/ZignaturesWidget.ui \
    dialogs/AsyncTaskDialog.ui \
    dialogs/R2TaskDialog.ui \
    widgets/StackWidget.ui \
    widgets/RegistersWidget.ui \
    widgets/ThreadsWidget.ui \
    widgets/ProcessesWidget.ui \
    widgets/BacktraceWidget.ui \
    dialogs/MapFileDialog.ui \
    dialogs/preferences/DebugOptionsWidget.ui \
    widgets/BreakpointWidget.ui \
    dialogs/BreakpointsDialog.ui \
    dialogs/AttachProcDialog.ui \
    widgets/RegisterRefsWidget.ui \
    dialogs/SetToDataDialog.ui \
    dialogs/EditVariablesDialog.ui \
    widgets/IaitoTreeView.ui \
    widgets/ComboQuickFilterView.ui \
    dialogs/HexdumpRangeDialog.ui \
    dialogs/WelcomeDialog.ui \
    dialogs/EditMethodDialog.ui \
    dialogs/TypesInteractionDialog.ui \
    widgets/SdbWidget.ui \
    dialogs/LinkTypeDialog.ui \
    widgets/ColorPicker.ui \
    dialogs/preferences/ColorThemeEditDialog.ui \
    widgets/ListDockWidget.ui \
    dialogs/LayoutManager.ui \
    widgets/R2GraphWidget.ui \
    dialogs/preferences/AnalOptionsWidget.ui

RESOURCES += \
    resources.qrc \
    themes/native/native.qrc \
    themes/qdarkstyle/dark.qrc \
    themes/midnight/midnight.qrc \
    themes/lightstyle/light.qrc


DISTFILES += Iaito.astylerc

# 'make install' for AppImage
unix {
    isEmpty(PREFIX) {
        PREFIX = /usr/local
    }

    icon_file = img/org.radare.iaito.svg

    share_icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
    share_icon.files = $$icon_file


    desktop_file = org.radare.iaito.desktop

    share_applications.path = $$PREFIX/share/applications
    share_applications.files = $$desktop_file

    appstream_file = org.radare.iaito.appdata.xml

    # Used by ???
    share_appdata.path = $$PREFIX/share/appdata
    share_appdata.files = $$appstream_file

    # Used by AppImageHub (See https://www.freedesktop.org/software/appstream)
    share_appdata.path = $$PREFIX/share/metainfo
    share_appdata.files = $$appstream_file

    # built-in no need for files atm
    target.path = $$PREFIX/bin

    INSTALLS += target share_appdata share_applications share_icon

    # Triggered for example by 'qmake APPIMAGE=1'
    !isEmpty(APPIMAGE){
        appimage_root.path = /
        appimage_root.files = $$icon_file $$desktop_file

        INSTALLS += appimage_root
        DEFINES += APPIMAGE
    }
}
