/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CSSRegisteredCustomProperty.h"
#include "StyleRule.h"
#include <wtf/HashMap.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore {

class RenderStyle;

namespace Style {

class CustomProperty;
class Scope;

class CustomPropertyRegistry {
    WTF_MAKE_TZONE_ALLOCATED(CustomPropertyRegistry);
public:
    CustomPropertyRegistry(Scope&);

    const CSSRegisteredCustomProperty* get(const AtomString&) const;
    bool isInherited(const AtomString&) const;

    bool registerFromAPI(CSSRegisteredCustomProperty&&);
    void registerFromStylesheet(const StyleRuleProperty::Descriptor&);
    void clearRegisteredFromStylesheets();

    const RenderStyle& initialValuePrototypeStyle() const;

    bool invalidatePropertiesWithViewportUnits(Document&);

    enum class ViewportUnitDependency : bool { No, Yes };
    enum class ParseInitialValueError : uint8_t { NotComputationallyIndependent, DidNotParse };
    static Expected<std::pair<RefPtr<const CustomProperty>, ViewportUnitDependency>, ParseInitialValueError> parseInitialValue(const Document&, const AtomString& propertyName, const CSSCustomPropertySyntax&, CSSParserTokenRange);

private:
    void invalidate(const AtomString&);
    void notifyAnimationsOfCustomPropertyRegistration(const AtomString&);

    Scope& m_scope;

    UncheckedKeyHashMap<AtomString, UniqueRef<CSSRegisteredCustomProperty>> m_propertiesFromAPI;
    UncheckedKeyHashMap<AtomString, UniqueRef<CSSRegisteredCustomProperty>> m_propertiesFromStylesheet;

    mutable std::unique_ptr<RenderStyle> m_initialValuePrototypeStyle;
    mutable bool m_hasInvalidPrototypeStyle { false };
};

} // namespace Style
} // namespace WebCore
