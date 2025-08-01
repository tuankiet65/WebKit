/*
 * Copyright (C) 2004, 2005, 2007 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2007 Rob Buis <buis@kde.org>
 * Copyright (C) 2009-2018 Google, Inc. All rights reserved.
 * Copyright (C) 2009-2025 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include "FloatRect.h"
#include "RenderReplaced.h"
#include "SVGRenderSupport.h"
#include <wtf/WeakHashSet.h>

namespace WebCore {

class AffineTransform;
class LegacyRenderSVGResourceContainer;
class SVGSVGElement;

class LegacyRenderSVGRoot final : public RenderReplaced {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(LegacyRenderSVGRoot);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(LegacyRenderSVGRoot);
public:
    LegacyRenderSVGRoot(SVGSVGElement&, RenderStyle&&);
    virtual ~LegacyRenderSVGRoot();

    SVGSVGElement& svgSVGElement() const;
    Ref<SVGSVGElement> protectedSVGSVGElement() const;

    bool isEmbeddedThroughSVGImage() const;
    bool isEmbeddedThroughFrameContainingSVGDocument() const;

    std::pair<FloatSize, FloatSize> computeIntrinsicSizeAndPreferredAspectRatio() const override;
    bool hasIntrinsicAspectRatio() const final;

    bool isLayoutSizeChanged() const { return m_isLayoutSizeChanged; }
    bool isInLayout() const { return m_inLayout; }
    void setNeedsBoundariesUpdate() override { m_needsBoundariesOrTransformUpdate = true; }
    void setNeedsTransformUpdate() override { m_needsBoundariesOrTransformUpdate = true; }

    IntSize containerSize() const { return m_containerSize; }
    void setContainerSize(const IntSize& containerSize) { m_containerSize = containerSize; }

    bool hasRelativeDimensions() const override;

    // localToBorderBoxTransform maps local SVG viewport coordinates to local CSS box coordinates.  
    const AffineTransform& localToBorderBoxTransform() const { return m_localToBorderBoxTransform; }

    // The flag is cleared at the beginning of each layout() pass. Elements then call this
    // method during layout when they are invalidated by a filter.
    static void addResourceForClientInvalidation(LegacyRenderSVGResourceContainer*);

private:
    void element() const = delete;

    // Intentially left 'RenderSVGRoot' instead of 'LegacyRenderSVGRoot', to avoid breaking layout tests.
    ASCIILiteral renderName() const override { return "RenderSVGRoot"_s; }

    LayoutUnit computeReplacedLogicalWidth(ShouldComputePreferred  = ShouldComputePreferred::ComputeActual) const override;
    LayoutUnit computeReplacedLogicalHeight(std::optional<LayoutUnit> estimatedUsedWidth = std::nullopt) const override;
    void layout() override;
    void paintReplaced(PaintInfo&, const LayoutPoint&) override;

    void willBeDestroyed() override;

    void insertedIntoTree() override;
    void willBeRemovedFromTree() override;

    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) override;

    const AffineTransform& localToParentTransform() const override;

    FloatRect objectBoundingBox() const override { return m_objectBoundingBox.value_or(FloatRect()); }
    FloatRect strokeBoundingBox() const override;
    FloatRect repaintRectInLocalCoordinates(RepaintRectCalculation = RepaintRectCalculation::Fast) const override;

    bool nodeAtPoint(const HitTestRequest&, HitTestResult&, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestAction) override;

    LayoutRect clippedOverflowRect(const RenderLayerModelObject* repaintContainer, VisibleRectContext) const override;
    RepaintRects rectsForRepaintingAfterLayout(const RenderLayerModelObject* repaintContainer, RepaintOutlineBounds) const override;

    LayoutRect localClippedOverflowRect(RepaintRectCalculation) const;

    LayoutRect computeContentsInkOverflow() const;

    std::optional<FloatRect> computeFloatVisibleRectInContainer(const FloatRect&, const RenderLayerModelObject* container, VisibleRectContext) const override;

    void mapLocalToContainer(const RenderLayerModelObject* ancestorContainer, TransformState&, OptionSet<MapCoordinatesMode>, bool* wasFixed) const override;
    const RenderElement* pushMappingToContainer(const RenderLayerModelObject* ancestorToStopAt, RenderGeometryMap&) const override;

    bool canBeSelectionLeaf() const override { return false; }
    bool canHaveChildren() const override { return true; }

    bool shouldApplyViewportClip() const;
    void updateCachedBoundaries();
    void buildLocalToBorderBoxTransform();

    FloatSize calculateIntrinsicSize() const;

    IntSize m_containerSize;
    FloatRect m_repaintBoundingBox;
    Markable<FloatRect> m_objectBoundingBox;
    mutable Markable<FloatRect> m_strokeBoundingBox;
    mutable Markable<FloatRect> m_accurateRepaintBoundingBox;
    mutable AffineTransform m_localToParentTransform;
    AffineTransform m_localToBorderBoxTransform;
    SingleThreadWeakHashSet<LegacyRenderSVGResourceContainer> m_resourcesNeedingToInvalidateClients;

    bool m_inLayout { false };
    bool m_isLayoutSizeChanged : 1 { false };
    bool m_needsBoundariesOrTransformUpdate : 1 { true };
    bool m_hasBoxDecorations : 1 { false };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(LegacyRenderSVGRoot, isLegacyRenderSVGRoot())
