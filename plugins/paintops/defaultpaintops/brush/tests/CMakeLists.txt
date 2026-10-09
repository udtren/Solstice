# on macOS cannot link to a MH_LIBRARY, see bug 417391
kis_add_test(KisDabRenderingQueueTest.cpp
    TEST_NAME KisDabRenderingQueueTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaimage kritatestsdk
    )

kis_add_test(KisToolOptionsBrushTest.cpp
    TEST_NAME KisToolOptionsBrushTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
    )

kis_add_test(KisBrushTipOptionParityTest.cpp
    TEST_NAME KisBrushTipOptionParityTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
    )

kis_add_test(KisCloneParityTest.cpp
    TEST_NAME KisCloneParityTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
    )

kis_add_test(KisBrushStrokeReplayTest.cpp
    TEST_NAME KisBrushStrokeReplayTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
    )

kis_add_test(KisBrushStrokeLayerTest.cpp
    TEST_NAME KisBrushStrokeLayerTest
    LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
    )

if(HAVE_KRITA_GPU_ENGINE)
    kis_add_test(KisGpuBrushJobsTest.cpp
        TEST_NAME KisGpuBrushJobsTest
        LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaimage kritatestsdk
        )
    kis_add_test(KisGpuStrokeTest.cpp
        TEST_NAME KisGpuStrokeTest
        LINK_LIBRARIES kritadefaultpaintops_static kritalibpaintop kritaui kritatestsdk
        )
endif()


krita_add_broken_unit_test(kis_brushop_test.cpp ../../../../../sdk/tests/stroke_testing_utils.cpp
    TEST_NAME KisBrushOpTest
    LINK_LIBRARIES kritaui kritalibpaintop kritatestsdk
    )
