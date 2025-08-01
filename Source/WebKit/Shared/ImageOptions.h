/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <wtf/OptionSet.h>

namespace WebKit {

enum class ImageOption : uint8_t {
    Shareable = 1 << 0,
    // Makes local in process buffer
    Local = 1 << 1,
    Accelerated = 1 << 2,
    AllowHDR = 1 << 3,
};

using ImageOptions = OptionSet<ImageOption>;

enum class SnapshotOption : uint16_t {
    Shareable = 1 << 0,
    ExcludeSelectionHighlighting = 1 << 1,
    InViewCoordinates = 1 << 2,
    PaintSelectionRectangle = 1 << 3,
    ExcludeDeviceScaleFactor = 1 << 5,
    ForceBlackText = 1 << 6,
    ForceWhiteText = 1 << 7,
    Printing = 1 << 8,
    UseScreenColorSpace = 1 << 9,
    VisibleContentRect = 1 << 10,
    FullContentRect = 1 << 11,
    TransparentBackground = 1 << 12,
    // Not supported with takeSnapshotLegacy
    Accelerated = 1 << 13,
    AllowHDR = 1 << 14,
};

using SnapshotOptions = OptionSet<SnapshotOption>;

inline ImageOptions snapshotOptionsToImageOptions(SnapshotOptions snapshotOptions)
{
    if (snapshotOptions.contains(SnapshotOption::Shareable))
        return ImageOption::Shareable;
    if (snapshotOptions.contains(SnapshotOption::Accelerated))
        return ImageOption::Accelerated;
    if (snapshotOptions.contains(SnapshotOption::AllowHDR))
        return ImageOption::AllowHDR;
    return { };
}

} // namespace WebKit
