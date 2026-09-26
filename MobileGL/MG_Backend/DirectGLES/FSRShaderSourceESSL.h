// MobileGL - MG_Backend/DirectGLES/FSRShaderSourceESSL.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// mg-3backends FSR1 for DirectGLES: Arm Accuracy Super Resolution (FFXM FSR1),
// pre-converted to ESSL 300 es. The desktop-GLSL sources and the conversion
// pipeline live in MobileGlues gl/FSR1/FSRShaderSource.h; these strings are the
// pinned SPIRV-Cross output of exactly those sources (glslang 450 -> SPIR-V ->
// ESSL 300 es), so the GLES-side conversion step is not needed here at all and
// cannot fail the way the old AMD port did. Embedded shaders keep the DirectGLES
// backend free of any shader-transpiler dependency.
//
// Arm ASR: (c) 2023 Advanced Micro Devices, Inc. / (c) 2024-2025 Arm Limited,
// MIT license - https://github.com/arm/accuracy-super-resolution
#pragma once

// Fullscreen quad vertex shader.
const char* FSR_VS_ESSL = R"fsr_glsl(#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec2 aPosition;
out vec2 vTexCoord;

void main()
{
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = (aPosition * 0.5) + vec2(0.5);
}

)fsr_glsl";

// EASU pass: low-res render texture -> upscaled intermediate. Reads uInputTex
// (unit 0) and uEasuCon0..3 (uvec4, CPU-computed).
const char* FSR_EASU_FS_ESSL = R"fsr_glsl(#version 300 es
precision highp float;
precision highp int;

uniform highp sampler2D uInputTex;
uniform uvec4 uEasuCon0;
uniform uvec4 uEasuCon1;
uniform uvec4 uEasuCon2;
uniform uvec4 uEasuCon3;

out highp vec4 oFragColor;
in highp vec2 vTexCoord;

highp vec2 ffxAsFloat(uvec2 x)
{
    return uintBitsToFloat(x);
}

highp vec4 asrGatherR(highp vec2 p)
{
    ivec2 ts = textureSize(uInputTex, 0);
    ivec2 t0 = ivec2(floor((p * vec2(ts)) - vec2(0.5)));
    ivec2 lo = ivec2(0);
    ivec2 hi = ts - ivec2(1);
    return vec4(texelFetch(uInputTex, clamp(t0 + ivec2(0, 1), lo, hi), 0).x, texelFetch(uInputTex, clamp(t0 + ivec2(1), lo, hi), 0).x, texelFetch(uInputTex, clamp(t0 + ivec2(0), lo, hi), 0).x, texelFetch(uInputTex, clamp(t0 + ivec2(1, 0), lo, hi), 0).x);
}

highp vec4 FsrEasuRF(highp vec2 p)
{
    highp vec2 param = p;
    return asrGatherR(param);
}

highp vec4 FsrEasuGF(highp vec2 p)
{
    ivec2 ts = textureSize(uInputTex, 0);
    ivec2 t0 = ivec2(floor((p * vec2(ts)) - vec2(0.5)));
    ivec2 lo = ivec2(0);
    ivec2 hi = ts - ivec2(1);
    return vec4(texelFetch(uInputTex, clamp(t0 + ivec2(0, 1), lo, hi), 0).y, texelFetch(uInputTex, clamp(t0 + ivec2(1), lo, hi), 0).y, texelFetch(uInputTex, clamp(t0 + ivec2(0), lo, hi), 0).y, texelFetch(uInputTex, clamp(t0 + ivec2(1, 0), lo, hi), 0).y);
}

highp vec4 FsrEasuBF(highp vec2 p)
{
    ivec2 ts = textureSize(uInputTex, 0);
    ivec2 t0 = ivec2(floor((p * vec2(ts)) - vec2(0.5)));
    ivec2 lo = ivec2(0);
    ivec2 hi = ts - ivec2(1);
    return vec4(texelFetch(uInputTex, clamp(t0 + ivec2(0, 1), lo, hi), 0).z, texelFetch(uInputTex, clamp(t0 + ivec2(1), lo, hi), 0).z, texelFetch(uInputTex, clamp(t0 + ivec2(0), lo, hi), 0).z, texelFetch(uInputTex, clamp(t0 + ivec2(1, 0), lo, hi), 0).z);
}

highp vec4 ffxBroadcast4(highp float value)
{
    return vec4(value);
}

highp vec2 ffxBroadcast2(highp float value)
{
    return vec2(value);
}

uint ffxAsUInt32(highp float x)
{
    return floatBitsToUint(x);
}

highp float ffxAsFloat(uint x)
{
    return uintBitsToFloat(x);
}

highp float ffxApproximateReciprocal(highp float value)
{
    highp float param = value;
    uint param_1 = 2129690299u - ffxAsUInt32(param);
    return ffxAsFloat(param_1);
}

highp float ffxSaturate(highp float x)
{
    return clamp(x, 0.0, 1.0);
}

void fsrEasuSetFloat(inout highp vec2 direction, inout highp float _length, highp vec2 pp, bool biS, bool biT, bool biU, bool biV, highp float lA, highp float lB, highp float lC, highp float lD, highp float lE)
{
    highp float weight = 0.0;
    if (biS)
    {
        weight = (1.0 - pp.x) * (1.0 - pp.y);
    }
    if (biT)
    {
        weight = pp.x * (1.0 - pp.y);
    }
    if (biU)
    {
        weight = (1.0 - pp.x) * pp.y;
    }
    if (biV)
    {
        weight = pp.x * pp.y;
    }
    highp float dc = lD - lC;
    highp float cb = lC - lB;
    highp float lengthX = max(abs(dc), abs(cb));
    highp float param = lengthX;
    lengthX = ffxApproximateReciprocal(param);
    highp float directionX = lD - lB;
    direction.x += (directionX * weight);
    highp float param_1 = abs(directionX) * lengthX;
    lengthX = ffxSaturate(param_1);
    lengthX *= lengthX;
    _length += (lengthX * weight);
    highp float ec = lE - lC;
    highp float ca = lC - lA;
    highp float lengthY = max(abs(ec), abs(ca));
    highp float param_2 = lengthY;
    lengthY = ffxApproximateReciprocal(param_2);
    highp float directionY = lE - lA;
    direction.y += (directionY * weight);
    highp float param_3 = abs(directionY) * lengthY;
    lengthY = ffxSaturate(param_3);
    lengthY *= lengthY;
    _length += (lengthY * weight);
}

highp float ffxApproximateReciprocalSquareRoot(highp float value)
{
    highp float param = value;
    uint param_1 = 1597275508u - (ffxAsUInt32(param) >> 1u);
    return ffxAsFloat(param_1);
}

highp vec3 ffxMin3(highp vec3 x, highp vec3 y, highp vec3 z)
{
    return min(x, min(y, z));
}

highp vec3 ffxMin(highp vec3 x, highp vec3 y)
{
    return min(x, y);
}

highp vec3 ffxMax3(highp vec3 x, highp vec3 y, highp vec3 z)
{
    return max(x, max(y, z));
}

highp vec3 ffxBroadcast3(highp float value)
{
    return vec3(value);
}

highp float ffxMin(highp float x, highp float y)
{
    return min(x, y);
}

void fsrEasuTapFloat(inout highp vec3 accumulatedColor, inout highp float accumulatedWeight, highp vec2 pixelOffset, highp vec2 gradientDirection, highp vec2 _length, highp float negativeLobeStrength, highp float clippingPoint, highp vec3 color)
{
    highp vec2 rotatedOffset;
    rotatedOffset.x = (pixelOffset.x * gradientDirection.x) + (pixelOffset.y * gradientDirection.y);
    rotatedOffset.y = (pixelOffset.x * (-gradientDirection.y)) + (pixelOffset.y * gradientDirection.x);
    rotatedOffset *= _length;
    highp float distanceSquared = (rotatedOffset.x * rotatedOffset.x) + (rotatedOffset.y * rotatedOffset.y);
    highp float param = distanceSquared;
    highp float param_1 = clippingPoint;
    distanceSquared = ffxMin(param, param_1);
    highp float weightB = (0.4000000059604644775390625 * distanceSquared) + (-1.0);
    highp float weightA = (negativeLobeStrength * distanceSquared) + (-1.0);
    weightB *= weightB;
    weightA *= weightA;
    weightB = (1.5625 * weightB) + (-0.5625);
    highp float weight = weightB * weightA;
    accumulatedColor += (color * weight);
    accumulatedWeight += weight;
}

highp float rcp(highp float x)
{
    return 1.0 / x;
}

void ffxFsrEasuFloat(out highp vec3 pix, uvec2 ip, uvec4 con0, uvec4 con1, uvec4 con2, uvec4 con3)
{
    uvec2 param = con0.xy;
    uvec2 param_1 = con0.zw;
    highp vec2 pp = (vec2(ip) * ffxAsFloat(param)) + ffxAsFloat(param_1);
    highp vec2 fp = floor(pp);
    pp -= fp;
    uvec2 param_2 = con1.xy;
    uvec2 param_3 = con1.zw;
    highp vec2 p0 = (fp * ffxAsFloat(param_2)) + ffxAsFloat(param_3);
    uvec2 param_4 = con2.xy;
    highp vec2 p1 = p0 + ffxAsFloat(param_4);
    uvec2 param_5 = con2.zw;
    highp vec2 p2 = p0 + ffxAsFloat(param_5);
    uvec2 param_6 = con3.xy;
    highp vec2 p3 = p0 + ffxAsFloat(param_6);
    highp vec2 param_7 = p0;
    highp vec4 bczzR = FsrEasuRF(param_7);
    highp vec2 param_8 = p0;
    highp vec4 bczzG = FsrEasuGF(param_8);
    highp vec2 param_9 = p0;
    highp vec4 bczzB = FsrEasuBF(param_9);
    highp vec2 param_10 = p1;
    highp vec4 ijfeR = FsrEasuRF(param_10);
    highp vec2 param_11 = p1;
    highp vec4 ijfeG = FsrEasuGF(param_11);
    highp vec2 param_12 = p1;
    highp vec4 ijfeB = FsrEasuBF(param_12);
    highp vec2 param_13 = p2;
    highp vec4 klhgR = FsrEasuRF(param_13);
    highp vec2 param_14 = p2;
    highp vec4 klhgG = FsrEasuGF(param_14);
    highp vec2 param_15 = p2;
    highp vec4 klhgB = FsrEasuBF(param_15);
    highp vec2 param_16 = p3;
    highp vec4 zzonR = FsrEasuRF(param_16);
    highp vec2 param_17 = p3;
    highp vec4 zzonG = FsrEasuGF(param_17);
    highp vec2 param_18 = p3;
    highp vec4 zzonB = FsrEasuBF(param_18);
    highp float param_19 = 0.5;
    highp float param_20 = 0.5;
    highp vec4 bczzL = (bczzB * ffxBroadcast4(param_19)) + ((bczzR * ffxBroadcast4(param_20)) + bczzG);
    highp float param_21 = 0.5;
    highp float param_22 = 0.5;
    highp vec4 ijfeL = (ijfeB * ffxBroadcast4(param_21)) + ((ijfeR * ffxBroadcast4(param_22)) + ijfeG);
    highp float param_23 = 0.5;
    highp float param_24 = 0.5;
    highp vec4 klhgL = (klhgB * ffxBroadcast4(param_23)) + ((klhgR * ffxBroadcast4(param_24)) + klhgG);
    highp float param_25 = 0.5;
    highp float param_26 = 0.5;
    highp vec4 zzonL = (zzonB * ffxBroadcast4(param_25)) + ((zzonR * ffxBroadcast4(param_26)) + zzonG);
    highp float bL = bczzL.x;
    highp float cL = bczzL.y;
    highp float iL = ijfeL.x;
    highp float jL = ijfeL.y;
    highp float fL = ijfeL.z;
    highp float eL = ijfeL.w;
    highp float kL = klhgL.x;
    highp float lL = klhgL.y;
    highp float hL = klhgL.z;
    highp float gL = klhgL.w;
    highp float oL = zzonL.z;
    highp float nL = zzonL.w;
    highp float param_27 = 0.0;
    highp vec2 dir = ffxBroadcast2(param_27);
    highp float len = 0.0;
    highp vec2 param_28 = dir;
    highp float param_29 = len;
    highp vec2 param_30 = pp;
    bool param_31 = true;
    bool param_32 = false;
    bool param_33 = false;
    bool param_34 = false;
    highp float param_35 = bL;
    highp float param_36 = eL;
    highp float param_37 = fL;
    highp float param_38 = gL;
    highp float param_39 = jL;
    fsrEasuSetFloat(param_28, param_29, param_30, param_31, param_32, param_33, param_34, param_35, param_36, param_37, param_38, param_39);
    dir = param_28;
    len = param_29;
    highp vec2 param_40 = dir;
    highp float param_41 = len;
    highp vec2 param_42 = pp;
    bool param_43 = false;
    bool param_44 = true;
    bool param_45 = false;
    bool param_46 = false;
    highp float param_47 = cL;
    highp float param_48 = fL;
    highp float param_49 = gL;
    highp float param_50 = hL;
    highp float param_51 = kL;
    fsrEasuSetFloat(param_40, param_41, param_42, param_43, param_44, param_45, param_46, param_47, param_48, param_49, param_50, param_51);
    dir = param_40;
    len = param_41;
    highp vec2 param_52 = dir;
    highp float param_53 = len;
    highp vec2 param_54 = pp;
    bool param_55 = false;
    bool param_56 = false;
    bool param_57 = true;
    bool param_58 = false;
    highp float param_59 = fL;
    highp float param_60 = iL;
    highp float param_61 = jL;
    highp float param_62 = kL;
    highp float param_63 = nL;
    fsrEasuSetFloat(param_52, param_53, param_54, param_55, param_56, param_57, param_58, param_59, param_60, param_61, param_62, param_63);
    dir = param_52;
    len = param_53;
    highp vec2 param_64 = dir;
    highp float param_65 = len;
    highp vec2 param_66 = pp;
    bool param_67 = false;
    bool param_68 = false;
    bool param_69 = false;
    bool param_70 = true;
    highp float param_71 = gL;
    highp float param_72 = jL;
    highp float param_73 = kL;
    highp float param_74 = lL;
    highp float param_75 = oL;
    fsrEasuSetFloat(param_64, param_65, param_66, param_67, param_68, param_69, param_70, param_71, param_72, param_73, param_74, param_75);
    dir = param_64;
    len = param_65;
    highp vec2 dir2 = dir * dir;
    highp float dirR = dir2.x + dir2.y;
    bool zro = dirR < 3.0517578125e-05;
    highp float param_76 = dirR;
    dirR = ffxApproximateReciprocalSquareRoot(param_76);
    dirR = zro ? 1.0 : dirR;
    highp float _714;
    if (zro)
    {
        _714 = 1.0;
    }
    else
    {
        _714 = dir.x;
    }
    dir.x = _714;
    highp float param_77 = dirR;
    dir *= ffxBroadcast2(param_77);
    len *= 0.5;
    len *= len;
    highp float param_78 = max(abs(dir.x), abs(dir.y));
    highp float stretch = ((dir.x * dir.x) + (dir.y * dir.y)) * ffxApproximateReciprocal(param_78);
    highp vec2 len2 = vec2(1.0 + ((stretch - 1.0) * len), 1.0 + ((-0.5) * len));
    highp float lob = 0.5 + ((-0.2899999916553497314453125) * len);
    highp float param_79 = lob;
    highp float clp = ffxApproximateReciprocal(param_79);
    highp vec3 param_80 = vec3(ijfeR.z, ijfeG.z, ijfeB.z);
    highp vec3 param_81 = vec3(klhgR.w, klhgG.w, klhgB.w);
    highp vec3 param_82 = vec3(ijfeR.y, ijfeG.y, ijfeB.y);
    highp vec3 param_83 = ffxMin3(param_80, param_81, param_82);
    highp vec3 param_84 = vec3(klhgR.x, klhgG.x, klhgB.x);
    highp vec3 min4 = ffxMin(param_83, param_84);
    highp vec3 param_85 = vec3(ijfeR.z, ijfeG.z, ijfeB.z);
    highp vec3 param_86 = vec3(klhgR.w, klhgG.w, klhgB.w);
    highp vec3 param_87 = vec3(ijfeR.y, ijfeG.y, ijfeB.y);
    highp vec3 max4 = max(ffxMax3(param_85, param_86, param_87), vec3(klhgR.x, klhgG.x, klhgB.x));
    highp float param_88 = 0.0;
    highp vec3 aC = ffxBroadcast3(param_88);
    highp float aW = 0.0;
    highp vec3 param_89 = aC;
    highp float param_90 = aW;
    highp vec2 param_91 = vec2(0.0, -1.0) - pp;
    highp vec2 param_92 = dir;
    highp vec2 param_93 = len2;
    highp float param_94 = lob;
    highp float param_95 = clp;
    highp vec3 param_96 = vec3(bczzR.x, bczzG.x, bczzB.x);
    fsrEasuTapFloat(param_89, param_90, param_91, param_92, param_93, param_94, param_95, param_96);
    aC = param_89;
    aW = param_90;
    highp vec3 param_97 = aC;
    highp float param_98 = aW;
    highp vec2 param_99 = vec2(1.0, -1.0) - pp;
    highp vec2 param_100 = dir;
    highp vec2 param_101 = len2;
    highp float param_102 = lob;
    highp float param_103 = clp;
    highp vec3 param_104 = vec3(bczzR.y, bczzG.y, bczzB.y);
    fsrEasuTapFloat(param_97, param_98, param_99, param_100, param_101, param_102, param_103, param_104);
    aC = param_97;
    aW = param_98;
    highp vec3 param_105 = aC;
    highp float param_106 = aW;
    highp vec2 param_107 = vec2(-1.0, 1.0) - pp;
    highp vec2 param_108 = dir;
    highp vec2 param_109 = len2;
    highp float param_110 = lob;
    highp float param_111 = clp;
    highp vec3 param_112 = vec3(ijfeR.x, ijfeG.x, ijfeB.x);
    fsrEasuTapFloat(param_105, param_106, param_107, param_108, param_109, param_110, param_111, param_112);
    aC = param_105;
    aW = param_106;
    highp vec3 param_113 = aC;
    highp float param_114 = aW;
    highp vec2 param_115 = vec2(0.0, 1.0) - pp;
    highp vec2 param_116 = dir;
    highp vec2 param_117 = len2;
    highp float param_118 = lob;
    highp float param_119 = clp;
    highp vec3 param_120 = vec3(ijfeR.y, ijfeG.y, ijfeB.y);
    fsrEasuTapFloat(param_113, param_114, param_115, param_116, param_117, param_118, param_119, param_120);
    aC = param_113;
    aW = param_114;
    highp vec3 param_121 = aC;
    highp float param_122 = aW;
    highp vec2 param_123 = vec2(0.0) - pp;
    highp vec2 param_124 = dir;
    highp vec2 param_125 = len2;
    highp float param_126 = lob;
    highp float param_127 = clp;
    highp vec3 param_128 = vec3(ijfeR.z, ijfeG.z, ijfeB.z);
    fsrEasuTapFloat(param_121, param_122, param_123, param_124, param_125, param_126, param_127, param_128);
    aC = param_121;
    aW = param_122;
    highp vec3 param_129 = aC;
    highp float param_130 = aW;
    highp vec2 param_131 = vec2(-1.0, 0.0) - pp;
    highp vec2 param_132 = dir;
    highp vec2 param_133 = len2;
    highp float param_134 = lob;
    highp float param_135 = clp;
    highp vec3 param_136 = vec3(ijfeR.w, ijfeG.w, ijfeB.w);
    fsrEasuTapFloat(param_129, param_130, param_131, param_132, param_133, param_134, param_135, param_136);
    aC = param_129;
    aW = param_130;
    highp vec3 param_137 = aC;
    highp float param_138 = aW;
    highp vec2 param_139 = vec2(1.0) - pp;
    highp vec2 param_140 = dir;
    highp vec2 param_141 = len2;
    highp float param_142 = lob;
    highp float param_143 = clp;
    highp vec3 param_144 = vec3(klhgR.x, klhgG.x, klhgB.x);
    fsrEasuTapFloat(param_137, param_138, param_139, param_140, param_141, param_142, param_143, param_144);
    aC = param_137;
    aW = param_138;
    highp vec3 param_145 = aC;
    highp float param_146 = aW;
    highp vec2 param_147 = vec2(2.0, 1.0) - pp;
    highp vec2 param_148 = dir;
    highp vec2 param_149 = len2;
    highp float param_150 = lob;
    highp float param_151 = clp;
    highp vec3 param_152 = vec3(klhgR.y, klhgG.y, klhgB.y);
    fsrEasuTapFloat(param_145, param_146, param_147, param_148, param_149, param_150, param_151, param_152);
    aC = param_145;
    aW = param_146;
    highp vec3 param_153 = aC;
    highp float param_154 = aW;
    highp vec2 param_155 = vec2(2.0, 0.0) - pp;
    highp vec2 param_156 = dir;
    highp vec2 param_157 = len2;
    highp float param_158 = lob;
    highp float param_159 = clp;
    highp vec3 param_160 = vec3(klhgR.z, klhgG.z, klhgB.z);
    fsrEasuTapFloat(param_153, param_154, param_155, param_156, param_157, param_158, param_159, param_160);
    aC = param_153;
    aW = param_154;
    highp vec3 param_161 = aC;
    highp float param_162 = aW;
    highp vec2 param_163 = vec2(1.0, 0.0) - pp;
    highp vec2 param_164 = dir;
    highp vec2 param_165 = len2;
    highp float param_166 = lob;
    highp float param_167 = clp;
    highp vec3 param_168 = vec3(klhgR.w, klhgG.w, klhgB.w);
    fsrEasuTapFloat(param_161, param_162, param_163, param_164, param_165, param_166, param_167, param_168);
    aC = param_161;
    aW = param_162;
    highp vec3 param_169 = aC;
    highp float param_170 = aW;
    highp vec2 param_171 = vec2(1.0, 2.0) - pp;
    highp vec2 param_172 = dir;
    highp vec2 param_173 = len2;
    highp float param_174 = lob;
    highp float param_175 = clp;
    highp vec3 param_176 = vec3(zzonR.z, zzonG.z, zzonB.z);
    fsrEasuTapFloat(param_169, param_170, param_171, param_172, param_173, param_174, param_175, param_176);
    aC = param_169;
    aW = param_170;
    highp vec3 param_177 = aC;
    highp float param_178 = aW;
    highp vec2 param_179 = vec2(0.0, 2.0) - pp;
    highp vec2 param_180 = dir;
    highp vec2 param_181 = len2;
    highp float param_182 = lob;
    highp float param_183 = clp;
    highp vec3 param_184 = vec3(zzonR.w, zzonG.w, zzonB.w);
    fsrEasuTapFloat(param_177, param_178, param_179, param_180, param_181, param_182, param_183, param_184);
    aC = param_177;
    aW = param_178;
    highp float param_185 = aW;
    highp float param_186 = rcp(param_185);
    highp vec3 param_187 = max4;
    highp vec3 param_188 = max(min4, aC * ffxBroadcast3(param_186));
    pix = ffxMin(param_187, param_188);
}

void main()
{
    uvec2 param_1 = uvec2(gl_FragCoord.xy);
    uvec4 param_2 = uEasuCon0;
    uvec4 param_3 = uEasuCon1;
    uvec4 param_4 = uEasuCon2;
    uvec4 param_5 = uEasuCon3;
    highp vec3 param;
    ffxFsrEasuFloat(param, param_1, param_2, param_3, param_4, param_5);
    highp vec3 color = param;
    oFragColor = vec4(color, 1.0);
}
)fsr_glsl";

// RCAS pass: sharpened intermediate -> surface. Reads uInputTex (unit 0) and
// uRcasCon (uvec4, CPU-computed).
const char* FSR_RCAS_FS_ESSL = R"fsr_glsl(#version 300 es
precision highp float;
precision highp int;

uniform highp sampler2D uInputTex;
uniform uvec4 uRcasCon;

out highp vec4 oFragColor;
in highp vec2 vTexCoord;

highp vec4 FsrRcasLoadF(ivec2 p)
{
    return texelFetch(uInputTex, p, 0);
}

void FsrRcasInputF(highp float r, highp float g, highp float b)
{
}

highp float ffxMax3(highp float x, highp float y, highp float z)
{
    return max(x, max(y, z));
}

highp float ffxMin3(highp float x, highp float y, highp float z)
{
    return min(x, min(y, z));
}

uint ffxAsUInt32(highp float x)
{
    return floatBitsToUint(x);
}

highp float ffxAsFloat(uint x)
{
    return uintBitsToFloat(x);
}

highp float ffxApproximateReciprocalMedium(highp float value)
{
    highp float param = value;
    uint param_1 = 2129764351u - ffxAsUInt32(param);
    highp float b = ffxAsFloat(param_1);
    return b * (((-b) * value) + 2.0);
}

highp float ffxSaturate(highp float x)
{
    return clamp(x, 0.0, 1.0);
}

highp float ffxMin(highp float x, highp float y)
{
    return min(x, y);
}

highp float rcp(highp float x)
{
    return 1.0 / x;
}

void FsrRcasF(out highp float pixR, out highp float pixG, out highp float pixB, uvec2 ip, uvec4 con)
{
    ivec2 sp = ivec2(ip);
    ivec2 param = sp + ivec2(0, -1);
    highp vec3 b = FsrRcasLoadF(param).xyz;
    ivec2 param_1 = sp + ivec2(-1, 0);
    highp vec3 d = FsrRcasLoadF(param_1).xyz;
    ivec2 param_2 = sp;
    highp vec3 e = FsrRcasLoadF(param_2).xyz;
    ivec2 param_3 = sp + ivec2(1, 0);
    highp vec3 f = FsrRcasLoadF(param_3).xyz;
    ivec2 param_4 = sp + ivec2(0, 1);
    highp vec3 h = FsrRcasLoadF(param_4).xyz;
    highp float bR = b.x;
    highp float bG = b.y;
    highp float bB = b.z;
    highp float dR = d.x;
    highp float dG = d.y;
    highp float dB = d.z;
    highp float eR = e.x;
    highp float eG = e.y;
    highp float eB = e.z;
    highp float fR = f.x;
    highp float fG = f.y;
    highp float fB = f.z;
    highp float hR = h.x;
    highp float hG = h.y;
    highp float hB = h.z;
    highp float param_5 = bR;
    highp float param_6 = bG;
    highp float param_7 = bB;
    FsrRcasInputF(param_5, param_6, param_7);
    bR = param_5;
    bG = param_6;
    bB = param_7;
    highp float param_8 = dR;
    highp float param_9 = dG;
    highp float param_10 = dB;
    FsrRcasInputF(param_8, param_9, param_10);
    dR = param_8;
    dG = param_9;
    dB = param_10;
    highp float param_11 = eR;
    highp float param_12 = eG;
    highp float param_13 = eB;
    FsrRcasInputF(param_11, param_12, param_13);
    eR = param_11;
    eG = param_12;
    eB = param_13;
    highp float param_14 = fR;
    highp float param_15 = fG;
    highp float param_16 = fB;
    FsrRcasInputF(param_14, param_15, param_16);
    fR = param_14;
    fG = param_15;
    fB = param_16;
    highp float param_17 = hR;
    highp float param_18 = hG;
    highp float param_19 = hB;
    FsrRcasInputF(param_17, param_18, param_19);
    hR = param_17;
    hG = param_18;
    hB = param_19;
    highp float bL = (bB * 0.5) + ((bR * 0.5) + bG);
    highp float dL = (dB * 0.5) + ((dR * 0.5) + dG);
    highp float eL = (eB * 0.5) + ((eR * 0.5) + eG);
    highp float fL = (fB * 0.5) + ((fR * 0.5) + fG);
    highp float hL = (hB * 0.5) + ((hR * 0.5) + hG);
    highp float nz = ((((0.25 * bL) + (0.25 * dL)) + (0.25 * fL)) + (0.25 * hL)) - eL;
    highp float param_20 = bL;
    highp float param_21 = dL;
    highp float param_22 = eL;
    highp float param_23 = ffxMax3(param_20, param_21, param_22);
    highp float param_24 = fL;
    highp float param_25 = hL;
    highp float param_26 = bL;
    highp float param_27 = dL;
    highp float param_28 = eL;
    highp float param_29 = ffxMin3(param_26, param_27, param_28);
    highp float param_30 = fL;
    highp float param_31 = hL;
    highp float param_32 = ffxMax3(param_23, param_24, param_25) - ffxMin3(param_29, param_30, param_31);
    highp float param_33 = abs(nz) * ffxApproximateReciprocalMedium(param_32);
    nz = ffxSaturate(param_33);
    nz = ((-0.5) * nz) + 1.0;
    highp float param_34 = bR;
    highp float param_35 = dR;
    highp float param_36 = fR;
    highp float param_37 = ffxMin3(param_34, param_35, param_36);
    highp float param_38 = hR;
    highp float mn4R = ffxMin(param_37, param_38);
    highp float param_39 = bG;
    highp float param_40 = dG;
    highp float param_41 = fG;
    highp float param_42 = ffxMin3(param_39, param_40, param_41);
    highp float param_43 = hG;
    highp float mn4G = ffxMin(param_42, param_43);
    highp float param_44 = bB;
    highp float param_45 = dB;
    highp float param_46 = fB;
    highp float param_47 = ffxMin3(param_44, param_45, param_46);
    highp float param_48 = hB;
    highp float mn4B = ffxMin(param_47, param_48);
    highp float param_49 = bR;
    highp float param_50 = dR;
    highp float param_51 = fR;
    highp float mx4R = max(ffxMax3(param_49, param_50, param_51), hR);
    highp float param_52 = bG;
    highp float param_53 = dG;
    highp float param_54 = fG;
    highp float mx4G = max(ffxMax3(param_52, param_53, param_54), hG);
    highp float param_55 = bB;
    highp float param_56 = dB;
    highp float param_57 = fB;
    highp float mx4B = max(ffxMax3(param_55, param_56, param_57), hB);
    highp vec2 peakC = vec2(1.0, -4.0);
    highp float param_58 = 4.0 * mx4R;
    highp float hitMinR = mn4R * rcp(param_58);
    highp float param_59 = 4.0 * mx4G;
    highp float hitMinG = mn4G * rcp(param_59);
    highp float param_60 = 4.0 * mx4B;
    highp float hitMinB = mn4B * rcp(param_60);
    highp float param_61 = (4.0 * mn4R) + peakC.y;
    highp float hitMaxR = (peakC.x - mx4R) * rcp(param_61);
    highp float param_62 = (4.0 * mn4G) + peakC.y;
    highp float hitMaxG = (peakC.x - mx4G) * rcp(param_62);
    highp float param_63 = (4.0 * mn4B) + peakC.y;
    highp float hitMaxB = (peakC.x - mx4B) * rcp(param_63);
    highp float lobeR = max(-hitMinR, hitMaxR);
    highp float lobeG = max(-hitMinG, hitMaxG);
    highp float lobeB = max(-hitMinB, hitMaxB);
    highp float param_64 = lobeR;
    highp float param_65 = lobeG;
    highp float param_66 = lobeB;
    highp float param_67 = ffxMax3(param_64, param_65, param_66);
    highp float param_68 = 0.0;
    uint param_69 = con.x;
    highp float lobe = max(-0.1875, ffxMin(param_67, param_68)) * ffxAsFloat(param_69);
    highp float param_70 = (4.0 * lobe) + 1.0;
    highp float rcpL = ffxApproximateReciprocalMedium(param_70);
    pixR = (((((lobe * bR) + (lobe * dR)) + (lobe * hR)) + (lobe * fR)) + eR) * rcpL;
    pixG = (((((lobe * bG) + (lobe * dG)) + (lobe * hG)) + (lobe * fG)) + eG) * rcpL;
    pixB = (((((lobe * bB) + (lobe * dB)) + (lobe * hB)) + (lobe * fB)) + eB) * rcpL;
}

void main()
{
    uvec2 param_3 = uvec2(gl_FragCoord.xy);
    uvec4 param_4 = uRcasCon;
    highp float param;
    highp float param_1;
    highp float param_2;
    FsrRcasF(param, param_1, param_2, param_3, param_4);
    highp float pixR = param;
    highp float pixG = param_1;
    highp float pixB = param_2;
    oFragColor = vec4(pixR, pixG, pixB, 1.0);
}
)fsr_glsl";
