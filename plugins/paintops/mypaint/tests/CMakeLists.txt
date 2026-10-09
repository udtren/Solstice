########### next target ###############

kis_add_test(
    kis_mypaintop_test.cpp ../MyPaintPaintOpSettings.cpp ../MyPaintPaintOpPreset.cpp ../MyPaintSurface.cpp
    TEST_NAME KisMyPaintOpTest
    LINK_LIBRARIES kritaimage kritamypaintop_static kritalibpaintop LibMyPaint::mypaint kritatestsdk
    )


kis_add_test(KisMyPaintParityTest.cpp
    TEST_NAME KisMyPaintParityTest
    LINK_LIBRARIES kritamypaintop_static kritalibpaintop kritaui kritatestsdk)
