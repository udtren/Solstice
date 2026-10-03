set(KRITA_RESTNOTE_SOURCES
    RestNotePlugin.cpp
    RestNoteDock.cpp
)

kis_add_library(kritarestnotedocker MODULE ${KRITA_RESTNOTE_SOURCES})
target_link_libraries(kritarestnotedocker kritaui)

file(GLOB_RECURSE KRITA_RESTNOTE_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.png"
)
qt_add_resources(kritarestnotedocker restnote_icons
    PREFIX "/restnote"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES ${KRITA_RESTNOTE_ICON_FILES}
)

install(TARGETS kritarestnotedocker DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
