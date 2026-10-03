set(KRITA_QUICKACCESS_CORE_SOURCES
    QuickAccessModel.cpp
    QuickAccessLayoutEngine.cpp
    QuickAccessProfileRepository.cpp
    QuickAccessLegacyImporter.cpp
)

kis_add_library(kritaquickaccesscore STATIC ${KRITA_QUICKACCESS_CORE_SOURCES})
target_link_libraries(kritaquickaccesscore PUBLIC Qt${QT_MAJOR_VERSION}::Core)
target_include_directories(kritaquickaccesscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(KRITA_QUICKACCESS_DOCKER_SOURCES
    QuickAccessDockerPlugin.cpp
    QuickAdjustDock.cpp
    QuickAdjustKeyController.cpp
    QuickColorSelectorDock.cpp
    QuickAccessDock.cpp
    QuickAccessGridEditDialog.cpp
    QuickAccessGesture.cpp
    QuickAccessItemExecutor.cpp
    QuickAccessResourcesDialog.cpp
    QuickAccessSettingsDialog.cpp
)

kis_add_library(kritaquickaccessdocker MODULE ${KRITA_QUICKACCESS_DOCKER_SOURCES})
target_link_libraries(kritaquickaccessdocker kritaui kritaquickaccesscore)
if(PYTHON_LIBRARY)
    target_include_directories(kritaquickaccessdocker SYSTEM PRIVATE "${PYTHON_INCLUDE_DIRS}")
    target_link_libraries(kritaquickaccessdocker ${PYTHON_LIBRARY})
    target_compile_definitions(kritaquickaccessdocker PRIVATE HAVE_QUICKACCESS_PYTHON=1)
endif()

file(GLOB_RECURSE KRITA_QUICKACCESS_ICON_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.png"
)
qt_add_resources(kritaquickaccessdocker quickaccess_icons
    PREFIX "/quickaccess"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES ${KRITA_QUICKACCESS_ICON_FILES}
)

install(TARGETS kritaquickaccessdocker DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
install(FILES quickaccess.action DESTINATION ${KDE_INSTALL_DATADIR}/krita/actions)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
