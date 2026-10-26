kis_add_test(TestControlProtocolTest.cpp ../TestControlHttpServer.cpp ../TestControlMcpServer.cpp
    TEST_NAME TestControlProtocolTest
    LINK_LIBRARIES Qt${QT_MAJOR_VERSION}::Network kritatestsdk)
target_include_directories(TestControlProtocolTest PRIVATE ..)

kis_add_test(TestControlInputInjectorTest.cpp ../TestControlInputInjector.cpp
    TEST_NAME TestControlInputInjectorTest
    LINK_LIBRARIES Qt${QT_MAJOR_VERSION}::Widgets kritatestsdk)
target_include_directories(TestControlInputInjectorTest PRIVATE ..)
