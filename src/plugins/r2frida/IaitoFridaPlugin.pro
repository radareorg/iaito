TEMPLATE = lib
CONFIG += plugin
CONFIG -= import_plugins
QT += widgets svg

TARGET = IaitoFridaPlugin
DESTDIR = $$PWD/../../../build/plugins/native

QMAKE_CXXFLAGS += $$system(pkg-config --cflags r_core)
QMAKE_CXXFLAGS += -std=c++20
INCLUDEPATH += \
    $$PWD \
    $$PWD/.. \
    $$PWD/../.. \
    $$PWD/../../common \
    $$PWD/../../core \
    $$PWD/../../widgets \
    $$PWD/../../dialogs \
    $$PWD/../../menus \
    $$PWD/../../plugins \
    $$system(r2 -H R2_INCDIR)

macx: QMAKE_LFLAGS += -undefined dynamic_lookup
unix:!macx: QMAKE_LFLAGS += -Wl,--unresolved-symbols=ignore-all

HEADERS += \
    FridaTypes.h \
    FridaUri.h \
    FridaBackend.h \
    FridaSession.h \
    FridaConnectDialog.h \
    FridaWidget.h \
    FridaPanels.h \
    IaitoFridaPlugin.h \
    ../IaitoPlugin.h

SOURCES += \
    FridaUri.cpp \
    FridaBackend.cpp \
    FridaSession.cpp \
    FridaConnectDialog.cpp \
    FridaWidget.cpp \
    FridaPanels.cpp \
    IaitoFridaPlugin.cpp
