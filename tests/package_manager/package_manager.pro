QT += core gui widgets testlib
CONFIG += console testcase c++20
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_package_manager
INCLUDEPATH += ../../src
SOURCES += test_package_manager.cpp ../../src/dialogs/PackageManagerDialog.cpp
HEADERS += ../../src/dialogs/PackageManagerDialog.h
