kis_add_test(
    KisColorsmudgeOpTest.cpp
     $<TARGET_PROPERTY:kritatestsdk,SOURCE_DIR>/stroke_testing_utils.cpp
    TEST_NAME KisColorsmudgeOpTest
    LINK_LIBRARIES kritalibpaintop kritaimage kritatestsdk
    )

kis_add_test(KisColorSmudgeParityTest.cpp
    TEST_NAME KisColorSmudgeParityTest
    LINK_LIBRARIES kritacolorsmudgepaintop_static kritalibpaintop kritaui kritatestsdk
    )
