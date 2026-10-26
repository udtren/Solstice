set(kritatestcontrol_SOURCES
    TestControlPlugin.cpp
    TestControlHttpServer.cpp
    TestControlMcpServer.cpp
    TestControlInputInjector.cpp
    TestControlCanvasTools.cpp
)

kis_add_library(kritatestcontrol MODULE ${kritatestcontrol_SOURCES})

target_link_libraries(kritatestcontrol kritaui Qt${QT_MAJOR_VERSION}::Network)

install(TARGETS kritatestcontrol DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
