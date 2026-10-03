kis_add_tests(
    kis_kra_loader_test.cpp
    LINK_LIBRARIES kritaui kritalibkra kritatestsdk
    )


kis_add_test(
    kis_kra_saver_test.cpp
    LINK_LIBRARIES kritaui kritalibkra kritatransformmaskstubs
    )

# GPU engine (Solstice, docs/agent/gpu-engine.md)
kis_add_test(
    KisGpuSaveTest.cpp
    LINK_LIBRARIES kritaui kritalibkra kritatestsdk
    )
