TARGET   = resvgicon
TEMPLATE = lib
CONFIG += plugin

HEADERS += resvgiconengine.h
SOURCES += main.cpp \
           resvgiconengine.cpp
OTHER_FILES += resvgiconengine.json
OTHER_FILES += resvgiconengine-nocompress.json
QT += core-private gui-private
LIBS += -lresvg

target.path += $$[QT_INSTALL_PLUGINS]/iconengines
INSTALLS += target
