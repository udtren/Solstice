add_library(kritatestsdk INTERFACE)

target_link_libraries(kritatestsdk INTERFACE Qt${QT_MAJOR_VERSION}::Test)

target_compile_definitions(kritatestsdk INTERFACE 
    FILES_DEFAULT_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
    SYSTEM_RESOURCES_DATA_DIR="${CMAKE_SOURCE_DIR}/krita/data/"
)

# Solstice: Qt 6 here has no built-in MIME database; the application embeds
# freedesktop.org.xml at :/qt-project.org/qmime/packages (krita/CMakeLists.txt).
# Without it, MIME lookups in tests return nothing and resource loaders are
# not found ("Could not create loader for ..."). Compile it once and link the
# object into every test, so that its resource registers at startup.
if (QT_MAJOR_VERSION STREQUAL "6")
    qt_add_resources(kritatestmimedatabase_SRCS ${CMAKE_SOURCE_DIR}/krita/data/mime-database/mime-database.qrc)
    add_library(kritatestmimedatabase OBJECT ${kritatestmimedatabase_SRCS})
    target_link_libraries(kritatestmimedatabase PRIVATE Qt${QT_MAJOR_VERSION}::Core)
    target_sources(kritatestsdk INTERFACE $<TARGET_OBJECTS:kritatestmimedatabase>)
endif()

add_library(kritatransformmaskstubs STATIC
    KisDumbTransformMaskParams.cpp
    KisDumbAnimatedTransformMaskParamsHolder.cpp
    KritaTransformMaskStubs.cpp
)

generate_export_header(kritatransformmaskstubs BASE_NAME kritatransformmaskstubs)

target_link_libraries(kritatransformmaskstubs INTERFACE kritatestsdk)
target_link_libraries(kritatransformmaskstubs PUBLIC kritaimage)
