TEMPLATE = app

TARGET = iaito

IAITO_VERSION_MAJOR = 6
IAITO_VERSION_MINOR = 1
IAITO_VERSION_PATCH = 4

CONFIG += c++20
QMAKE_CXXFLAGS += -std=c++20
QMAKE_CXXFLAGS += -include $$absolute_path(common/CompilerWorkarounds.h, $$PWD)

CONFIG += app_bundle
# LIBS+= -dead_strip

CONFIG += sdk_no_version_check

!defined(IAITO_BUNDLE_R2_APPBUNDLE, var)       IAITO_BUNDLE_R2_APPBUNDLE=false
equals(IAITO_BUNDLE_R2_APPBUNDLE, true)        CONFIG += IAITO_BUNDLE_R2_APPBUNDLE

unix:!IAITO_BUNDLE_R2_APPBUNDLE {
    isEmpty(R2_LIBDIR) {
        R2_LIBDIR = $$system(r2 -H R2_LIBDIR)
    }
    QMAKE_RPATHDIR += $${R2_LIBDIR}
    QMAKE_LFLAGS_RPATH=
    QMAKE_LFLAGS += "-Wl,-rpath,$${R2_LIBDIR}"
}

QMAKE_CXXFLAGS += $$(CXXFLAGS)
QMAKE_CFLAGS += $$(CFLAGS)
QMAKE_LFLAGS += $$(LDFLAGS)

# build with thread-sanitizer
# unix:QMAKE_LFLAGS += "-fsanitize=thread"
# QMAKE_CXXFLAGS += -fsanitize=thread
QMAKE_CXXFLAGS += -g -O0
#QMAKE_CXXFLAGS += -O0 -fsanitize=address
#QMAKE_LFLAGS += -fsanitize=address

VERSION = $${IAITO_VERSION_MAJOR}.$${IAITO_VERSION_MINOR}.$${IAITO_VERSION_PATCH}

# Required QT version
lessThan(QT_MAJOR_VERSION, 5): error("requires at least Qt 5")

# Icon for OS X
ICON = img/iaito.icns

# Icon/resources for Windows
win32: RC_ICONS = img/iaito.ico

QT += widgets svg network
QT_CONFIG -= no-pkg-config

greaterThan(QT_MAJOR_VERSION, 5): QT += svgwidgets

!defined(IAITO_ENABLE_CRASH_REPORTS, var)      IAITO_ENABLE_CRASH_REPORTS=false
equals(IAITO_ENABLE_CRASH_REPORTS, true)       CONFIG += IAITO_ENABLE_CRASH_REPORTS

!defined(IAITO_ENABLE_DEBUGGER, var)           IAITO_ENABLE_DEBUGGER=true
equals(IAITO_ENABLE_DEBUGGER, true)            CONFIG += IAITO_ENABLE_DEBUGGER

!defined(IAITO_APPVEYOR_R2DEC, var)            IAITO_APPVEYOR_R2DEC=false
equals(IAITO_APPVEYOR_R2DEC, true)             CONFIG += IAITO_APPVEYOR_R2DEC

!defined(IAITO_R2GHIDRA_STATIC, var)           IAITO_R2GHIDRA_STATIC=false
equals(IAITO_R2GHIDRA_STATIC, true)            CONFIG += IAITO_R2GHIDRA_STATIC

DEFINES += IAITO_SOURCE_BUILD

IAITO_ENABLE_CRASH_REPORTS {
    message("Crash report support enabled.")
    DEFINES += IAITO_ENABLE_CRASH_REPORTS
} else {
    # message("Crash report support disabled.")
}

IAITO_ENABLE_DEBUGGER {
    message("Native debugger support enabled.")
    DEFINES += IAITO_ENABLE_DEBUGGER
} else {
    message("Native debugger support disabled.")
}

INCLUDEPATH *= . core widgets dialogs common plugins menus

win32 {
    # Generate debug symbols in release mode
# QMAKE_CXXFLAGS_RELEASE += -Zi   # Compiler
# QMAKE_LFLAGS_RELEASE += /DEBUG  # Linker

    # Multithreaded compilation
    QMAKE_CXXFLAGS += -MP
}

macx {
    QMAKE_CXXFLAGS += -mmacosx-version-min=10.7 -std=c++20 -stdlib=libc++ -g
    QMAKE_TARGET_BUNDLE_PREFIX = org.radare
    QMAKE_BUNDLE = iaito
    QMAKE_INFO_PLIST = macos/Info.plist
}

unix {
    isEmpty(R2_INCDIR) {
        R2_INCDIR = $$system(r2 -H R2_INCDIR)
    }
    INCLUDEPATH += $${R2_INCDIR}
    INCLUDEPATH += $${R2_INCDIR}/sdb
}
unix {
    QMAKE_LFLAGS += -rdynamic # Export dynamic symbols for plugins
}

# Libraries
include(lib_radare2.pri)

!win32 {
    CONFIG += link_pkgconfig
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
    dialogs/DumpDialog.cpp \
    dialogs/WriteCommandsDialogs.cpp \
    widgets/DisassemblerGraphView.cpp \
    widgets/OverviewView.cpp \
    common/RichTextPainter.cpp \
    dialogs/InitialOptionsDialog.cpp \
    dialogs/AboutDialog.cpp \
    dialogs/AddressSpaceManagerDialog.cpp \
    dialogs/CommentsDialog.cpp \
    dialogs/EditInstructionDialog.cpp \
    dialogs/FlagDialog.cpp \
    dialogs/RemoteDebugDialog.cpp \
    dialogs/NativeDebugDialog.cpp \
    dialogs/XrefsDialog.cpp \
    core/MainWindow.cpp \
    common/Helpers.cpp \
    common/TextEditDialog.cpp \
    common/Highlighter.cpp \
    common/MdHighlighter.cpp \
    common/DirectionalComboBox.cpp \
    common/TypeScriptHighlighter.cpp \
    common/AnalTask.cpp \
    dialogs/settings/AsmOptionsWidget.cpp \
    dialogs/FortuneDialog.cpp \
    dialogs/NewFileDialog.cpp \
    widgets/CommentsWidget.cpp \
    widgets/ConsoleWidget.cpp \
    widgets/CustomCommandWidget.cpp \
    widgets/Dashboard.cpp \
    widgets/EntrypointWidget.cpp \
    widgets/ExportsWidget.cpp \
    widgets/FlagsWidget.cpp \
    widgets/FunctionsWidget.cpp \
    widgets/ImportsWidget.cpp \
    widgets/CodeEditor.cpp \
    widgets/Omnibar.cpp \
    widgets/RelocsWidget.cpp \
    widgets/SectionsWidget.cpp \
    widgets/SegmentsWidget.cpp \
    widgets/StringsWidget.cpp \
    widgets/SymbolsWidget.cpp \
    widgets/XrefsWidget.cpp \
    widgets/RefsWidget.cpp \
    menus/DisassemblyContextMenu.cpp \
    menus/DecompilerContextMenu.cpp \
    menus/ColorPickerMenu.cpp \
    menus/PinPickerMenu.cpp \
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
    dialogs/settings/SettingsDialog.cpp \
    dialogs/settings/AppearanceOptionsWidget.cpp \
    dialogs/settings/GraphOptionsWidget.cpp \
    dialogs/settings/SettingCategory.cpp \
    dialogs/settings/InitializationFileEditor.cpp \
    widgets/QuickFilterView.cpp \
    widgets/ClassesWidget.cpp \
    widgets/ResourcesWidget.cpp \
    widgets/VTablesWidget.cpp \
    widgets/TypesWidget.cpp \
    widgets/HeadersWidget.cpp \
    widgets/SearchWidget.cpp \
    IaitoApplication.cpp \
    dialogs/R2PluginsDialog.cpp \
    widgets/IaitoDockWidget.cpp \
    widgets/IaitoTreeWidget.cpp \
    widgets/MapsWidget.cpp \
    widgets/BinariesWidget.cpp \
    widgets/FilesWidget.cpp \
    widgets/FilesystemWidget.cpp \
    widgets/GraphWidget.cpp \
    widgets/OverviewWidget.cpp \
    common/JsonTreeItem.cpp \
    common/JsonModel.cpp \
    dialogs/VersionInfoDialog.cpp \
    widgets/ZignaturesWidget.cpp \
    common/AsyncTask.cpp \
    common/DecompileTask.cpp \
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
    dialogs/settings/DebugOptionsWidget.cpp \
    dialogs/settings/WebserverOptionsWidget.cpp \
    dialogs/settings/PluginsOptionsWidget.cpp \
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
    common/IaitoSeekable.cpp \
    common/RefreshDeferrer.cpp \
    dialogs/WelcomeDialog.cpp \
    common/RunScriptTask.cpp \
    dialogs/EditMethodDialog.cpp \
    dialogs/TypesInteractionDialog.cpp \
    widgets/SdbWidget.cpp \
    plugins/PluginManager.cpp \
    common/BasicBlockHighlighter.cpp \
    common/BasicInstructionHighlighter.cpp \
    dialogs/LinkTypeDialog.cpp \
    widgets/ColorPicker.cpp \
    common/ColorThemeWorker.cpp \
    widgets/ColorThemeComboBox.cpp \
    widgets/ColorThemeListView.cpp \
    dialogs/settings/ColorThemeEditDialog.cpp \
    widgets/MemoryDockWidget.cpp \
    common/CrashHandler.cpp \
    common/BugReporting.cpp \
    common/HighDpiPixmap.cpp \
    widgets/GraphGridLayout.cpp \
    widgets/HexWidget.cpp \
    widgets/R2AIWidget.cpp \
    common/SelectionHighlight.cpp \
    common/Decompiler.cpp \
    common/R2GhidraCmdDecompiler.cpp \
    common/ShortcutKeys.cpp \
    dialogs/ShortcutKeysDialog.cpp \
    common/R2pdcCmdDecompiler.cpp \
    common/R2DecaiDecompiler.cpp \
    common/R2AiDecompiler.cpp \
    common/R2AnotesDecompiler.cpp \
    common/R2HermesDecompiler.cpp \
    common/R2retdecDecompiler.cpp \
    menus/AddressableItemContextMenu.cpp \
    common/AddressableItemModel.cpp \
    widgets/ListDockWidget.cpp \
    dialogs/MultitypeFileSaveDialog.cpp \
    widgets/BoolToggleDelegate.cpp \
    common/IOModesController.cpp \
    dialogs/LayoutManager.cpp \
    common/IaitoLayout.cpp \
    widgets/GraphHorizontalAdapter.cpp \
    common/ResourcePaths.cpp \
    widgets/IaitoGraphView.cpp \
    widgets/SimpleTextGraphView.cpp \
    widgets/R2GraphWidget.cpp \
    widgets/CallGraph.cpp \
    widgets/CalculatorWidget.cpp \
    widgets/SyscallsWidget.cpp \
    widgets/AddressableDockWidget.cpp \
    dialogs/settings/AnalOptionsWidget.cpp \
    dialogs/settings/KeyboardOptionsWidget.cpp \
    common/DecompilerHighlighter.cpp \
    dialogs/PackageManagerDialog.cpp \
    dialogs/AssemblerDialog.cpp \
    dialogs/ScriptManagerDialog.cpp \
    dialogs/ScriptManagerWidget.cpp \
    widgets/ZoomWidget.cpp

GRAPHVIZ_SOURCES = \
    widgets/GraphvizLayout.cpp

HEADERS  += \
    common/R2Shims.h \
    core/Iaito.h \
    core/IaitoCommon.h \
    core/IaitoDescriptions.h \
    dialogs/EditStringDialog.h \
    dialogs/DumpDialog.h \
    dialogs/WriteCommandsDialogs.h \
    widgets/ClickableSvgWidget.h \
    widgets/DisassemblerGraphView.h \
    widgets/OverviewView.h \
    common/RichTextPainter.h \
    common/CachedFontMetrics.h \
    common/TypeScriptHighlighter.h \
    common/Helpers.h \
    common/ShortcutKeys.h \
    dialogs/ShortcutKeysDialog.h \
    common/TextEditDialog.h \
    common/Highlighter.h \
    common/MdHighlighter.h \
    common/DirectionalComboBox.h \
    common/AnalTask.h \
    dialogs/AboutDialog.h \
    dialogs/AddressSpaceManagerDialog.h \
    dialogs/settings/AsmOptionsWidget.h \
    dialogs/CommentsDialog.h \
    dialogs/EditInstructionDialog.h \
    dialogs/FlagDialog.h \
    dialogs/RemoteDebugDialog.h \
    dialogs/NativeDebugDialog.h \
    dialogs/XrefsDialog.h \
    core/MainWindow.h \
    dialogs/InitialOptionsDialog.h \
    dialogs/NewFileDialog.h \
    widgets/CommentsWidget.h \
    widgets/ConsoleWidget.h \
    widgets/CustomCommandWidget.h \
    widgets/Dashboard.h \
    widgets/EntrypointWidget.h \
    widgets/ExportsWidget.h \
    widgets/FlagsWidget.h \
    widgets/FunctionsWidget.h \
    widgets/ImportsWidget.h \
    widgets/Omnibar.h \
    widgets/RelocsWidget.h \
    widgets/CodeEditor.h \
    widgets/SectionsWidget.h \
    widgets/SegmentsWidget.h \
    widgets/StringsWidget.h \
    widgets/SymbolsWidget.h \
    widgets/XrefsWidget.h \
    widgets/RefsWidget.h \
    menus/DisassemblyContextMenu.h \
    menus/DecompilerContextMenu.h \
    menus/ColorPickerMenu.h \
    menus/PinPickerMenu.h \
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
    dialogs/settings/SettingsDialog.h \
    dialogs/settings/AppearanceOptionsWidget.h \
    dialogs/settings/SettingCategory.h \
    dialogs/settings/GraphOptionsWidget.h \
    dialogs/settings/InitializationFileEditor.h \
    widgets/QuickFilterView.h \
    widgets/ClassesWidget.h \
    widgets/ResourcesWidget.h \
    IaitoApplication.h \
    widgets/VTablesWidget.h \
    widgets/TypesWidget.h \
    widgets/HeadersWidget.h \
    widgets/SearchWidget.h \
    dialogs/R2PluginsDialog.h \
    widgets/IaitoDockWidget.h \
    widgets/IaitoTreeWidget.h \
    widgets/MapsWidget.h \
    widgets/BinariesWidget.h \
    widgets/FilesWidget.h \
    widgets/FilesystemWidget.h \
    widgets/GraphWidget.h \
    widgets/OverviewWidget.h \
    common/JsonTreeItem.h \
    common/JsonModel.h \
    dialogs/VersionInfoDialog.h \
    widgets/ZignaturesWidget.h \
    common/AsyncTask.h \
    common/DecompileTask.h \
    dialogs/AsyncTaskDialog.h \
    widgets/StackWidget.h \
    widgets/RegistersWidget.h \
    widgets/ThreadsWidget.h \
    widgets/ProcessesWidget.h \
    widgets/BacktraceWidget.h \
    dialogs/FortuneDialog.h \
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
    dialogs/settings/DebugOptionsWidget.h \
    dialogs/settings/WebserverOptionsWidget.h \
    dialogs/settings/PluginsOptionsWidget.h \
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
    common/IaitoSeekable.h \
    common/RefreshDeferrer.h \
    dialogs/WelcomeDialog.h \
    common/RunScriptTask.h \
    common/Json.h \
    dialogs/EditMethodDialog.h \
    common/CrashHandler.h \
    dialogs/TypesInteractionDialog.h \
    widgets/SdbWidget.h \
    plugins/PluginManager.h \
    common/BasicBlockHighlighter.h \
    common/BasicInstructionHighlighter.h \
    widgets/ColorPicker.h \
    common/ColorThemeWorker.h \
    widgets/ColorThemeComboBox.h \
    widgets/MemoryDockWidget.h \
    widgets/ColorThemeListView.h \
    dialogs/settings/ColorThemeEditDialog.h \
    dialogs/settings/KeyboardOptionsWidget.h \
    dialogs/LinkTypeDialog.h \
    common/BugReporting.h \
    common/HighDpiPixmap.h \
    widgets/GraphLayout.h \
    widgets/GraphGridLayout.h \
    widgets/HexWidget.h \
    widgets/R2AIWidget.h \
    common/SelectionHighlight.h \
    common/Decompiler.h \
    common/R2GhidraCmdDecompiler.h \
    common/R2DecaiDecompiler.h \
    common/R2AiDecompiler.h \
    common/R2AnotesDecompiler.h \
    common/R2HermesDecompiler.h \
    common/R2pdcCmdDecompiler.h \
    common/R2retdecDecompiler.h \
    menus/AddressableItemContextMenu.h \
    common/AddressableItemModel.h \
    widgets/ListDockWidget.h \
    widgets/AddressableItemList.h \
    dialogs/MultitypeFileSaveDialog.h \
    widgets/BoolToggleDelegate.h \
    common/IOModesController.h \
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
    widgets/CalculatorWidget.h \
    widgets/SyscallsWidget.h \
    widgets/AddressableDockWidget.h \
    dialogs/settings/AnalOptionsWidget.h \
    common/DecompilerHighlighter.h \
    common/CodeMetaRange.h \
    dialogs/PackageManagerDialog.h \
    dialogs/AssemblerDialog.h \
    dialogs/ScriptManagerDialog.h \
    dialogs/ScriptManagerWidget.h \
    widgets/ZoomWidget.h

GRAPHVIZ_HEADERS = widgets/GraphvizLayout.h

FORMS    += \
    dialogs/AboutDialog.ui \
    dialogs/EditStringDialog.ui \
    dialogs/Base64EnDecodedWriteDialog.ui \
    dialogs/DumpDialog.ui \
    dialogs/DuplicateFromOffsetDialog.ui \
    dialogs/IncrementDecrementDialog.ui \
    dialogs/settings/AsmOptionsWidget.ui \
    dialogs/CommentsDialog.ui \
    dialogs/EditInstructionDialog.ui \
    dialogs/FlagDialog.ui \
    dialogs/RemoteDebugDialog.ui \
    dialogs/NativeDebugDialog.ui \
    dialogs/XrefsDialog.ui \
    dialogs/NewFileDialog.ui \
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
    dialogs/settings/SettingsDialog.ui \
    dialogs/settings/AppearanceOptionsWidget.ui \
    dialogs/settings/GraphOptionsWidget.ui \
    dialogs/settings/InitializationFileEditor.ui \
    dialogs/settings/KeyboardOptionsWidget.ui \
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
    dialogs/settings/DebugOptionsWidget.ui \
    dialogs/settings/WebserverOptionsWidget.ui \
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
    dialogs/settings/ColorThemeEditDialog.ui \
    widgets/ListDockWidget.ui \
    dialogs/LayoutManager.ui \
    widgets/R2GraphWidget.ui \
    widgets/CustomCommandWidget.ui \
    dialogs/settings/AnalOptionsWidget.ui

RESOURCES += \
    resources.qrc \
    themes/native/native.qrc \
    themes/qdarkstyle/dark.qrc \
    themes/midnight/midnight.qrc \
    themes/lightstyle/light.qrc \
    themes/classic/classic.qrc


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
