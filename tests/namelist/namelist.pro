QT += core gui widgets testlib
CONFIG += console testcase c++20 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test_namelist
DEFINES += IAITO_SOURCE_BUILD
PKGCONFIG += r_core
INCLUDEPATH += ../../src
SOURCES += test_namelist.cpp \
           ../../src/common/AddressableItemModel.cpp \
           ../../src/common/NameListModel.cpp \
           ../../src/widgets/NameListView.cpp \
           ../../src/widgets/ComboQuickFilterView.cpp \
           ../../src/widgets/QuickFilterView.cpp
HEADERS += ../../src/common/AddressableItemModel.h \
           ../../src/common/NameListModel.h \
           ../../src/widgets/NameListView.h \
           ../../src/widgets/ComboQuickFilterView.h \
           ../../src/widgets/QuickFilterView.h
FORMS += ../../src/widgets/QuickFilterView.ui
RESOURCES += namelist.qrc
