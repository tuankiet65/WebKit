/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "AccessibilityMockObject.h"
#include "AccessibilityRenderObject.h"

namespace WebCore {

class HTMLInputElement;

class AccessibilitySlider final : public AccessibilityRenderObject {
public:
    static Ref<AccessibilitySlider> create(AXID, RenderObject&, AXObjectCache&);
    virtual ~AccessibilitySlider() = default;

private:
    explicit AccessibilitySlider(AXID, RenderObject&, AXObjectCache&);

    HTMLInputElement* inputElement() const;
    AccessibilityObject* elementAccessibilityHitTest(const IntPoint&) const final;

    AccessibilityRole determineAccessibilityRole() final { return AccessibilityRole::Slider; }

    void addChildren() final;

    bool canSetValueAttribute() const final { return true; }
    
    bool setValue(const String&) final;
    float valueForRange() const final;
    float maxValueForRange() const final;
    float minValueForRange() const final;
    std::optional<AccessibilityOrientation> explicitOrientation() const final;
};

class AccessibilitySliderThumb final : public AccessibilityMockObject {
public:
    static Ref<AccessibilitySliderThumb> create(AXID, AXObjectCache&);
    virtual ~AccessibilitySliderThumb() = default;

    AccessibilityRole determineAccessibilityRole() final { return AccessibilityRole::SliderThumb; }
    LayoutRect elementRect() const final;

private:
    explicit AccessibilitySliderThumb(AXID, AXObjectCache&);

    bool isSliderThumb() const final { return true; }
    bool computeIsIgnored() const final;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::AccessibilitySliderThumb) \
    static bool isType(const WebCore::AccessibilityObject& object) { return object.isSliderThumb(); } \
SPECIALIZE_TYPE_TRAITS_END()
