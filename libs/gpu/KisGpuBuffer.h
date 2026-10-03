/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUBUFFER_H
#define KISGPUBUFFER_H

#include <QString>

#include <memory>

#include "KisGpuVulkanFunctions.h"
#include "kritagpu_export.h"

class KisGpuContext;

/**
 * A VkBuffer with its own dedicated VkDeviceMemory.
 *
 * Every buffer is usable as a storage buffer, as a transfer source and
 * destination, and through its device address (GL_EXT_buffer_reference).
 * The GPU engine passes buffers to shaders by address only; it does not use
 * descriptor sets.
 */
class KRITAGPU_EXPORT KisGpuBuffer
{
public:
    enum class Location {
        /// Device-local, not mappable. Tile storage and intermediate data.
        Device,
        /// Host-visible, write-combined. Prefers device-local (Resizable BAR).
        Upload,
        /// Host-visible and host-cached. GPU-to-CPU readback.
        Readback,
    };

    ~KisGpuBuffer();

    KisGpuBuffer(const KisGpuBuffer &) = delete;
    KisGpuBuffer &operator=(const KisGpuBuffer &) = delete;

    static std::unique_ptr<KisGpuBuffer>
    create(KisGpuContext &context, VkDeviceSize size, Location location, QString *errorMessage = nullptr);

    VkBuffer handle() const;
    VkDeviceSize size() const;
    VkDeviceAddress deviceAddress() const;
    Location location() const;
    /// True if the memory is DEVICE_LOCAL (also for Upload buffers in ReBAR memory).
    bool isDeviceLocal() const;

    /// Persistently mapped pointer; nullptr for Location::Device.
    void *mapped() const;

private:
    KisGpuBuffer(KisGpuContext &context);

    KisGpuContext &m_context;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    VkDeviceAddress m_address = 0;
    Location m_location = Location::Device;
    bool m_deviceLocal = false;
    void *m_mapped = nullptr;
};

#endif // KISGPUBUFFER_H
