QT += core gui widgets testlib
CONFIG += console testcase c++20
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_columnheaders
INCLUDEPATH += ../../src
SOURCES += test_columnheaders.cpp ../../src/common/ColumnHeaderMenu.cpp
HEADERS += ../../src/common/ColumnHeaderMenu.h
