/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Shared CPU-compatible separable RGBA blend functions for layers and dabs.
 */
// Must match KisGpuBlendOp in KisGpuLayerCompositor.h
const uint OpOver = 0;
const uint OpMultiply = 1;
const uint OpScreen = 2;
const uint OpAdd = 3;
const uint OpSubtract = 4;
const uint OpDarken = 5;
const uint OpLighten = 6;
const uint OpDifference = 7;
const uint OpOverlay = 8;
const uint OpHardLight = 9;
const uint OpExclusion = 10;
const uint OpErase = 11;

// qFuzzyIsNull(float)
bool fuzzyZero(float v)
{
    return abs(v) <= 0.00001;
}

// qFuzzyCompare(v, 1.0f)
bool fuzzyUnit(float v)
{
    return abs(v - 1.0) * 100000.0 <= min(abs(v), 1.0);
}

// qFuzzyCompare(v, 0.5f)
bool fuzzyHalf(float v)
{
    return abs(v - 0.5) * 100000.0 <= min(abs(v), 0.5);
}

float hardLight(float s, float d)
{
    if (fuzzyHalf(s)) {
        return d;
    }
    float s2 = s + s;
    if (s > 0.5) {
        s2 -= 1.0;
        return s2 + d - s2 * d;
    }
    return s2 * d;
}

float blendChannel(uint op, float s, float d)
{
    switch (op) {
    case OpMultiply:
        return s * d;
    case OpScreen:
        return s + d - s * d;
    case OpAdd:
        return s + d;
    case OpSubtract:
        return d - s;
    case OpDarken:
        return min(s, d);
    case OpLighten:
        return max(s, d);
    case OpDifference:
        return max(s, d) - min(s, d);
    case OpOverlay:
        return hardLight(d, s);
    case OpHardLight:
        return hardLight(s, d);
    case OpExclusion: {
        const float x = s * d;
        return d + s - (x + x);
    }
    }
    return s;
}

vec3 blendColor(uint op, vec3 s, vec3 d)
{
    return vec3(blendChannel(op, s.r, d.r), blendChannel(op, s.g, d.g), blendChannel(op, s.b, d.b));
}

vec3 clampSource(uint op, vec3 s)
{
    // Clamp policies of the functors (KoCompositeOpGenericFunctorBase.h)
    if (op == OpOverlay || op == OpHardLight || op == OpExclusion) {
        return clamp(s, 0.0, 1.0);
    }
    return s;
}

vec3 clampDestination(uint op, vec3 d)
{
    if (op == OpExclusion) {
        return clamp(d, 0.0, 1.0);
    }
    return d;
}

vec4 compositeGenericColor(uint op, vec4 dst, vec4 src, float srcAlpha, bool alphaLocked)
{
    const float dstAlpha = dst.a;
    if (fuzzyZero(srcAlpha)) {
        return dst;
    }

    const vec3 s = clampSource(op, src.rgb);
    if (alphaLocked) {
        if (!fuzzyZero(dstAlpha)) {
            const vec3 d = clampDestination(op, dst.rgb);
            return vec4((blendColor(op, s, d) - d) * srcAlpha + d, dstAlpha);
        }
        return dst;
    }
    if (fuzzyZero(dstAlpha)) {
        return vec4(s, srcAlpha);
    }

    const vec3 d = clampDestination(op, dst.rgb);
    const vec3 cf = blendColor(op, s, d);
    if (fuzzyUnit(dstAlpha)) {
        return vec4((cf - d) * srcAlpha + d, 1.0);
    }
    if (fuzzyUnit(srcAlpha)) {
        return vec4((cf - s) * dstAlpha + s, 1.0);
    }

    const float newAlpha = srcAlpha + dstAlpha - srcAlpha * dstAlpha;
    if (fuzzyZero(newAlpha)) {
        return vec4(dst.rgb, newAlpha);
    }
    const vec3 result = (1.0 - srcAlpha) * dstAlpha * d + (1.0 - dstAlpha) * srcAlpha * s + dstAlpha * srcAlpha * cf;
    return vec4(result / newAlpha, newAlpha);
}

vec4 compositeGeneric(uint op, vec4 dst, vec4 src, float opacity, bool alphaLocked, float coverage, uint channels)
{
    alphaLocked = alphaLocked || (channels & 8) == 0;
    // KoCompositeOpBase clears hidden RGB for ANY restricted channel set,
    // before testing source alpha/coverage (including zero coverage).
    if ((alphaLocked || channels != 15) && dst.a == 0.0) dst = vec4(0.0);
    vec4 result = compositeGenericColor(op, dst, src, (src.a * coverage) * opacity, alphaLocked);
    if ((channels & 1) == 0) result.r = dst.r;
    if ((channels & 2) == 0) result.g = dst.g;
    if ((channels & 4) == 0) result.b = dst.b;
    return result;
}
