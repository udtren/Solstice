kis_add_tests(
    KisGpuEngineTest.cpp
    LINK_LIBRARIES kritagpu kritapigment kritatestsdk Qt${QT_MAJOR_VERSION}::Concurrent
    )

if(WIN32)
    kis_add_tests(
        KisGpuGLInteropTest.cpp
        LINK_LIBRARIES kritagpu kritatestsdk Qt${QT_MAJOR_VERSION}::OpenGL
        )
endif()
