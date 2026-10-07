#include <KoConfig.h>

/* This variable contains the path to the root of the source directory */
#define KRITA_SOURCE_DIR "${CMAKE_SOURCE_DIR}"

/* This variable contains the path to the data install dir */
#define KRITA_RESOURCE_DIRS_FOR_TESTS "${CMAKE_INSTALL_PREFIX}/${KDE_INSTALL_DATADIR};${CMAKE_SOURCE_DIR}/krita/data"

/* This variable contains the path to the plugins install dir */
#define KRITA_PLUGINS_DIR_FOR_TESTS "${CMAKE_INSTALL_PREFIX}/${KRITA_PLUGIN_INSTALL_DIR}"

/* Solstice: the installed Fontconfig configuration (fonts.conf) for tests,
   which run from the build tree where KoFontRegistry does not find it */
#define KRITA_FONTCONFIG_DIR_FOR_TESTS "${CMAKE_INSTALL_PREFIX}/etc/fonts"
