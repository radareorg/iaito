HEADERS        += ../IaitoSamplePlugin.h ../IaitoPlugin.h
INCLUDEPATH    += ../ ../../ ../../core ../../widgets
SOURCES        += IaitoSamplePlugin.cpp

QMAKE_CXXFLAGS += $$system("pkg-config --cflags r_core")

TEMPLATE        = lib
CONFIG         += plugin
QT             += widgets
TARGET          = PluginSample
