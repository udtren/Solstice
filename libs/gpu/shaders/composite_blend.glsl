/* SPDX-FileCopyrightText: 2026 Krita contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Shared CPU-compatible RGBA blend functions for layers and dabs.
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
const uint OpLinearBurn = 12;
const uint OpLinearLight = 13;
const uint OpPinLight = 14;
const uint OpSoftLightSvg = 15;
const uint OpSoftLightPhotoshop = 16;
const uint OpColorDodge = 17;
const uint OpColorBurn = 18;
const uint OpDivide = 19;
const uint OpVividLight = 20;
const uint OpHardMix = 21;
const uint OpHardMixPhotoshop = 22;
const uint OpHardMixSofterPhotoshop = 23;
const uint OpGrainMerge = 24;
const uint OpGrainExtract = 25;
const uint OpNegation = 26;
const uint OpAllanon = 27;
const uint OpHue = 28;
const uint OpSaturation = 29;
const uint OpColor = 30;
const uint OpLuminosity = 31;
const uint OpDarkerColor = 32;
const uint OpLighterColor = 33;

float maskCoverage(uint value, bool exact)
{
    // Match the CPU's float(byte) / 255 lookup. A rounded reciprocal multiply
    // alone differs by one ULP for some bytes; repeated HDR dabs amplify it.
    precise float scaled = float(value) * (1.0 / 255.0);
    // Retain the established rounding of existing modes. The new modes use
    // the CPU lookup's exact conversion, needed for repeated Linear Light.
    if (!exact) return scaled;
    float residual = fma(-scaled, 255.0, float(value));
    return fma(residual, 1.0 / 255.0, scaled);
}

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
#ifdef TILE_F16
    if (abs(s - 0.5) < 0.001) return d;
    float s2 = s + s;
    if (s > 0.5) {
        s2 = float(float16_t(s2 - 1.0));
        return s2 + d - float(float16_t(s2 * d));
    }
    return float(float16_t(s2)) * d;
#else
    if (fuzzyHalf(s)) {
        return d;
    }
    float s2 = s + s;
    if (s > 0.5) {
        s2 -= 1.0;
        return s2 + d - s2 * d;
    }
    return s2 * d;
#endif
}

float finiteSDRDivision(float numerator, float denominator)
{
    float value = numerator / denominator;
    return isinf(value) || isnan(value) ? 1.0 : value;
}

float colorDodge(float s, float d)
{
    if (s == 1.0) return d <= 0.0 ? 0.0 : 1.0;
#ifdef TILE_F16
    // Arithmetic::inv<half> rounds before the division.
    return clamp(finiteSDRDivision(d, float(float16_t(1.0 - s))), 0.0, 1.0);
#else
    return clamp(finiteSDRDivision(d, 1.0 - s), 0.0, 1.0);
#endif
}

float colorBurn(float s, float d)
{
    if (d == 1.0) return 1.0;
#ifdef TILE_F16
    // Both inv(dst) and clampToSDR<half>(quotient) return half values.
    float value = float(float16_t(1.0 - d)) / s;
    if (isinf(value) || isnan(value)) return 0.0;
    return 1.0 - float(float16_t(clamp(value, 0.0, 1.0)));
#else
    precise float numerator = 1.0 - d;
    precise float value = numerator / s;
    if (isinf(value) || isnan(value)) return 0.0;
    // Recover the rounding lost by the GPU's reciprocal-based division.
    float residual = fma(-value, s, numerator);
    value = fma(residual, 1.0 / s, value);
    return 1.0 - clamp(value, 0.0, 1.0);
#endif
}

float blendChannel(uint op, float s, float d)
{
    switch (op) {
    case OpMultiply:
        return s * d;
    case OpScreen:
#ifdef TILE_F16
        return s + d - float(float16_t(s * d));
#else
        return s + d - s * d;
#endif
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
#ifdef TILE_F16
        // CFExclusion stores Arithmetic::mul<half> before widening to double.
        const float x = float(float16_t(s * d));
#else
        const float x = s * d;
#endif
        return d + s - (x + x);
    }
    case OpLinearBurn:
        // CFLinearBurn<..., ClampAsFloatHDR>: no upper clamp on the result.
        return max(s + d - 1.0, 0.0);
    case OpLinearLight: {
        // cfLinearLight's Arithmetic::clamp<float> leaves HDR/negative values intact.
        // Preserve CPU rounding before interpolation, even across many dabs.
        precise float result = (s + s + d) - 1.0;
        return result;
    }
    case OpPinLight:
        return max(s + s - 1.0, min(d, s + s));
#ifndef BASIC_BLEND_ONLY
    case OpSoftLightSvg:
    case OpSoftLightPhotoshop:
        if (s > 0.5) {
            float curve = op == OpSoftLightSvg && d <= 0.25 ? ((16.0 * d - 12.0) * d + 4.0) * d : sqrt(d);
            return d + (2.0 * s - 1.0) * (curve - d);
        }
        return d - (1.0 - 2.0 * s) * d * (1.0 - d);
    case OpColorDodge:
        return colorDodge(s, d);
    case OpColorBurn:
        return colorBurn(s, d);
    case OpDivide:
        if (d == 0.0) return 0.0;
        return s == 0.0 ? 1.0 : min(d / s, 1.0);
    case OpVividLight:
        if (s < 0.5) {
            if (s == 0.0) return d >= 1.0 ? 1.0 : 0.0;
            return clamp(1.0 - finiteSDRDivision(1.0 - d, s + s), 0.0, 1.0);
        }
        if (s == 1.0) return d <= 0.0 ? 0.0 : 1.0;
        return clamp(finiteSDRDivision(d, 2.0 * (1.0 - s)), 0.0, 1.0);
    case OpHardMix:
        return d > 0.5 ? colorDodge(s, d) : colorBurn(s, d);
    case OpHardMixPhotoshop:
        return s + d > 1.0 ? 1.0 : 0.0;
    case OpHardMixSofterPhotoshop:
        return clamp(3.0 * d - 2.0 * (1.0 - s), 0.0, 1.0);
    case OpGrainMerge:
        return clamp(d + s - 0.5, 0.0, 1.0);
    case OpGrainExtract:
        return clamp(d - s + 0.5, 0.0, 1.0);
    case OpNegation:
        return 1.0 - abs(1.0 - s - d);
    case OpAllanon:
        return (s + d) * 0.5;
#endif
    }
    return s;
}

vec3 blendColor(uint op, vec3 s, vec3 d)
{
    vec3 color = vec3(blendChannel(op, s.r, d.r), blendChannel(op, s.g, d.g), blendChannel(op, s.b, d.b));
#ifdef TILE_F16
    color = vec3(f16vec3(color));
#endif
    return color;
}

bool blendZero(uint op, float value)
{
#ifdef TILE_F16
    return abs(value) < 0.002;
#endif
    return fuzzyZero(value);
}

bool blendUnit(uint op, float value)
{
#ifdef TILE_F16
    return abs(value - 1.0) < 0.002;
#endif
    return fuzzyUnit(value);
}

float blendAlpha(uint op, float s, float d)
{
#ifdef TILE_F16
    return float(float16_t(s + d - float(float16_t(s * d))));
#endif
#ifndef BASIC_BLEND_ONLY
    if (op == OpColorBurn) {
        precise float alpha = s + d - s * d;
        return alpha;
    }
#endif
    return s + d - s * d;
}

vec3 blendWeightedColor(uint op, vec4 dst, vec4 src, float srcAlpha, vec3 blended)
{
#ifdef TILE_F16
    {
        // Arithmetic::blend<half> rounds each multiply result and the sum.
        vec3 a = vec3(f16vec3((float(float16_t(1.0 - srcAlpha)) * dst.a) * dst.rgb));
        vec3 b = vec3(f16vec3((float(float16_t(1.0 - dst.a)) * srcAlpha) * src.rgb));
        vec3 c = vec3(f16vec3((dst.a * srcAlpha) * blended));
        return vec3(f16vec3(a + b + c));
    }
#endif
#ifndef BASIC_BLEND_ONLY
    if (op == OpColorBurn) {
        precise vec3 result = (1.0 - srcAlpha) * dst.a * dst.rgb +
                              (1.0 - dst.a) * srcAlpha * src.rgb + dst.a * srcAlpha * blended;
        return result;
    }
#endif
    return (1.0 - srcAlpha) * dst.a * dst.rgb + (1.0 - dst.a) * srcAlpha * src.rgb + dst.a * srcAlpha * blended;
}

vec3 clampSource(uint op, vec3 s)
{
#ifndef BASIC_BLEND_ONLY
    if (op == OpDivide) return max(s, 0.0);
    if (op >= OpSoftLightSvg && op <= OpNegation) return clamp(s, 0.0, 1.0);
#endif
    // Clamp policies of the functors (KoCompositeOpGenericFunctorBase.h)
    if (op == OpOverlay || op == OpHardLight || op == OpExclusion || op == OpLinearBurn || op == OpPinLight) {
        return clamp(s, 0.0, 1.0);
    }
    return s;
}

vec3 clampDestination(uint op, vec3 d)
{
#ifndef BASIC_BLEND_ONLY
    if (op == OpDivide) return max(d, 0.0);
    if (op == OpSoftLightSvg || op == OpSoftLightPhotoshop || op == OpColorBurn ||
        (op >= OpHardMixPhotoshop && op <= OpNegation)) return clamp(d, 0.0, 1.0);
#endif
    if (op == OpExclusion || op == OpPinLight) {
        return clamp(d, 0.0, 1.0);
    }
    return d;
}

vec3 interpolateBlend(uint op, vec3 a, vec3 b, float alpha)
{
    if (op == OpLinearLight
#ifndef BASIC_BLEND_ONLY
        || op == OpColorBurn
#endif
    ) {
        // Repeated unbounded/nonlinear blending amplifies rounding differences.
        precise vec3 result = (b - a) * alpha + a;
        return result;
    }
    return (b - a) * alpha + a;
}

#ifndef BASIC_BLEND_ONLY
float hsyLightness(vec3 color)
{
    return 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
}

float hsySaturation(vec3 color)
{
    return max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
}

vec3 hsySetSaturation(vec3 color, float saturation)
{
    float low = min(min(color.r, color.g), color.b);
    float chroma = hsySaturation(color);
    return chroma > 1.192092896e-7 ? ((color - low) * saturation) / chroma : vec3(0.0);
}

vec3 hsySetLightness(vec3 color, float lightness)
{
    // KoColorSpaceMaths::setLightness/HSY ToneMapping. Keep the original
    // extrema for both corrections, including the case where both apply.
    color += lightness - hsyLightness(color);
    float light = hsyLightness(color);
    float low = min(min(color.r, color.g), color.b);
    float high = max(max(color.r, color.g), color.b);
    if (low < 0.0) {
        float stretch = light - low;
        if (light <= 0.00001 || stretch < 1.192092896e-7) color = vec3(0.0);
        else color = light + ((color - light) * light) * (1.0 / stretch);
    }
    if (high > 1.0) {
        float stretch = high - light;
        if (light > 1.0 || stretch < 1.192092896e-7) color = vec3(1.0);
        else color = light + ((color - light) * (1.0 - light)) * (1.0 / stretch);
    }
    return color;
}

vec3 blendHSY(uint op, vec3 s, vec3 d)
{
    float light = hsyLightness(d);
    switch (op) {
    case OpHue:
        return max(hsySetLightness(hsySetSaturation(s, hsySaturation(d)), light), 0.0);
    case OpSaturation:
        return max(hsySetLightness(hsySetSaturation(d, hsySaturation(s)), light), 0.0);
    case OpColor:
        return max(hsySetLightness(s, light), 0.0);
    case OpLuminosity:
        return max(hsySetLightness(d, hsyLightness(s)), 0.0);
    case OpDarkerColor:
        return light > hsyLightness(s) ? s : d;
    case OpLighterColor:
        return light < hsyLightness(s) ? s : d;
    }
    return d;
}

vec4 compositeHSY(uint op, vec4 dst, vec4 src, float srcAlpha, bool alphaLocked)
{
    // Unlike the separable functor, GenericHSL uses strict alpha comparisons
    // and interpolates the ORIGINAL (unclamped) source/destination colors.
    if (alphaLocked && dst.a == 0.0) return dst;
    float alpha = alphaLocked ? dst.a : blendAlpha(op, srcAlpha, dst.a);
    if (alpha == 0.0) return vec4(dst.rgb, alpha);
    vec3 blended = blendHSY(op, clamp(src.rgb, 0.0, 1.0), clamp(dst.rgb, 0.0, 1.0));
#ifdef TILE_F16
    blended = vec3(f16vec3(blended));
#endif
    if (alphaLocked) return vec4((blended - dst.rgb) * srcAlpha + dst.rgb, alpha);
    vec3 result = blendWeightedColor(op, dst, src, srcAlpha, blended);
    return vec4(result / alpha, alpha);
}

#endif

vec4 compositeGenericColor(uint op, vec4 dst, vec4 src, float srcAlpha, bool alphaLocked)
{
#ifndef BASIC_BLEND_ONLY
    if (op >= OpHue) return compositeHSY(op, dst, src, srcAlpha, alphaLocked);
#endif
    const float dstAlpha = dst.a;
    if (blendZero(op, srcAlpha)) {
        return dst;
    }

    const vec3 s = clampSource(op, src.rgb);
    if (alphaLocked) {
        if (!blendZero(op, dstAlpha)) {
            const vec3 d = clampDestination(op, dst.rgb);
            return vec4(interpolateBlend(op, d, blendColor(op, s, d), srcAlpha), dstAlpha);
        }
        return dst;
    }
    if (blendZero(op, dstAlpha)) {
        return vec4(s, srcAlpha);
    }

    const vec3 d = clampDestination(op, dst.rgb);
    const vec3 cf = blendColor(op, s, d);
    if (blendUnit(op, dstAlpha)) {
        return vec4(interpolateBlend(op, d, cf, srcAlpha), 1.0);
    }
    if (blendUnit(op, srcAlpha)) {
        return vec4(interpolateBlend(op, s, cf, dstAlpha), 1.0);
    }

    const float newAlpha = blendAlpha(op, srcAlpha, dstAlpha);
    if (blendZero(op, newAlpha)) {
        return vec4(dst.rgb, newAlpha);
    }
    const vec3 result = blendWeightedColor(op, vec4(d, dstAlpha), vec4(s, src.a), srcAlpha, cf);
#ifndef BASIC_BLEND_ONLY
    if (op == OpColorBurn) {
        precise vec3 quotient = result / newAlpha;
        vec3 residual = fma(-quotient, vec3(newAlpha), result);
        return vec4(fma(residual, vec3(1.0 / newAlpha), quotient), newAlpha);
    }
#endif
    return vec4(result / newAlpha, newAlpha);
}

vec4 compositeGeneric(uint op, vec4 dst, vec4 src, float opacity, bool alphaLocked, float coverage, uint channels)
{
    alphaLocked = alphaLocked || (channels & 8) == 0;
    // KoCompositeOpBase clears hidden RGB for ANY restricted channel set,
    // before testing source alpha/coverage (including zero coverage).
    if ((alphaLocked || channels != 15) && dst.a == 0.0) dst = vec4(0.0);
    float srcAlpha = (src.a * coverage) * opacity;
#ifndef BASIC_BLEND_ONLY
    if (op == OpColorBurn) {
        precise float alpha = (src.a * coverage) * opacity;
        srcAlpha = alpha;
    }
#endif
#ifdef TILE_F16
    // Generic CPU operations use half alpha, opacity and intermediates for
    // every blend mode, including the established basic shader family.
    srcAlpha = float(float16_t((src.a * float(float16_t(coverage))) * float(float16_t(opacity))));
#endif
    vec4 result = compositeGenericColor(op, dst, src, srcAlpha, alphaLocked);
    if ((channels & 1) == 0) result.r = dst.r;
    if ((channels & 2) == 0) result.g = dst.g;
    if ((channels & 4) == 0) result.b = dst.b;
    return result;
}
