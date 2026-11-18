TEMPLATE = app
TARGET = test_android
CONFIG += console testcase c++20
CONFIG -= app_bundle
QT = core testlib
INCLUDEPATH += ../../src
SOURCES += test_android.cpp ../../src/common/Android.cpp ../../src/common/FilePaths.cpp
HEADERS += ../../src/common/Android.h ../../src/common/FilePaths.h
