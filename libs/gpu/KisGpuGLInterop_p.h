/*
 * SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGPUGLINTEROP_P_H
#define KISGPUGLINTEROP_P_H

// Private to kritagpu: OpenGL entry points of GL_EXT_memory_object(_win32)
// and GL_EXT_semaphore(_win32). Include after <windows.h>.

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QStringList>

namespace KisGpuGLInterop
{
// GL_EXT_memory_object, GL_EXT_memory_object_win32, GL_EXT_semaphore,
// GL_EXT_semaphore_win32 tokens (not all GL headers ship them).
constexpr GLenum GL_TEXTURE_TILING_EXT_ = 0x9580;
constexpr GLenum GL_DEDICATED_MEMORY_OBJECT_EXT_ = 0x9581;
constexpr GLenum GL_OPTIMAL_TILING_EXT_ = 0x9584;
constexpr GLenum GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_ = 0x9587;
constexpr GLenum GL_LAYOUT_GENERAL_EXT_ = 0x958D;
constexpr GLenum GL_NUM_DEVICE_UUIDS_EXT_ = 0x9596;
constexpr GLenum GL_DEVICE_UUID_EXT_ = 0x9597;
constexpr GLenum GL_RGBA16F_ = 0x881A;
constexpr GLenum GL_RGBA32F_ = 0x8814;

using PFN_glCreateMemoryObjectsEXT = void(APIENTRY *)(GLsizei, GLuint *);
using PFN_glDeleteMemoryObjectsEXT = void(APIENTRY *)(GLsizei, const GLuint *);
using PFN_glMemoryObjectParameterivEXT = void(APIENTRY *)(GLuint, GLenum, const GLint *);
using PFN_glImportMemoryWin32HandleEXT = void(APIENTRY *)(GLuint, quint64, GLenum, void *);
using PFN_glTexStorageMem2DEXT = void(APIENTRY *)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, quint64);
using PFN_glGenSemaphoresEXT = void(APIENTRY *)(GLsizei, GLuint *);
using PFN_glDeleteSemaphoresEXT = void(APIENTRY *)(GLsizei, const GLuint *);
using PFN_glImportSemaphoreWin32HandleEXT = void(APIENTRY *)(GLuint, GLenum, void *);
using PFN_glWaitSemaphoreEXT = void(APIENTRY *)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
using PFN_glSignalSemaphoreEXT =
    void(APIENTRY *)(GLuint, GLuint, const GLuint *, GLuint, const GLuint *, const GLenum *);
using PFN_glGetUnsignedBytei_vEXT = void(APIENTRY *)(GLenum, GLuint, GLubyte *);
using PFN_glBufferStorageMemEXT = void(APIENTRY *)(GLenum, GLsizeiptr, GLuint, quint64);

struct GLInteropFunctions {
    PFN_glCreateMemoryObjectsEXT glCreateMemoryObjectsEXT = nullptr;
    PFN_glDeleteMemoryObjectsEXT glDeleteMemoryObjectsEXT = nullptr;
    PFN_glMemoryObjectParameterivEXT glMemoryObjectParameterivEXT = nullptr;
    PFN_glImportMemoryWin32HandleEXT glImportMemoryWin32HandleEXT = nullptr;
    PFN_glTexStorageMem2DEXT glTexStorageMem2DEXT = nullptr;
    PFN_glGenSemaphoresEXT glGenSemaphoresEXT = nullptr;
    PFN_glDeleteSemaphoresEXT glDeleteSemaphoresEXT = nullptr;
    PFN_glImportSemaphoreWin32HandleEXT glImportSemaphoreWin32HandleEXT = nullptr;
    PFN_glWaitSemaphoreEXT glWaitSemaphoreEXT = nullptr;
    PFN_glSignalSemaphoreEXT glSignalSemaphoreEXT = nullptr;
    PFN_glGetUnsignedBytei_vEXT glGetUnsignedBytei_vEXT = nullptr;
    PFN_glBufferStorageMemEXT glBufferStorageMemEXT = nullptr;

    bool load(QOpenGLContext *context, QString *errorMessage)
    {
        const QStringList requiredExtensions = {QStringLiteral("GL_EXT_memory_object"),
                                                QStringLiteral("GL_EXT_memory_object_win32"),
                                                QStringLiteral("GL_EXT_semaphore"),
                                                QStringLiteral("GL_EXT_semaphore_win32")};
        for (const QString &extension : requiredExtensions) {
            if (!context->hasExtension(extension.toLatin1())) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("OpenGL extension %1 is not available").arg(extension);
                }
                return false;
            }
        }

#define KIS_GPU_LOAD_GL(name)                                                                                          \
    name = reinterpret_cast<PFN_##name>(context->getProcAddress(#name));                                               \
    if (!name) {                                                                                                       \
        if (errorMessage) {                                                                                            \
            *errorMessage = QStringLiteral("Missing OpenGL function " #name);                                          \
        }                                                                                                              \
        return false;                                                                                                  \
    }
        KIS_GPU_LOAD_GL(glCreateMemoryObjectsEXT)
        KIS_GPU_LOAD_GL(glDeleteMemoryObjectsEXT)
        KIS_GPU_LOAD_GL(glMemoryObjectParameterivEXT)
        KIS_GPU_LOAD_GL(glImportMemoryWin32HandleEXT)
        KIS_GPU_LOAD_GL(glTexStorageMem2DEXT)
        KIS_GPU_LOAD_GL(glGenSemaphoresEXT)
        KIS_GPU_LOAD_GL(glDeleteSemaphoresEXT)
        KIS_GPU_LOAD_GL(glImportSemaphoreWin32HandleEXT)
        KIS_GPU_LOAD_GL(glWaitSemaphoreEXT)
        KIS_GPU_LOAD_GL(glSignalSemaphoreEXT)
        KIS_GPU_LOAD_GL(glGetUnsignedBytei_vEXT)
        KIS_GPU_LOAD_GL(glBufferStorageMemEXT)
#undef KIS_GPU_LOAD_GL
        return true;
    }

    /// True if one of the GL implementation's devices is @p deviceUuid.
    bool sameDevice(QOpenGLContext *context, const QByteArray &deviceUuid) const
    {
        GLint uuidCount = 0;
        context->functions()->glGetIntegerv(GL_NUM_DEVICE_UUIDS_EXT_, &uuidCount);
        for (GLint i = 0; i < uuidCount; i++) {
            GLubyte uuid[16] = {};
            glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT_, GLuint(i), uuid);
            if (QByteArray(reinterpret_cast<const char *>(uuid), 16) == deviceUuid) {
                return true;
            }
        }
        return false;
    }
};
} // namespace KisGpuGLInterop

#endif // KISGPUGLINTEROP_P_H
