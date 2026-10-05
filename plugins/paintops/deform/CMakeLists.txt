set(kritadeformpaintop_SOURCES
    deform_brush.cpp
    deform_paintop_plugin.cpp
    kis_deform_paintop.cpp
    kis_deform_paintop_settings.cpp
    kis_deform_paintop_settings_widget.cpp
    KisBrushSizeOptionData.cpp
    KisBrushSizeOptionModel.cpp
    KisBrushSizeOptionWidget.cpp
    KisDeformOptionData.cpp
    KisDeformOptionModel.cpp
    KisDeformOptionWidget.cpp
    )

ki18n_wrap_ui(kritadeformpaintop_SOURCES wdgdeformoptions.ui )

set(kritadeformpaintop_static_SOURCES ${kritadeformpaintop_SOURCES})
list(REMOVE_ITEM kritadeformpaintop_static_SOURCES deform_paintop_plugin.cpp)

kis_add_library(kritadeformpaintop_static STATIC ${kritadeformpaintop_static_SOURCES})
target_link_libraries(kritadeformpaintop_static kritaui kritalibpaintop)

kis_add_library(kritadeformpaintop MODULE deform_paintop_plugin.cpp)

target_link_libraries(kritadeformpaintop kritadeformpaintop_static)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS kritadeformpaintop  DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
install( FILES  krita-deform.png DESTINATION ${KDE_INSTALL_DATADIR}/krita/images )
