kis_add_tests(KisSignalAutoConnectionTest.cpp
    KisSignalCompressorTest.cpp
    KisForestTest.cpp
    KisMplTest.cpp
    KisRectsGridTest.cpp
    KisLazyStorageTest.cpp
    KisValueCacheTest.cpp
    KisHistoryListTest.cpp
    KisAdaptedLockTest.cpp
    KisGlobalTest.cpp
    LINK_LIBRARIES kritaglobal kritatestsdk
    )

include(SolsticeEmbedRcc)
solstice_embed_rcc(SOLSTICE_PATHS_TEST_DEFAULTS solsticePathsTestDefaultsRcc
    "${CMAKE_CURRENT_SOURCE_DIR}/KisSolsticePathsTestDefaults.qrc"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/solstice-paths-test/defaultsrc")
kis_add_test(
    KisSolsticePathsTest.cpp
    ${SOLSTICE_PATHS_TEST_DEFAULTS}
    TEST_NAME KisSolsticePathsTest
    LINK_LIBRARIES kritaglobal kritatestsdk
    )

kis_add_test(
    KisSolsticeProfileTest.cpp
    TEST_NAME KisSolsticeProfileTest
    LINK_LIBRARIES kritaglobal kritatestsdk
    )

