set(KRITA_OVERVIEWDOCKER_SOURCES overviewdocker.cpp
    overviewdocker_dock.cpp
    overviewwidget.cc
    overviewdocker_page.cpp)
kis_add_library(kritaoverviewdocker MODULE ${KRITA_OVERVIEWDOCKER_SOURCES})
target_link_libraries(kritaoverviewdocker kritaui Qt${QT_MAJOR_VERSION}::Concurrent)
install(TARGETS kritaoverviewdocker  DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
