kis_add_tests(
    QuickAccessCoreTest.cpp
    LINK_LIBRARIES kritaquickaccesscore kritatestsdk
)

kis_add_test(QuickAdjustKeyControllerTest.cpp ../QuickAdjustKeyController.cpp
    TEST_NAME QuickAdjustKeyControllerTest
    LINK_LIBRARIES kritaui kritatestsdk
)
target_include_directories(QuickAdjustKeyControllerTest PRIVATE ..)
