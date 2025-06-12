/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "AnchorTracker.h"

#include "Element.h"
#include "Node.h"
#include "RenderBoxModelObject.h"
#include "RenderElementInlines.h"

namespace WebCore::Style {

void AnchorSet::registerAnchor(const RenderBoxModelObject& renderer)
{
    bool isNewAnchor = m_anchors.add(renderer).isNewEntry;
    m_isDirty |= isNewAnchor;
}

void AnchorSet::unregisterAnchor(const RenderBoxModelObject& renderer)
{
    bool isRemoved = m_anchors.remove(renderer);
    m_isDirty |= isRemoved;
}

void AnchorTracker::registerAnchor(const RenderBoxModelObject& renderer, const FixedVector<ScopedName>& anchorNames)
{
    CheckedPtr element = renderer.element();
    ASSERT(element);

    HashSet<ResolvedScopedName> resolveNames;
    for (const auto& name : anchorNames) {
        auto resolvedName = ResolvedScopedName::createFromScopedName(*element, name);
        m_anchors.ensure(resolvedName, [] () {
            return AnchorSet { };
        });
        resolveNames.add(resolvedName);
    }

    // TODO: optimize this.
    for (auto& [name, set] : m_anchors) {
        if (resolveNames.contains(name))
            set.registerAnchor(renderer);
        else
            set.unregisterAnchor(renderer);
    }
}

void AnchorTracker::unregisterAnchor(const RenderBoxModelObject& renderer)
{
    for (auto& [name, set] : m_anchors)
        set.unregisterAnchor(renderer);
}

void AnchorTracker::markAsClean()
{
    for (auto& [name, set] : m_anchors)
        set.markAsClean();
}

Vector<SingleThreadWeakRef<const RenderBoxModelObject>> AnchorTracker::sortedAnchorsWithName(ResolvedScopedName anchorName) const
{
    auto maybeSetIter = m_anchors.find(anchorName);
    if (maybeSetIter == m_anchors.end())
        return { };

    Vector<SingleThreadWeakRef<const RenderBoxModelObject>> sortedAnchors;
    for (const auto& anchor : maybeSetIter->value.anchors())
        sortedAnchors.append(anchor);

    std::ranges::sort(sortedAnchors, [](auto& a, auto& b) {
        // FIXME: Figure out anonymous pseudo-elements.
        // FIXME: Since we already ensure renderer MUST have an associated element, maybe remove this?
        if (!a->element() || !b->element())
            return !!b->element();
        return is_lt(treeOrder<ComposedTree>(*a->element(), *b->element()));
    });

    return sortedAnchors;
}

bool AnchorTracker::anchorNameIsDirty(ResolvedScopedName name) const
{
    auto maybeSetIter = m_anchors.find(name);
    if (maybeSetIter == m_anchors.end())
        return false;

    return maybeSetIter->value.isDirty();
}

} // namespace WebCore::Style
