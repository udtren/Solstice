set(kritabuginfo_SOURCES
    buginfo.cpp
    dlg_buginfo.cpp
    DlgSysInfo.cpp
    DlgKritaLog.cpp
)

if (WIN32)
    set(kritabuginfo_SOURCES ${kritabuginfo_SOURCES} DlgCrashLog.cpp)
endif()

if (KRITA_USE_SURFACE_COLOR_MANAGEMENT_API)
    set(kritabuginfo_SOURCES ${kritabuginfo_SOURCES} DlgColorManagementInfo.cpp)
endif()

ki18n_wrap_ui(kritabuginfo_SOURCES wdg_buginfo.ui )

kis_add_library(kritabuginfo MODULE ${kritabuginfo_SOURCES})

target_link_libraries(kritabuginfo kritaui)

if (KRITA_USE_SURFACE_COLOR_MANAGEMENT_API)
    target_link_libraries(kritabuginfo kritasurfacecolormanagementapi)
endif()


install(TARGETS kritabuginfo  DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

install( FILES  buginfo.xmlgui  DESTINATION ${KDE_INSTALL_DATADIR}/kritaplugins)
