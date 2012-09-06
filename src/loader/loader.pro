include(../make/config.inc)

TARGET             = $$VACUUM_LOADER_NAME
TEMPLATE           = app
QT                += xml
LIBS              += -L../libs
LIBS              += -l$$VACUUM_UTILS_NAME
DEPENDPATH        += ..
INCLUDEPATH       += ..
DESTDIR            = ../..
include(loader.pri)

#Appication icon
win32:RC_FILE      = loader.rc
macx:ICON          = ../../vacuum.icns

#MacOS Info.plist
macx:QMAKE_INFO_PLIST = ../../src/packages/macosx/Info.plist

#SVN Info
isEmpty(SVN_REVISION) {
  SVN_REVISION=$$system(svnversion -n -c ./../../)
}
win32 {
  exists(svninfo.h):system(del svninfo.h)
  !isEmpty(SVN_REVISION):system(echo $${LITERAL_HASH}define SVN_REVISION \"$$SVN_REVISION\" >> svninfo.h) {
    DEFINES         += SVNINFO
    QMAKE_DISTCLEAN += svninfo.h
  }
} else {
  exists(svninfo.h):system(rm -f svninfo.h)
  !isEmpty(SVN_REVISION):system(echo \\$${LITERAL_HASH}define SVN_REVISION \\\"$${SVN_REVISION}\\\" >> svninfo.h) {
    DEFINES         += SVNINFO
    QMAKE_DISTCLEAN += svninfo.h
  }
}

#Install
target.path        = $$INSTALL_BINS
resources.path     = $$INSTALL_RESOURCES
resources.files    = ../../resources/*
documents.path     = $$INSTALL_DOCUMENTS
documents.files    = ../../AUTHORS ../../CHANGELOG ../../README ../../COPYING ../../TRANSLATORS
INSTALLS           += target resources documents

#Translation
TRANS_SOURCE_ROOT  = ..
include(../translations/languages.inc)

#Linux desktop install
unix:!macx {
  icons.path       = $$INSTALL_PREFIX/$$INSTALL_RES_DIR/pixmaps
  icons.files      = ../../resources/menuicons/shared/vacuum.png
  INSTALLS        += icons

  desktop.path     = $$INSTALL_PREFIX/$$INSTALL_RES_DIR/applications
  desktop.files    = ../../src/packages/linux/*.desktop
  INSTALLS        += desktop
}

#MacOS Install
macx {
  UTILS_LIB_NAME   = lib$${VACUUM_UTILS_NAME}.$${VACUUM_UTILS_ABI}.dylib
  UTILS_LIB_LINK   = lib$${VACUUM_UTILS_NAME}.dylib

  lib_utils.path   = $$INSTALL_LIBS
  lib_utils.extra  = cp -f ../libs/$$UTILS_LIB_NAME $(INSTALL_ROOT)$$INSTALL_LIBS/$$UTILS_LIB_NAME && \
                     ln -sf $$UTILS_LIB_NAME $(INSTALL_ROOT)$$INSTALL_LIBS/$$UTILS_LIB_LINK
  INSTALLS        += lib_utils

  name_tool.path   = $$INSTALL_BINS
  name_tool.extra  = install_name_tool -change $$UTILS_LIB_NAME @executable_path/../Frameworks/$$UTILS_LIB_NAME $(INSTALL_ROOT)$$INSTALL_BINS/$$INSTALL_APP_DIR/Contents/MacOS/$$VACUUM_LOADER_NAME
  INSTALLS        += name_tool

  sdk_utils.path   = $$INSTALL_INCLUDES/utils
  sdk_utils.files  = ../utils/*.h
  INSTALLS        += sdk_utils

  #Dirty hack to install utils translations
  TARGET           = $$VACUUM_UTILS_NAME
  include(../translations/languages.inc)
  TARGET           = $$VACUUM_LOADER_NAME
}
