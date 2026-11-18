QT += core gui widgets testlib
CONFIG += console testcase c++20
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_exceptioninfo
INCLUDEPATH += ../../src
SOURCES += test_exceptioninfo.cpp ../../src/common/ExceptionInfo.cpp ../../src/widgets/ExceptionBar.cpp
HEADERS += ../../src/common/ExceptionInfo.h ../../src/widgets/ExceptionBar.h
