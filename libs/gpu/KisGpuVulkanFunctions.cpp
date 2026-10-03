/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGpuVulkanFunctions.h"

#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

#include <memory>

namespace
{
PFN_vkGetInstanceProcAddr loadLoader(QString *errorMessage)
{
    static QMutex mutex;
    static QLibrary *library = nullptr;
    static PFN_vkGetInstanceProcAddr entry = nullptr;

    QMutexLocker locker(&mutex);
    if (entry) {
        return entry;
    }

#ifdef Q_OS_WIN
    const QStringList candidates = {QStringLiteral("vulkan-1")};
#else
    const QStringList candidates = {QStringLiteral("vulkan"), QStringLiteral("libvulkan.so.1")};
#endif

    QStringList failures;
    for (const QString &name : candidates) {
        std::unique_ptr<QLibrary> candidate(new QLibrary(name));
        if (!candidate->load()) {
            failures << candidate->errorString();
            continue;
        }
        entry = reinterpret_cast<PFN_vkGetInstanceProcAddr>(candidate->resolve("vkGetInstanceProcAddr"));
        if (entry) {
            library = candidate.release();
            break;
        }
        failures << QStringLiteral("%1: vkGetInstanceProcAddr not exported").arg(name);
    }

    if (!entry && errorMessage) {
        *errorMessage = QStringLiteral("Vulkan loader is not available (%1)").arg(failures.join(QStringLiteral("; ")));
    }
    Q_UNUSED(library);
    return entry;
}
} // namespace

bool KisGpuVulkanFunctions::loadGlobal(QString *errorMessage)
{
    vkGetInstanceProcAddr = loadLoader(errorMessage);
    if (!vkGetInstanceProcAddr) {
        return false;
    }

#define KIS_GPU_LOAD_GLOBAL(name)                                                                                      \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name));                                 \
    if (!name) {                                                                                                       \
        if (errorMessage) {                                                                                            \
            *errorMessage = QStringLiteral("Missing Vulkan global function " #name);                                   \
        }                                                                                                              \
        return false;                                                                                                  \
    }
    KIS_GPU_VK_GLOBAL_FUNCTIONS(KIS_GPU_LOAD_GLOBAL)
#undef KIS_GPU_LOAD_GLOBAL

    return true;
}

bool KisGpuVulkanFunctions::loadInstance(VkInstance instance, QString *errorMessage)
{
#define KIS_GPU_LOAD_INSTANCE(name)                                                                                    \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));                                       \
    if (!name) {                                                                                                       \
        if (errorMessage) {                                                                                            \
            *errorMessage = QStringLiteral("Missing Vulkan instance function " #name);                                 \
        }                                                                                                              \
        return false;                                                                                                  \
    }
    KIS_GPU_VK_INSTANCE_FUNCTIONS(KIS_GPU_LOAD_INSTANCE)
#undef KIS_GPU_LOAD_INSTANCE

    return true;
}

bool KisGpuVulkanFunctions::loadDevice(VkDevice device, QString *errorMessage)
{
#define KIS_GPU_LOAD_DEVICE(name)                                                                                      \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                           \
    if (!name) {                                                                                                       \
        if (errorMessage) {                                                                                            \
            *errorMessage = QStringLiteral("Missing Vulkan device function " #name);                                   \
        }                                                                                                              \
        return false;                                                                                                  \
    }
    KIS_GPU_VK_DEVICE_FUNCTIONS(KIS_GPU_LOAD_DEVICE)
#undef KIS_GPU_LOAD_DEVICE

#define KIS_GPU_LOAD_OPTIONAL_DEVICE(name) name = vkGetDeviceProcAddr(device, #name);
    KIS_GPU_VK_DEVICE_OPTIONAL_FUNCTIONS(KIS_GPU_LOAD_OPTIONAL_DEVICE)
#undef KIS_GPU_LOAD_OPTIONAL_DEVICE

    return true;
}

QString kisGpuVkResultString(VkResult result)
{
    switch (result) {
#define KIS_GPU_RESULT_CASE(value)                                                                                     \
    case value:                                                                                                        \
        return QStringLiteral(#value);
        KIS_GPU_RESULT_CASE(VK_SUCCESS)
        KIS_GPU_RESULT_CASE(VK_NOT_READY)
        KIS_GPU_RESULT_CASE(VK_TIMEOUT)
        KIS_GPU_RESULT_CASE(VK_INCOMPLETE)
        KIS_GPU_RESULT_CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
        KIS_GPU_RESULT_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        KIS_GPU_RESULT_CASE(VK_ERROR_INITIALIZATION_FAILED)
        KIS_GPU_RESULT_CASE(VK_ERROR_DEVICE_LOST)
        KIS_GPU_RESULT_CASE(VK_ERROR_MEMORY_MAP_FAILED)
        KIS_GPU_RESULT_CASE(VK_ERROR_LAYER_NOT_PRESENT)
        KIS_GPU_RESULT_CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
        KIS_GPU_RESULT_CASE(VK_ERROR_FEATURE_NOT_PRESENT)
        KIS_GPU_RESULT_CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
        KIS_GPU_RESULT_CASE(VK_ERROR_TOO_MANY_OBJECTS)
        KIS_GPU_RESULT_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
        KIS_GPU_RESULT_CASE(VK_ERROR_INVALID_EXTERNAL_HANDLE)
#undef KIS_GPU_RESULT_CASE
    default:
        return QStringLiteral("VkResult(%1)").arg(int(result));
    }
}
