TARGET = mediaplayer-binding

HEADERS = mediaplayer-manager.h \
      gdbus/lightmediascanner_interface.h \
      gdbus/udisks_interface.h

SOURCES = mediaplayer-api.c \
      mediaplayer-manager.c \
      gdbus/lightmediascanner_interface.c \
      gdbus/udisks_interface.c

LIBS += -Wl,--version-script=$$PWD/export.map

CONFIG += link_pkgconfig
INCLUDEPATH += $$PWD/gdbus
PKGCONFIG += json-c afb-daemon sqlite3 glib-2.0 gio-2.0 gio-unix-2.0 zlib

include(binding.pri)
