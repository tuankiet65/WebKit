/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
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

namespace WebCore {

class Scrollbar;

class AccessibilityScrollbar final : public AccessibilityMockObject {
public:
    static Ref<AccessibilityScrollbar> create(AXID, Scrollbar&, AXObjectCache&);

private:
    explicit AccessibilityScrollbar(AXID, Scrollbar&, AXObjectCache&);

    bool canSetValueAttribute() const final { return true; }

    bool isAccessibilityScrollbar() const final { return true; }
    LayoutRect elementRect() const final;

    AccessibilityRole determineAccessibilityRole() final { return AccessibilityRole::ScrollBar; }
    std::optional<AccessibilityOrientation> explicitOrientation() const final;
    Document* document() const final;
    bool isEnabled() const final;

    // Assumes float [0..1]
    bool setValue(float) final;
    float valueForRange() const final;

    const Ref<Scrollbar> m_scrollbar;
};

} // namespace WebCore


SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::AccessibilityScrollbar) \
    static bool isType(const WebCore::AccessibilityObject& object) { return object.isAccessibilityScrollbar(); } \
SPECIALIZE_TYPE_TRAITS_END()
