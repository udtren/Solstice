set(KRITA_ASSETLIBRARY_SOURCES
    AssetLibraryPlugin.cpp
    AssetLibraryDock.cpp
)

kis_add_library(kritaassetlibrarydocker MODULE ${KRITA_ASSETLIBRARY_SOURCES})
target_link_libraries(kritaassetlibrarydocker kritaui)
install(TARGETS kritaassetlibrarydocker DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
