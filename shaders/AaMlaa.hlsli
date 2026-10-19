// AMD-style Morphological Anti-Aliasing for Veyra's anti-aliasing effect (#53).
// Ported from SAOG0721/Magpie d687997 src/Effects/MLAA/MLAA.hlsl (GPL-3.0),
// adapted there from https://github.com/GPUOpen-LibrariesAndSDKs/MLAA11.
// Veyra changes: edges and shape tests read the perceptual luma texture
// (AntiAlias.hlsli) instead of a luma derived from gamma-encoded colour; the
// blend is a plain lerp of the linear working colour (Magpie's
// sqrt(lerp(c*c, a*a, w)) linearised gamma-encoded input); the three passes are
// separate compute shaders sharing this header.
//
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#ifndef VEYRA_MLAA_HLSLI
#define VEYRA_MLAA_HLSLI
#include "AntiAlias.hlsli"

static const uint MAX_EDGE_LENGTH = 7;
#endif
