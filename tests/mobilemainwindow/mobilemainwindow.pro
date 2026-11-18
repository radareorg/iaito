QT += core gui widgets network svg testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += svgwidgets
CONFIG += console testcase c++20 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_mobilemainwindow
DEFINES += IAITO_SOURCE_BUILD
PKGCONFIG += r_core
INCLUDEPATH += ../../build ../../src ../../src/core ../../src/common ../../src/widgets
SOURCES += test_mobilemainwindow.cpp
IAITO_BUILD = $$clean_path($$PWD/../../build)
IAITO_OBJECTS = $$files($$IAITO_BUILD/*.o)
IAITO_OBJECTS -= $$IAITO_BUILD/Main.o $$IAITO_BUILD/main.o
LIBS += $$IAITO_OBJECTS
PRE_TARGETDEPS += $$IAITO_OBJECTS
# Match conditionally compiled members of NewFileDialog.
IAITO_BUILD_SETTINGS = $$cat($$IAITO_BUILD/Makefile, lines)
contains(IAITO_BUILD_SETTINGS, ^DEFINES.*IAITO_ENABLE_DEBUGGER.*): DEFINES += IAITO_ENABLE_DEBUGGER
