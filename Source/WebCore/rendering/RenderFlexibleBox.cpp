/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RenderFlexibleBox.h"

#include "BaselineAlignment.h"
#include "FontBaseline.h"
#include "HitTestResult.h"
#include "InspectorInstrumentation.h"
#include "LayoutIntegrationCoverage.h"
#include "LayoutIntegrationFlexLayout.h"
#include "LayoutRepainter.h"
#include "LayoutUnit.h"
#include "RenderBlockInlines.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderChildIterator.h"
#include "RenderElementInlines.h"
#include "RenderLayer.h"
#include "RenderLayoutState.h"
#include "RenderObjectEnums.h"
#include "RenderObjectInlines.h"
#include "RenderReplaced.h"
#include "RenderSVGRoot.h"
#include "RenderStyleConstants.h"
#include "RenderTable.h"
#include "RenderView.h"
#include "StylePrimitiveNumericTypes+Evaluation.h"
#include "WritingMode.h"
#include <limits>
#include <wtf/MathExtras.h>
#include <wtf/SetForScope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/TypeCasts.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderFlexibleBox);

struct RenderFlexibleBox::LineState {
    LineState(LayoutUnit crossAxisOffset, LayoutUnit crossAxisExtent, std::optional<BaselineAlignmentState> baselineAlignmentState, FlexLayoutItems&& flexLayoutItems)
        : crossAxisOffset(crossAxisOffset)
        , crossAxisExtent(crossAxisExtent)
        , baselineAlignmentState(baselineAlignmentState)
        , flexLayoutItems(flexLayoutItems)
    {
    }
    
    LayoutUnit crossAxisOffset;
    LayoutUnit crossAxisExtent;
    std::optional<BaselineAlignmentState> baselineAlignmentState;
    FlexLayoutItems flexLayoutItems;
};

RenderFlexibleBox::RenderFlexibleBox(Type type, Element& element, RenderStyle&& style)
    : RenderBlock(type, element, WTFMove(style), TypeFlag::IsFlexibleBox)
{
    ASSERT(isRenderFlexibleBox());
    setChildrenInline(false); // All of our children must be block-level.
}

RenderFlexibleBox::RenderFlexibleBox(Type type, Document& document, RenderStyle&& style)
    : RenderBlock(type, document, WTFMove(style), TypeFlag::IsFlexibleBox)
{
    ASSERT(isRenderFlexibleBox());
    setChildrenInline(false); // All of our children must be block-level.
}

RenderFlexibleBox::~RenderFlexibleBox() = default;

ASCIILiteral RenderFlexibleBox::renderName() const
{
    return "RenderFlexibleBox"_s;
}

void RenderFlexibleBox::computeIntrinsicLogicalWidths(LayoutUnit& minLogicalWidth, LayoutUnit& maxLogicalWidth) const
{
    auto addScrollbarWidth = [&]() {
        LayoutUnit scrollbarWidth(scrollbarLogicalWidth());
        maxLogicalWidth += scrollbarWidth;
        minLogicalWidth += scrollbarWidth;
    };

    if (shouldApplySizeOrInlineSizeContainment()) {
        if (auto width = explicitIntrinsicInnerLogicalWidth()) {
            minLogicalWidth = width.value();
            maxLogicalWidth = width.value();
        }
        addScrollbarWidth();
        return;
    }

    LayoutUnit flexItemMinWidth;
    LayoutUnit flexItemMaxWidth;
    bool hadExcludedChildren = computePreferredWidthsForExcludedChildren(flexItemMinWidth, flexItemMaxWidth);

    // FIXME: We're ignoring flex-basis here and we shouldn't. We can't start
    // honoring it though until the flex shorthand stops setting it to 0. See
    // https://bugs.webkit.org/show_bug.cgi?id=116117 and
    // https://crbug.com/240765.
    size_t numItemsWithNormalLayout = 0;
    for (RenderBox* flexItem = firstChildBox(); flexItem; flexItem = flexItem->nextSiblingBox()) {
        if (flexItem->isOutOfFlowPositioned() || flexItem->isExcludedFromNormalLayout())
            continue;
        ++numItemsWithNormalLayout;

        // Pre-layout orthogonal children in order to get a valid value for the preferred width.
        if (writingMode().isOrthogonal(flexItem->writingMode()))
            flexItem->layoutIfNeeded();

        LayoutUnit margin = marginIntrinsicLogicalWidthForChild(*flexItem);

        LayoutUnit minPreferredLogicalWidth;
        LayoutUnit maxPreferredLogicalWidth;
        computeChildPreferredLogicalWidths(*flexItem, minPreferredLogicalWidth, maxPreferredLogicalWidth);

        minPreferredLogicalWidth += margin;
        maxPreferredLogicalWidth += margin;

        if (!isColumnFlow()) {
            maxLogicalWidth += maxPreferredLogicalWidth;
            if (isMultiline()) {
                // For multiline, the min preferred width is if you put a break between
                // each item.
                minLogicalWidth = std::max(minLogicalWidth, minPreferredLogicalWidth);
            } else
                minLogicalWidth += minPreferredLogicalWidth;
        } else {
            minLogicalWidth = std::max(minPreferredLogicalWidth, minLogicalWidth);
            maxLogicalWidth = std::max(maxPreferredLogicalWidth, maxLogicalWidth);
        }
    }

    if (!isColumnFlow() && numItemsWithNormalLayout > 1) {
        LayoutUnit inlineGapSize = (numItemsWithNormalLayout - 1) * computeGap(GapType::BetweenItems);
        maxLogicalWidth += inlineGapSize;
        if (!isMultiline())
            minLogicalWidth += inlineGapSize;
    }

    maxLogicalWidth = std::max(minLogicalWidth, maxLogicalWidth);
    
    // Due to negative margins, it is possible that we calculated a negative
    // intrinsic width. Make sure that we never return a negative width.
    minLogicalWidth = std::max(0_lu, minLogicalWidth);
    maxLogicalWidth = std::max(0_lu, maxLogicalWidth);
    
    if (hadExcludedChildren) {
        minLogicalWidth = std::max(minLogicalWidth, flexItemMinWidth);
        maxLogicalWidth = std::max(maxLogicalWidth, flexItemMaxWidth);
    }

    addScrollbarWidth();
}

#define SET_OR_CLEAR_OVERRIDING_SIZE(box, SizeType, size)       \
    {                                                           \
        if (size)                                               \
            box.setOverridingBorderBoxLogical##SizeType(*size); \
        else                                                    \
            box.clearOverridingBorderBoxLogical##SizeType();    \
    }

// RAII class which defines a scope in which overriding sizes of a box are either:
//   1) replaced by other size in one axis if size is specified
//   2) cleared in both axis if size == std::nullopt
//
// In any case the previous overriding sizes are restored on destruction (in case of
// not having a previous value it's simply cleared).
class OverridingSizesScope {
public:
    enum class Axis {
        Inline,
        Block,
        Both
    };

    OverridingSizesScope(RenderBox& box, Axis axis, std::optional<LayoutUnit> size = std::nullopt)
        : m_box(box)
        , m_axis(axis)
    {
        ASSERT(!size || (axis != Axis::Both));
        if (axis == Axis::Both || axis == Axis::Inline) {
            m_previousOverridingBorderBoxLogicalWidth = box.overridingBorderBoxLogicalWidth();
            SET_OR_CLEAR_OVERRIDING_SIZE(m_box, Width, size);
        }
        if (axis == Axis::Both || axis == Axis::Block) {
            m_previousOverridingBorderBoxLogicalHeight = box.overridingBorderBoxLogicalHeight();
            SET_OR_CLEAR_OVERRIDING_SIZE(m_box, Height, size);
        }
    }
    ~OverridingSizesScope()
    {
        if (m_axis == Axis::Inline || m_axis == Axis::Both)
            SET_OR_CLEAR_OVERRIDING_SIZE(m_box, Width, m_previousOverridingBorderBoxLogicalWidth);

        if (m_axis == Axis::Block || m_axis == Axis::Both)
            SET_OR_CLEAR_OVERRIDING_SIZE(m_box, Height, m_previousOverridingBorderBoxLogicalHeight);
    }

private:
    RenderBox& m_box;
    Axis m_axis;
    std::optional<LayoutUnit> m_previousOverridingBorderBoxLogicalWidth;
    std::optional<LayoutUnit> m_previousOverridingBorderBoxLogicalHeight;
};

static void updateFlexItemDirtyBitsBeforeLayout(bool relayoutFlexItem, RenderBox& flexItem)
{
    if (flexItem.isOutOfFlowPositioned())
        return;

    // FIXME: Technically percentage height objects only need a relayout if their percentage isn't going to be turned into
    // an auto value. Add a method to determine this, so that we can avoid the relayout.
    if (relayoutFlexItem || flexItem.hasRelativeLogicalHeight())
        flexItem.setChildNeedsLayout(MarkOnlyThis);
}

void RenderFlexibleBox::computeChildIntrinsicLogicalWidths(RenderBox& flexBoxChild, LayoutUnit& minPreferredLogicalWidth, LayoutUnit& maxPreferredLogicalWidth) const
{
    // Children excluded from normal layout are handled here too (e.g. legend when fieldset is set to flex).
    ASSERT(flexBoxChild.isFlexItem() || (flexBoxChild.parent() == this && flexBoxChild.isExcludedFromNormalLayout()));

    // If the item cross size should use the definite container cross size then set the overriding size now so
    // the intrinsic sizes are properly computed in the presence of aspect ratios. The only exception is when
    // we are both a flex item&container, because our parent might have already set our overriding size.
    auto flexItemIntrinsicSizeComputation = SetForScope(m_inFlexItemIntrinsicWidthComputation, true);
    if (flexItemCrossSizeShouldUseContainerCrossSize(flexBoxChild) && !isFlexItem()) {
        auto axis = mainAxisIsFlexItemInlineAxis(flexBoxChild) ? OverridingSizesScope::Axis::Block : OverridingSizesScope::Axis::Inline;
        OverridingSizesScope overridingSizeScope(flexBoxChild, axis, computeCrossSizeForFlexItemUsingContainerCrossSize(flexBoxChild));
        RenderBlock::computeChildIntrinsicLogicalWidths(flexBoxChild, minPreferredLogicalWidth, maxPreferredLogicalWidth);
        return;
    }

    OverridingSizesScope cleanOverridingSizesScope(flexBoxChild, OverridingSizesScope::Axis::Both);
    RenderBlock::computeChildIntrinsicLogicalWidths(flexBoxChild, minPreferredLogicalWidth, maxPreferredLogicalWidth);
}

std::optional<LayoutUnit> RenderFlexibleBox::firstLineBaseline() const
{
    if ((isWritingModeRoot() && !isFlexItem()) || !m_numberOfFlexItemsOnFirstLine || shouldApplyLayoutContainment())
        return { };

    auto* baselineFlexItem = flexItemForFirstBaseline();
    if (!baselineFlexItem)
        return { };

    if (!isColumnFlow() && !mainAxisIsFlexItemInlineAxis(*baselineFlexItem))
        return LayoutUnit { (crossAxisExtentForFlexItem(*baselineFlexItem) + baselineFlexItem->logicalTop()).toInt() };
    if (isColumnFlow() && mainAxisIsFlexItemInlineAxis(*baselineFlexItem))
        return LayoutUnit { (mainAxisExtentForFlexItem(*baselineFlexItem) + baselineFlexItem->logicalTop()).toInt() };

    std::optional<LayoutUnit> baseline = baselineFlexItem->firstLineBaseline();
    if (!baseline) {
        // FIXME: We should pass |direction| into firstLineBoxBaseline and stop bailing out if we're a writing mode root.
        // This would also fix some cases where the flexbox is orthogonal to its container.
        LineDirectionMode direction = isHorizontalWritingMode() ? HorizontalLine : VerticalLine;
        return synthesizedBaseline(*baselineFlexItem, style(), direction, BorderBox) + baselineFlexItem->logicalTop();
    }

    return LayoutUnit { (baseline.value() + baselineFlexItem->logicalTop()).toInt() };
}

std::optional <LayoutUnit> RenderFlexibleBox::lastLineBaseline() const
{
    if (isWritingModeRoot() || !m_numberOfFlexItemsOnLastLine || shouldApplyLayoutContainment())
        return { };

    auto* baselineFlexItem = flexItemForLastBaseline();
    if (!baselineFlexItem)
        return { };

    if (!isColumnFlow() && !mainAxisIsFlexItemInlineAxis(*baselineFlexItem))
        return LayoutUnit { (crossAxisExtentForFlexItem(*baselineFlexItem) + baselineFlexItem->logicalTop()).toInt() };
    if (isColumnFlow() && mainAxisIsFlexItemInlineAxis(*baselineFlexItem))
        return LayoutUnit { (mainAxisExtentForFlexItem(*baselineFlexItem) + baselineFlexItem->logicalTop()).toInt() };

    auto baseline = baselineFlexItem->lastLineBaseline();
    if (!baseline) {
        // FIXME: We should pass |direction| into firstLineBoxBaseline and stop bailing out if we're a writing mode root.
        // This would also fix some cases where the flexbox is orthogonal to its container.
        LineDirectionMode direction = isHorizontalWritingMode() ? HorizontalLine : VerticalLine;
        return synthesizedBaseline(*baselineFlexItem, style(), direction, BorderBox) + baselineFlexItem->logicalTop();
    }

    return LayoutUnit { (baseline.value() + baselineFlexItem->logicalTop()).toInt() };
}

static const StyleContentAlignmentData& contentAlignmentNormalBehavior()
{
    // The justify-content property applies along the main axis, but since
    // flexing in the main axis is controlled by flex, stretch behaves as
    // flex-start (ignoring the specified fallback alignment, if any).
    // https://drafts.csswg.org/css-align/#distribution-flex
    static const StyleContentAlignmentData normalBehavior = { ContentPosition::Normal, ContentDistribution::Stretch};
    return normalBehavior;
}

void RenderFlexibleBox::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    RenderBlock::styleDidChange(diff, oldStyle);
    if (!oldStyle || diff != StyleDifference::Layout)
        return;

    auto oldStyleAlignItemsIsStretch = oldStyle->resolvedAlignItems(selfAlignmentNormalBehavior()).position() == ItemPosition::Stretch;
    for (auto& flexItem : childrenOfType<RenderBox>(*this)) {
        // Flex items that were previously stretching need to be relayed out so we
        // can compute new available cross axis space. This is only necessary for
        // stretching since other alignment values don't change the size of the
        // box.
        if (oldStyleAlignItemsIsStretch) {
            ItemPosition previousAlignment = flexItem.style().resolvedAlignSelf(oldStyle, selfAlignmentNormalBehavior()).position();
            if (previousAlignment == ItemPosition::Stretch && previousAlignment != flexItem.style().resolvedAlignSelf(&style(), selfAlignmentNormalBehavior()).position())
                flexItem.setChildNeedsLayout(MarkOnlyThis);
        }
    }
}

bool RenderFlexibleBox::hitTestChildren(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& adjustedLocation, HitTestAction hitTestAction)
{
    if (hitTestAction != HitTestForeground)
        return false;

    LayoutPoint scrolledOffset = hasNonVisibleOverflow() ? adjustedLocation - toLayoutSize(scrollPosition()) : adjustedLocation;

    // If collecting the children in reverse order is bad for performance, this Vector could be determined at layout time.
    Vector<RenderBox*> reversedOrderIteratorForHitTesting;
    for (auto* flexItem = m_orderIterator.first(); flexItem; flexItem = m_orderIterator.next()) {
        if (flexItem->isOutOfFlowPositioned())
            continue;
        reversedOrderIteratorForHitTesting.append(flexItem);
    }
    reversedOrderIteratorForHitTesting.reverse();

    for (auto* flexItem : reversedOrderIteratorForHitTesting) {
        if (flexItem->hasSelfPaintingLayer())
            continue;
        auto location = flipForWritingModeForChild(*flexItem, scrolledOffset);
        if (flexItem->hitTest(request, result, locationInContainer, location)) {
            updateHitTestResult(result, flipForWritingMode(toLayoutPoint(locationInContainer.point() - adjustedLocation)));
            return true;
        }
    }

    return false;
}

void RenderFlexibleBox::layoutBlock(RelayoutChildren relayoutChildren, LayoutUnit)
{
    ASSERT(needsLayout());

    if (relayoutChildren == RelayoutChildren::No) {
        auto simplifiedLayoutScope = SetForScope(m_inSimplifiedLayout, true);
        if (simplifiedLayout())
            return;
    }

    LayoutRepainter repainter(*this);

    resetLogicalHeightBeforeLayoutIfNeeded();
    m_relaidOutFlexItems.clear();
    
    bool oldInLayout = m_inLayout;
    m_inLayout = true;

    if (!style().marginTrim().isEmpty())
        initializeMarginTrimState();

    if (recomputeLogicalWidth())
        relayoutChildren = RelayoutChildren::Yes;

    LayoutUnit previousHeight = logicalHeight();
    setLogicalHeight(borderAndPaddingLogicalHeight() + scrollbarLogicalHeight());
    {
        LayoutStateMaintainer statePusher(*this, locationOffset(), isTransformed() || hasReflection() || writingMode().isBlockFlipped());

        preparePaginationBeforeBlockLayout(relayoutChildren);

        m_numberOfFlexItemsOnFirstLine = { };
        m_numberOfFlexItemsOnLastLine = { };
        m_justifyContentStartOverflow = 0;

        beginUpdateScrollInfoAfterLayoutTransaction();

        prepareOrderIteratorAndMargins();

        // Fieldsets need to find their legend and position it inside the border of the object.
        // The legend then gets skipped during normal layout. The same is true for ruby text.
        // It doesn't get included in the normal layout process but is instead skipped.
        layoutExcludedChildren(relayoutChildren);

        FlexItemFrameRects oldFlexItemRects;
        appendFlexItemFrameRects(oldFlexItemRects);

        performFlexLayout(relayoutChildren);

        {
            auto scrollbarLayout = SetForScope(m_inPostFlexUpdateScrollbarLayout, true);
            endAndCommitUpdateScrollInfoAfterLayoutTransaction();
        }

        if (logicalHeight() != previousHeight)
            relayoutChildren = RelayoutChildren::Yes;

        if (isDocumentElementRenderer())
            layoutOutOfFlowBoxes(RelayoutChildren::Yes);
        else
            layoutOutOfFlowBoxes(relayoutChildren);

        repaintFlexItemsDuringLayoutIfMoved(oldFlexItemRects);
        // FIXME: css3/flexbox/repaint-rtl-column.html seems to repaint more overflow than it needs to.
        computeOverflow(layoutOverflowLogicalBottom(*this));

        updateDescendantTransformsAfterLayout();
    }
    updateLayerTransform();

    // We have to reset this, because changes to our ancestors' style can affect
    // this value. Also, this needs to be before we call updateAfterLayout, as
    // that function may re-enter this one.
    resetHasDefiniteHeight();

    // Update our scroll information if we're overflow:auto/scroll/hidden now that we know if we overflow or not.
    updateScrollInfoAfterLayout();

    repainter.repaintAfterLayout();

    clearNeedsLayout();
    
    m_inLayout = oldInLayout;
}

void RenderFlexibleBox::appendFlexItemFrameRects(FlexItemFrameRects& flexItemFrameRects)
{
    for (RenderBox* flexItem = m_orderIterator.first(); flexItem; flexItem = m_orderIterator.next()) {
        if (!flexItem->isOutOfFlowPositioned())
            flexItemFrameRects.append(flexItem->frameRect());
    }
}

void RenderFlexibleBox::repaintFlexItemsDuringLayoutIfMoved(const FlexItemFrameRects& oldFlexItemRects)
{
    size_t index = 0;
    for (RenderBox* flexItem = m_orderIterator.first(); flexItem; flexItem = m_orderIterator.next()) {
        if (flexItem->isOutOfFlowPositioned())
            continue;

        // If the child moved, we have to repaint it as well as any floating/positioned
        // descendants. An exception is if we need a layout. In this case, we know we're going to
        // repaint ourselves (and the child) anyway.
        if (!selfNeedsLayout() && flexItem->checkForRepaintDuringLayout())
            flexItem->repaintDuringLayoutIfMoved(oldFlexItemRects[index]);
        ++index;
    }
    ASSERT(index == oldFlexItemRects.size());
}

void RenderFlexibleBox::paintChildren(PaintInfo& paintInfo, const LayoutPoint& paintOffset, PaintInfo& paintInfoForFlexItem, bool usePrintRect)
{
    for (RenderBox* flexItem = m_orderIterator.first(); flexItem; flexItem = m_orderIterator.next()) {
        if (!paintChild(*flexItem, paintInfo, paintOffset, paintInfoForFlexItem, usePrintRect, PaintAsInlineBlock))
            return;
    }
}

void RenderFlexibleBox::repositionLogicalHeightDependentFlexItems(FlexLineStates& lineStates, LayoutUnit gapBetweenLines)
{
    auto flexLayoutScope = SetForScope(m_inCrossAxisLayout, true);
    LayoutUnit crossAxisStartEdge = lineStates.isEmpty() ? 0_lu : lineStates[0].crossAxisOffset;
    // If we have a single line flexbox, the line height is all the available space. For flex-direction: row,
    // this means we need to use the height, so we do this after calling updateLogicalHeight.
    if (!isMultiline() && !lineStates.isEmpty())
        lineStates[0].crossAxisExtent = crossAxisContentExtent();

    alignFlexLines(lineStates, gapBetweenLines);

    alignFlexItems(lineStates);
    
    if (style().flexWrap() == FlexWrap::Reverse)
        flipForWrapReverse(lineStates, crossAxisStartEdge);
    
    // direction:rtl + flex-direction:column means the cross-axis direction is
    // flipped.
    flipForRightToLeftColumn(lineStates);
}

bool RenderFlexibleBox::mainAxisIsFlexItemInlineAxis(const RenderBox& flexItem) const
{
    return isHorizontalFlow() == flexItem.isHorizontalWritingMode();
}

bool RenderFlexibleBox::isColumnFlow() const
{
    return style().isColumnFlexDirection();
}

bool RenderFlexibleBox::isColumnOrRowReverse() const
{
    return style().flexDirection() == FlexDirection::ColumnReverse || style().flexDirection() == FlexDirection::RowReverse;
}

bool RenderFlexibleBox::isHorizontalFlow() const
{
    if (isHorizontalWritingMode())
        return !isColumnFlow();
    return isColumnFlow();
}

bool RenderFlexibleBox::isLeftToRightFlow() const
{
    if (isColumnFlow())
        return writingMode().blockDirection() == FlowDirection::TopToBottom || writingMode().blockDirection() == FlowDirection::LeftToRight;
    return writingMode().isLogicalLeftInlineStart() ^ (style().flexDirection() == FlexDirection::RowReverse);
}

RenderFlexibleBox::Direction RenderFlexibleBox::crossAxisDirection() const
{
    auto crossAxisDirection = style().isRowFlexDirection() ? writingMode().blockDirection() : writingMode().inlineDirection();
    switch (crossAxisDirection) {
    case FlowDirection::TopToBottom:
        return style().flexWrap() == FlexWrap::Reverse ? Direction::BottomToTop : Direction::TopToBottom;
    case FlowDirection::BottomToTop:
        return style().flexWrap() == FlexWrap::Reverse ? Direction::TopToBottom : Direction::BottomToTop;
    case FlowDirection::LeftToRight:
        return style().flexWrap() == FlexWrap::Reverse ? Direction::RightToLeft : Direction::LeftToRight;
    case FlowDirection::RightToLeft:
        return style().flexWrap() == FlexWrap::Reverse ? Direction::LeftToRight : Direction::RightToLeft;
    default:
        ASSERT_NOT_REACHED();
        return Direction::TopToBottom;
    }
}

bool RenderFlexibleBox::isMultiline() const
{
    return style().flexWrap() != FlexWrap::NoWrap;
}

// https://drafts.csswg.org/css-flexbox/#min-size-auto
bool RenderFlexibleBox::shouldApplyMinSizeAutoForFlexItem(const RenderBox& flexItem) const
{
    auto minSize = minMainSizeLengthForFlexItem(flexItem);
    // min, max and fit-content are equivalent to the automatic size for block sizes https://drafts.csswg.org/css-sizing-3/#valdef-width-min-content.
    bool flexItemBlockSizeIsEquivalentToAutomaticSize  = !mainAxisIsFlexItemInlineAxis(flexItem) && (minSize.isMinContent() || minSize.isMaxContent() || minSize.isFitContent());

    return (minSize.isAuto() || flexItemBlockSizeIsEquivalentToAutomaticSize) && (mainAxisOverflowForFlexItem(flexItem) == Overflow::Visible);
}

bool RenderFlexibleBox::shouldApplyMinBlockSizeAutoForFlexItem(const RenderBox& flexItem) const
{
    return !mainAxisIsFlexItemInlineAxis(flexItem) && shouldApplyMinSizeAutoForFlexItem(flexItem);
}

Style::FlexBasis RenderFlexibleBox::flexBasisForFlexItem(const RenderBox& flexItem) const
{
    auto flexBasis = flexItem.style().flexBasis();
    if (flexBasis.isAuto())
        flexBasis = preferredMainSizeLengthForFlexItem(flexItem).asFlexBasis();
    return flexBasis;
}

LayoutUnit RenderFlexibleBox::crossAxisExtentForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.height() : flexItem.width();
}

LayoutUnit RenderFlexibleBox::cachedFlexItemIntrinsicContentLogicalHeight(const RenderBox& flexItem) const
{
    if (auto* renderReplaced = dynamicDowncast<RenderReplaced>(flexItem))
        return renderReplaced->intrinsicLogicalHeight();
    
    if (m_intrinsicContentLogicalHeights.contains(flexItem))
        return m_intrinsicContentLogicalHeights.get(flexItem);
    
    return flexItem.contentBoxLogicalHeight();
}

void RenderFlexibleBox::setCachedFlexItemIntrinsicContentLogicalHeight(const RenderBox& flexItem, LayoutUnit height)
{
    if (flexItem.isRenderReplaced())
        return; // Replaced elements know their intrinsic height already, so save space by not caching.
    m_intrinsicContentLogicalHeights.set(flexItem, height);
}

void RenderFlexibleBox::clearCachedFlexItemIntrinsicContentLogicalHeight(const RenderBox& flexItem)
{
    if (flexItem.isRenderReplaced())
        return; // Replaced elements know their intrinsic height already, so nothing to do.
    m_intrinsicContentLogicalHeights.remove(flexItem);
}

LayoutUnit RenderFlexibleBox::flexItemIntrinsicLogicalHeight(RenderBox& flexItem) const
{
    // This should only be called if the logical height is the cross size
    ASSERT(mainAxisIsFlexItemInlineAxis(flexItem));
    if (needToStretchFlexItemLogicalHeight(flexItem)) {
        LayoutUnit flexItemContentHeight = cachedFlexItemIntrinsicContentLogicalHeight(flexItem);
        LayoutUnit flexItemLogicalHeight = flexItemContentHeight + flexItem.scrollbarLogicalHeight() + flexItem.borderAndPaddingLogicalHeight();
        return flexItem.constrainLogicalHeightByMinMax(flexItemLogicalHeight, flexItemContentHeight);
    }
    return flexItem.logicalHeight();
}

LayoutUnit RenderFlexibleBox::flexItemIntrinsicLogicalWidth(RenderBox& flexItem)
{
    // This should only be called if the logical width is the cross size
    ASSERT(!mainAxisIsFlexItemInlineAxis(flexItem));
    if (flexItemCrossSizeIsDefinite(flexItem, flexItem.style().logicalWidth()))
        return flexItem.logicalWidth();

    LogicalExtentComputedValues values;
    {
        OverridingSizesScope cleanOverridingWidthScope(flexItem, OverridingSizesScope::Axis::Inline);
        flexItem.computeLogicalWidth(values);
    }
    return values.m_extent;
}

LayoutUnit RenderFlexibleBox::crossAxisIntrinsicExtentForFlexItem(RenderBox& flexItem)
{
    return mainAxisIsFlexItemInlineAxis(flexItem) ? flexItemIntrinsicLogicalHeight(flexItem) : flexItemIntrinsicLogicalWidth(flexItem);
}

LayoutUnit RenderFlexibleBox::mainAxisExtentForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.size().width() : flexItem.size().height();
}

LayoutUnit RenderFlexibleBox::mainAxisContentExtentForFlexItemIncludingScrollbar(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.contentBoxWidth() + flexItem.verticalScrollbarWidth() : flexItem.contentBoxHeight() + flexItem.horizontalScrollbarHeight();
}

LayoutUnit RenderFlexibleBox::crossAxisExtent() const
{
    return isHorizontalFlow() ? size().height() : size().width();
}

LayoutUnit RenderFlexibleBox::mainAxisExtent() const
{
    return isHorizontalFlow() ? size().width() : size().height();
}

LayoutUnit RenderFlexibleBox::crossAxisContentExtent() const
{
    return isHorizontalFlow() ? contentBoxHeight() : contentBoxWidth();
}

LayoutUnit RenderFlexibleBox::mainAxisContentExtent(LayoutUnit contentLogicalHeight)
{
    if (!isColumnFlow())
        return contentBoxLogicalWidth();

    LayoutUnit borderPaddingAndScrollbar = borderAndPaddingLogicalHeight() + scrollbarLogicalHeight();
    LayoutUnit borderBoxLogicalHeight = contentLogicalHeight + borderPaddingAndScrollbar;
    auto computedValues = computeLogicalHeight(borderBoxLogicalHeight, logicalTop());
    if (computedValues.m_extent == LayoutUnit::max())
        return computedValues.m_extent;
    return std::max(0_lu, computedValues.m_extent - borderPaddingAndScrollbar);
}

// FIXME: consider adding this check to RenderBox::hasIntrinsicAspectRatio(). We could even make it
// virtual returning false by default. RenderReplaced will overwrite it with the current implementation
// plus this extra check. See wkb.ug/231955.
static bool isSVGRootWithIntrinsicAspectRatio(const RenderBox& flexItem)
{
    if (!flexItem.isRenderOrLegacyRenderSVGRoot())
        return false;
    // It's common for some replaced elements, such as SVGs, to have intrinsic aspect ratios but no intrinsic sizes.
    // That's why it isn't enough just to check for intrinsic sizes in those cases.
    return downcast<RenderReplaced>(flexItem).computeIntrinsicAspectRatio() > 0;
};

static bool flexItemHasAspectRatio(const RenderBox& flexItem)
{
    return flexItem.hasIntrinsicAspectRatio() || flexItem.style().hasAspectRatio() || isSVGRootWithIntrinsicAspectRatio(flexItem);
}

template<typename SizeType> std::optional<LayoutUnit> RenderFlexibleBox::computeMainAxisExtentForFlexItem(RenderBox& flexItem, const SizeType& size)
{
    // If we have a horizontal flow, that means the main size is the width.
    // That's the logical width for horizontal writing modes, and the logical
    // height in vertical writing modes. For a vertical flow, main size is the
    // height, so it's the inverse. So we need the logical width if we have a
    // horizontal flow and horizontal writing mode, or vertical flow and vertical
    // writing mode. Otherwise we need the logical height.
    if (!mainAxisIsFlexItemInlineAxis(flexItem)) {
        // We don't have to check for "auto" here - computeContentLogicalHeight
        // will just return a null Optional for that case anyway. It's safe to access
        // scrollbarLogicalHeight here because ComputeNextFlexLine will have
        // already forced layout on the child. We previously did a layout out the child
        // if necessary (see ComputeNextFlexLine and the call to
        // flexItemHasIntrinsicMainAxisSize) so we can be sure that the two height
        // calls here will return up-to-date data.
        auto height = flexItem.computeContentLogicalHeight(size, cachedFlexItemIntrinsicContentLogicalHeight(flexItem));
        if (!height)
            return height;
        // Tables interpret overriding sizes as the size of captions + rows. However the specified height of a table
        // only includes the size of the rows. That's why we need to add the size of the captions here so that the table
        // layout algorithm behaves appropriately.
        LayoutUnit captionsHeight;
        if (CheckedPtr table = dynamicDowncast<RenderTable>(flexItem); table && flexItemMainSizeIsDefinite(flexItem, size))
            captionsHeight = table->sumCaptionsLogicalHeight();
        return *height + flexItem.scrollbarLogicalHeight() + captionsHeight;
    }

    // computeLogicalWidth always re-computes the intrinsic widths. However, when
    // our logical width is auto, we can just use our cached value. So let's do
    // that here. (Compare code in RenderBlock::computePreferredLogicalWidths)
    if (flexItem.style().logicalWidth().isAuto() && !flexItemHasAspectRatio(flexItem)) {
        if (size.isMinContent()) {
            if (flexItem.shouldInvalidatePreferredWidths())
                flexItem.setNeedsPreferredWidthsUpdate(MarkOnlyThis);
            return flexItem.minPreferredLogicalWidth() - flexItem.borderAndPaddingLogicalWidth();
        }
        if (size.isMaxContent()) {
            if (flexItem.shouldInvalidatePreferredWidths())
                flexItem.setNeedsPreferredWidthsUpdate(MarkOnlyThis);
            return flexItem.maxPreferredLogicalWidth() - flexItem.borderAndPaddingLogicalWidth();
        }
    }

    auto mainAxisWidth = isColumnFlow() ? availableLogicalHeight(AvailableLogicalHeightType::ExcludeMarginBorderPadding) : contentBoxLogicalWidth();
    return flexItem.computeLogicalWidthUsing(size, mainAxisWidth, *this) - flexItem.borderAndPaddingLogicalWidth();
}

FlowDirection RenderFlexibleBox::transformedBlockFlowDirection() const
{
    if (!isColumnFlow())
        return writingMode().blockDirection();
    return writingMode().inlineDirection();
}

LayoutUnit RenderFlexibleBox::flowAwareBorderStart() const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? borderLeft() : borderRight();
    return isLeftToRightFlow() ? borderTop() : borderBottom();
}

LayoutUnit RenderFlexibleBox::flowAwareBorderEnd() const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? borderRight() : borderLeft();
    return isLeftToRightFlow() ? borderBottom() : borderTop();
}

LayoutUnit RenderFlexibleBox::flowAwareBorderBefore() const
{
    switch (transformedBlockFlowDirection()) {
    case FlowDirection::TopToBottom:
        return borderTop();
    case FlowDirection::BottomToTop:
        return borderBottom();
    case FlowDirection::LeftToRight:
        return borderLeft();
    case FlowDirection::RightToLeft:
        return borderRight();
    }
    ASSERT_NOT_REACHED();
    return borderTop();
}

LayoutUnit RenderFlexibleBox::flowAwareBorderAfter() const
{
    switch (transformedBlockFlowDirection()) {
    case FlowDirection::TopToBottom:
        return borderBottom();
    case FlowDirection::BottomToTop:
        return borderTop();
    case FlowDirection::LeftToRight:
        return borderRight();
    case FlowDirection::RightToLeft:
        return borderLeft();
    }
    ASSERT_NOT_REACHED();
    return borderTop();
}

LayoutUnit RenderFlexibleBox::flowAwarePaddingStart() const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? paddingLeft() : paddingRight();
    return isLeftToRightFlow() ? paddingTop() : paddingBottom();
}

LayoutUnit RenderFlexibleBox::flowAwarePaddingEnd() const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? paddingRight() : paddingLeft();
    return isLeftToRightFlow() ? paddingBottom() : paddingTop();
}

LayoutUnit RenderFlexibleBox::flowAwarePaddingBefore() const
{
    switch (transformedBlockFlowDirection()) {
    case FlowDirection::TopToBottom:
        return paddingTop();
    case FlowDirection::BottomToTop:
        return paddingBottom();
    case FlowDirection::LeftToRight:
        return paddingLeft();
    case FlowDirection::RightToLeft:
        return paddingRight();
    }
    ASSERT_NOT_REACHED();
    return paddingTop();
}

LayoutUnit RenderFlexibleBox::flowAwarePaddingAfter() const
{
    switch (transformedBlockFlowDirection()) {
    case FlowDirection::TopToBottom:
        return paddingBottom();
    case FlowDirection::BottomToTop:
        return paddingTop();
    case FlowDirection::LeftToRight:
        return paddingRight();
    case FlowDirection::RightToLeft:
        return paddingLeft();
    }
    ASSERT_NOT_REACHED();
    return paddingTop();
}

LayoutUnit RenderFlexibleBox::flowAwareMarginStartForFlexItem(const RenderBox& flexItem) const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? flexItem.marginLeft() : flexItem.marginRight();
    return isLeftToRightFlow() ? flexItem.marginTop() : flexItem.marginBottom();
}

LayoutUnit RenderFlexibleBox::flowAwareMarginEndForFlexItem(const RenderBox& flexItem) const
{
    if (isHorizontalFlow())
        return isLeftToRightFlow() ? flexItem.marginRight() : flexItem.marginLeft();
    return isLeftToRightFlow() ? flexItem.marginBottom() : flexItem.marginTop();
}

LayoutUnit RenderFlexibleBox::flowAwareMarginBeforeForFlexItem(const RenderBox& flexItem) const
{
    switch (transformedBlockFlowDirection()) {
    case FlowDirection::TopToBottom:
        return flexItem.marginTop();
    case FlowDirection::BottomToTop:
        return flexItem.marginBottom();
    case FlowDirection::LeftToRight:
        return flexItem.marginLeft();
    case FlowDirection::RightToLeft:
        return flexItem.marginRight();
    }
    ASSERT_NOT_REACHED();
    return marginTop();
}

void RenderFlexibleBox::initializeMarginTrimState()
{
    // When computeIntrinsicLogicalWidth goes through each of the children, it
    // will include the margins when computing the flexbox's min and max widths.
    // We need to trim the margins of the first and last child early so that
    // these margins do not incorrectly constribute to the box's min/max width
    auto marginTrim = style().marginTrim();
    auto isRowsFlexbox = isHorizontalFlow();
    if (auto flexItem = firstInFlowChildBox(); flexItem && marginTrim.contains(MarginTrimType::InlineStart))
        isRowsFlexbox ? m_marginTrimItems.m_itemsAtFlexLineStart.add(*flexItem) : m_marginTrimItems.m_itemsOnFirstFlexLine.add(*flexItem);
    if (auto flexItem = lastInFlowChildBox(); flexItem && marginTrim.contains(MarginTrimType::InlineEnd))
        isRowsFlexbox ? m_marginTrimItems.m_itemsAtFlexLineEnd.add(*flexItem) : m_marginTrimItems.m_itemsOnLastFlexLine.add(*flexItem);
}

bool RenderFlexibleBox::canFitItemWithTrimmedMarginEnd(const FlexLayoutItem& flexLayoutItem, LayoutUnit sumHypotheticalMainSize, LayoutUnit lineBreakLength) const
{
    auto marginTrim = style().marginTrim();
    if ((isHorizontalFlow() && marginTrim.contains(MarginTrimType::InlineEnd)) || (isColumnFlow() && marginTrim.contains(MarginTrimType::BlockEnd)))
        return sumHypotheticalMainSize + flexLayoutItem.hypotheticalMainAxisMarginBoxSize() - flowAwareMarginEndForFlexItem(flexLayoutItem.renderer) <= lineBreakLength;
    return false;
}

void RenderFlexibleBox::removeMarginEndFromFlexSizes(FlexLayoutItem& flexLayoutItem, LayoutUnit& sumFlexBaseSize, LayoutUnit& sumHypotheticalMainSize) const
{
    LayoutUnit margin;
    if (isHorizontalFlow())
        margin = flexLayoutItem.renderer->marginEnd(writingMode());
    else
        margin = flexLayoutItem.renderer->marginAfter(writingMode());
    sumFlexBaseSize -= margin;
    sumHypotheticalMainSize -= margin;
}

LayoutUnit RenderFlexibleBox::mainAxisMarginExtentForFlexItem(const RenderBox& flexItem) const
{
    if (!flexItem.needsLayout())
        return isHorizontalFlow() ? flexItem.horizontalMarginExtent() : flexItem.verticalMarginExtent();

    LayoutUnit marginStart;
    LayoutUnit marginEnd;
    if (isHorizontalFlow())
        flexItem.computeInlineDirectionMargins(*this, flexItem.containingBlockLogicalWidthForContent(), flexItem.logicalWidth(), { }, marginStart, marginEnd);
    else
        flexItem.computeBlockDirectionMargins(*this, marginStart, marginEnd);
    return marginStart + marginEnd;
}

LayoutUnit RenderFlexibleBox::crossAxisMarginExtentForFlexItem(const RenderBox& flexItem) const
{
    if (!flexItem.needsLayout())
        return isHorizontalFlow() ? flexItem.verticalMarginExtent() : flexItem.horizontalMarginExtent();

    LayoutUnit marginStart;
    LayoutUnit marginEnd;
    if (isHorizontalFlow())
        flexItem.computeBlockDirectionMargins(*this, marginStart, marginEnd);
    else
        flexItem.computeInlineDirectionMargins(*this, flexItem.containingBlockLogicalWidthForContent(), flexItem.logicalWidth(), { }, marginStart, marginEnd);
    return marginStart + marginEnd;
}

bool RenderFlexibleBox::isChildEligibleForMarginTrim(MarginTrimType marginTrimType, const RenderBox& flexItem) const
{
    ASSERT(style().marginTrim().contains(marginTrimType));
    auto isMarginParallelWithMainAxis = [this](MarginTrimType marginTrimType) {
        if (isHorizontalFlow())
            return marginTrimType == MarginTrimType::BlockStart || marginTrimType == MarginTrimType::BlockEnd;
        return marginTrimType == MarginTrimType::InlineStart || marginTrimType == MarginTrimType::InlineEnd;
    };
    if (isMarginParallelWithMainAxis(marginTrimType))
        return (marginTrimType == MarginTrimType::BlockStart || marginTrimType == MarginTrimType::InlineStart) ? m_marginTrimItems.m_itemsOnFirstFlexLine.contains(flexItem) : m_marginTrimItems.m_itemsOnLastFlexLine.contains(flexItem);
    return (marginTrimType == MarginTrimType::BlockStart || marginTrimType == MarginTrimType::InlineStart) ? m_marginTrimItems.m_itemsAtFlexLineStart.contains(flexItem) : m_marginTrimItems.m_itemsAtFlexLineEnd.contains(flexItem);
}

bool RenderFlexibleBox::shouldTrimMainAxisMarginStart() const
{
    if (isHorizontalFlow())
        return style().marginTrim().contains(MarginTrimType::InlineStart);
    return style().marginTrim().contains(MarginTrimType::BlockStart);
}

bool RenderFlexibleBox::shouldTrimMainAxisMarginEnd() const
{
    if (isHorizontalFlow())
        return style().marginTrim().contains(MarginTrimType::InlineEnd);
    return style().marginTrim().contains(MarginTrimType::BlockEnd);
}

bool RenderFlexibleBox::shouldTrimCrossAxisMarginStart() const
{
    if (isHorizontalFlow())
        return style().marginTrim().contains(MarginTrimType::BlockStart);
    return style().marginTrim().contains(MarginTrimType::InlineStart);
}

bool RenderFlexibleBox::shouldTrimCrossAxisMarginEnd() const
{
    if (isHorizontalFlow())
        return style().marginTrim().contains(MarginTrimType::BlockEnd);
    return style().marginTrim().contains(MarginTrimType::InlineEnd);
}

void RenderFlexibleBox::trimMainAxisMarginStart(const FlexLayoutItem& flexLayoutItem)
{
    auto horizontalFlow = isHorizontalFlow();
    flexLayoutItem.mainAxisMargin -= horizontalFlow
        ? flexLayoutItem.renderer->marginStart(writingMode())
        : flexLayoutItem.renderer->marginBefore(writingMode());
    if (horizontalFlow)
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::InlineStart);
    else
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::BlockStart);
    m_marginTrimItems.m_itemsAtFlexLineStart.add(flexLayoutItem.renderer);
}

void RenderFlexibleBox::trimMainAxisMarginEnd(const FlexLayoutItem& flexLayoutItem)
{
    auto horizontalFlow = isHorizontalFlow();
    flexLayoutItem.mainAxisMargin -= horizontalFlow
        ? flexLayoutItem.renderer->marginEnd(writingMode())
        : flexLayoutItem.renderer->marginAfter(writingMode());
    if (horizontalFlow)
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::InlineEnd);
    else
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::BlockEnd);
    m_marginTrimItems.m_itemsAtFlexLineEnd.add(flexLayoutItem.renderer);
}

void RenderFlexibleBox::trimCrossAxisMarginStart(const FlexLayoutItem& flexLayoutItem)
{
    if (isHorizontalFlow())
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::BlockStart);
    else
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::InlineStart);
    m_marginTrimItems.m_itemsOnFirstFlexLine.add(flexLayoutItem.renderer);
}

void RenderFlexibleBox::trimCrossAxisMarginEnd(const FlexLayoutItem& flexLayoutItem)
{
    if (isHorizontalFlow())
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::BlockEnd);
    else
        setTrimmedMarginForChild(flexLayoutItem.renderer, MarginTrimType::InlineEnd);
    m_marginTrimItems.m_itemsOnLastFlexLine.add(flexLayoutItem.renderer);
}

LayoutUnit RenderFlexibleBox::crossAxisScrollbarExtent() const
{
    return isHorizontalFlow() ? horizontalScrollbarHeight() : verticalScrollbarWidth();
}

LayoutPoint RenderFlexibleBox::flowAwareLocationForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.location() : flexItem.location().transposedPoint();
}

const Style::PreferredSize& RenderFlexibleBox::preferredCrossSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().height() : flexItem.style().width();
}

const Style::MinimumSize& RenderFlexibleBox::minCrossSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().minHeight() : flexItem.style().minWidth();
}

const Style::MaximumSize& RenderFlexibleBox::maxCrossSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().maxHeight() : flexItem.style().maxWidth();
}

const Style::PreferredSize& RenderFlexibleBox::preferredMainSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().width() : flexItem.style().height();
}

const Style::MinimumSize& RenderFlexibleBox::minMainSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().minWidth() : flexItem.style().minHeight();
}

const Style::MaximumSize& RenderFlexibleBox::maxMainSizeLengthForFlexItem(const RenderBox& flexItem) const
{
    return isHorizontalFlow() ? flexItem.style().maxWidth() : flexItem.style().maxHeight();
}

double RenderFlexibleBox::preferredAspectRatioForFlexItem(const RenderBox& flexItem) const
{
    auto flexItemAspectRatio = [&] {
        auto flexItemIntrinsicSize = LayoutSize { flexItem.intrinsicLogicalWidth(), flexItem.intrinsicLogicalHeight() };
        if (flexItem.isRenderOrLegacyRenderSVGRoot())
            return downcast<RenderReplaced>(flexItem).computeIntrinsicAspectRatio();
        if (flexItem.style().aspectRatio().isRatio() || (flexItem.style().aspectRatio().isAutoAndRatio() && flexItemIntrinsicSize.isEmpty()))
            return flexItem.style().logicalAspectRatio();
        if (auto* replacedElement = dynamicDowncast<RenderReplaced>(flexItem))
            return replacedElement->computeIntrinsicAspectRatio();

        ASSERT(flexItem.intrinsicLogicalHeight());
        return flexItem.intrinsicLogicalWidth().toDouble() / flexItem.intrinsicLogicalHeight().toDouble();
    };

    if (mainAxisIsFlexItemInlineAxis(flexItem))
        return flexItemAspectRatio();
    return 1 / flexItemAspectRatio();
}

// FIXME: computeMainSizeFromAspectRatioUsing may need to return an std::optional<LayoutUnit> in the future
// rather than returning indefinite sizes as 0/-1.
template<typename SizeType> LayoutUnit RenderFlexibleBox::computeMainSizeFromAspectRatioUsing(const RenderBox& flexItem, const SizeType& crossSizeLength) const
{
    ASSERT(flexItemHasAspectRatio(flexItem));

    // `crossSize` is border-box size if box-sizing is border-box, and content-box otherwise.

    auto crossSizeOptional = WTF::switchOn(crossSizeLength,
        [&](const SizeType::Fixed& fixedCrossSizeLength) -> std::optional<LayoutUnit> {
            return LayoutUnit(fixedCrossSizeLength.value);
        },
        [&](const SizeType::Percentage& percentageCrossSizeLength) -> std::optional<LayoutUnit> {
            return mainAxisIsFlexItemInlineAxis(flexItem)
                ? flexItem.computePercentageLogicalHeight(percentageCrossSizeLength)
                : adjustBorderBoxLogicalWidthForBoxSizing(Style::evaluate(percentageCrossSizeLength, contentBoxWidth()));
        },
        [&](const SizeType::Calc& calcCrossSizeLength) -> std::optional<LayoutUnit> {
            return mainAxisIsFlexItemInlineAxis(flexItem)
                ? flexItem.computePercentageLogicalHeight(calcCrossSizeLength)
                : adjustBorderBoxLogicalWidthForBoxSizing(Style::evaluate(calcCrossSizeLength, contentBoxWidth()));
        },
        [&](const CSS::Keyword::Auto&) -> std::optional<LayoutUnit> {
            ASSERT(flexItemCrossSizeShouldUseContainerCrossSize(flexItem));
            return computeCrossSizeForFlexItemUsingContainerCrossSize(flexItem);
        },
        [&](const auto&) -> std::optional<LayoutUnit> {
            ASSERT_NOT_REACHED();
            return { };
        }
    );
    if (!crossSizeOptional)
        return 0_lu;

    auto crossSize = *crossSizeOptional;
    auto flexItemIntrinsicSize = flexItem.intrinsicSize();
    LayoutUnit borderAndPadding;
    if (flexItem.style().aspectRatio().isRatio() || (flexItem.style().aspectRatio().isAutoAndRatio() && flexItemIntrinsicSize.isEmpty())) {
        if (flexItem.style().boxSizingForAspectRatio() == BoxSizing::ContentBox)
            crossSize -= isHorizontalFlow() ? flexItem.verticalBorderAndPaddingExtent() : flexItem.horizontalBorderAndPaddingExtent();
        else
            borderAndPadding = isHorizontalFlow() ? flexItem.horizontalBorderAndPaddingExtent() : flexItem.verticalBorderAndPaddingExtent();
    } else {
        // We need to subtract the border and padding extent from the cross axis.
        // Furthermore, the sizing calculations that floor the content box size at zero when applying box-sizing are also ignored.
        // https://drafts.csswg.org/css-flexbox/#algo-main-item.
        if (flexItem.style().boxSizing() == BoxSizing::BorderBox)
            crossSize -= isHorizontalFlow() ? flexItem.verticalBorderAndPaddingExtent() : flexItem.horizontalBorderAndPaddingExtent();
    }

    auto preferredAspectRatio = preferredAspectRatioForFlexItem(flexItem);
    return std::max(0_lu, LayoutUnit(crossSize * preferredAspectRatio) - borderAndPadding);
}

void RenderFlexibleBox::setFlowAwareLocationForFlexItem(RenderBox& flexItem, const LayoutPoint& location)
{
    if (isHorizontalFlow())
        flexItem.setLocation(location);
    else
        flexItem.setLocation(location.transposedPoint());
}

template<typename SizeType> bool RenderFlexibleBox::canComputePercentageFlexBasis(const RenderBox& flexItem, const SizeType& flexBasis, UpdatePercentageHeightDescendants updateDescendants)
{
    if (!isColumnFlow() || m_hasDefiniteHeight == SizeDefiniteness::Definite)
        return true;
    if (m_hasDefiniteHeight == SizeDefiniteness::Indefinite)
        return false;

    auto isPercentResolveSuspended = view().frameView().layoutContext().isPercentHeightResolveDisabledFor(flexItem);
    ASSERT(!isPercentResolveSuspended || is<RenderBlock>(flexItem));

    bool definite = !isPercentResolveSuspended && flexItem.computePercentageLogicalHeight(flexBasis, updateDescendants).has_value();
    if (m_inLayout && (isHorizontalWritingMode() == flexItem.isHorizontalWritingMode())) {
        // We can reach this code even while we're not laying ourselves out, such
        // as from mainSizeForPercentageResolution.
        m_hasDefiniteHeight = definite ? SizeDefiniteness::Definite : SizeDefiniteness::Indefinite;
    }
    return definite;
}

template<typename SizeType> bool RenderFlexibleBox::flexItemMainSizeIsDefinite(const RenderBox& flexItem, const SizeType& size)
{
    if constexpr (!std::same_as<SizeType, Style::MaximumSize>) {
        if (size.isAuto())
            return false;
    }
    if constexpr (std::same_as<SizeType, Style::FlexBasis>) {
        if (size.isContent())
            return false;
    }
    if (!mainAxisIsFlexItemInlineAxis(flexItem) && (size.isIntrinsic() || size.isIntrinsicKeyword()))
        return false;
    if (size.isPercentOrCalculated())
        return canComputePercentageFlexBasis(flexItem, size, UpdatePercentageHeightDescendants::No);
    return true;
}

bool RenderFlexibleBox::flexItemHasComputableAspectRatio(const RenderBox& flexItem) const
{
    if (!flexItemHasAspectRatio(flexItem))
        return false;
    return flexItem.intrinsicSize().height() || flexItem.style().hasAspectRatio() || isSVGRootWithIntrinsicAspectRatio(flexItem);
}

bool RenderFlexibleBox::flexItemHasComputableAspectRatioAndCrossSizeIsConsideredDefinite(const RenderBox& flexItem)
{
    return flexItemHasComputableAspectRatio(flexItem)
        && (flexItemCrossSizeIsDefinite(flexItem, preferredCrossSizeLengthForFlexItem(flexItem)) || flexItemCrossSizeShouldUseContainerCrossSize(flexItem));
}

bool RenderFlexibleBox::flexItemCrossSizeShouldUseContainerCrossSize(const RenderBox& flexItem) const
{
    // 9.8 https://drafts.csswg.org/css-flexbox/#definite-sizes
    // 1. If a single-line flex container has a definite cross size, the automatic preferred outer cross size of any
    // stretched flex items is the flex container's inner cross size (clamped to the flex item's min and max cross size)
    // and is considered definite.
    if (!isMultiline() && alignmentForFlexItem(flexItem) == ItemPosition::Stretch && !hasAutoMarginsInCrossAxis(flexItem) && preferredCrossSizeLengthForFlexItem(flexItem).isAuto()) {
        if (isColumnFlow())
            return true;
        // This must be kept in sync with computeMainSizeFromAspectRatioUsing().
        auto& crossSize = isHorizontalFlow() ? style().height() : style().width();
        return crossSize.isFixed() || (crossSize.isPercent() && availableLogicalHeightForPercentageComputation());  
    }
    return false;
}

template<typename SizeType> bool RenderFlexibleBox::flexItemCrossSizeIsDefinite(const RenderBox& flexItem, const SizeType& size)
{
    if constexpr (!std::same_as<SizeType, Style::MaximumSize>) {
        if (size.isAuto())
            return false;
    }

    if (size.isPercentOrCalculated()) {
        if (!mainAxisIsFlexItemInlineAxis(flexItem) || m_hasDefiniteHeight == SizeDefiniteness::Definite)
            return true;
        if (m_hasDefiniteHeight == SizeDefiniteness::Indefinite)
            return false;
        bool definite = bool(flexItem.computePercentageLogicalHeight(size));
        m_hasDefiniteHeight = definite ? SizeDefiniteness::Definite : SizeDefiniteness::Indefinite;
        return definite;
    }
    // FIXME: Eventually we should support other types of sizes here.
    // Requires updating computeMainSizeFromAspectRatioUsing.
    return size.isFixed();
}

void RenderFlexibleBox::cacheFlexItemMainSize(const RenderBox& flexItem)
{
    ASSERT(!flexItem.needsLayout());
    ASSERT(!mainAxisIsFlexItemInlineAxis(flexItem));

    auto mainSize = [&] {
        auto flexBasis = flexBasisForFlexItem(flexItem);
        if (flexBasis.isPercentOrCalculated() && !flexItemMainSizeIsDefinite(flexItem, flexBasis))
            return cachedFlexItemIntrinsicContentLogicalHeight(flexItem) + flexItem.borderAndPaddingLogicalHeight() + flexItem.scrollbarLogicalHeight();
        return flexItem.logicalHeight();
    };

    m_intrinsicSizeAlongMainAxis.set(flexItem, mainSize());
    m_relaidOutFlexItems.add(flexItem);
}

void RenderFlexibleBox::clearCachedMainSizeForFlexItem(const RenderBox& flexItem)
{
    m_intrinsicSizeAlongMainAxis.remove(flexItem);
}

// This is a RAII class that is used to temporarily set the flex basis as the child size in the main axis.
class ScopedFlexBasisAsFlexItemMainSize {
public:
    ScopedFlexBasisAsFlexItemMainSize(RenderBox& flexItem, Style::PreferredSize&& flexBasis, bool mainAxisIsInlineAxis)
        : m_flexItem(flexItem)
        , m_mainAxisIsInlineAxis(mainAxisIsInlineAxis)
    {
        if (flexBasis.isAuto())
            return;

        if (m_mainAxisIsInlineAxis)
            m_flexItem.setOverridingBorderBoxLogicalWidthForFlexBasisComputation(WTFMove(flexBasis));
        else
            m_flexItem.setOverridingBorderBoxLogicalHeightForFlexBasisComputation(WTFMove(flexBasis));
        m_didOverride = true;
    }

    ~ScopedFlexBasisAsFlexItemMainSize()
    {
        if (!m_didOverride)
            return;

        if (m_mainAxisIsInlineAxis)
            m_flexItem.clearOverridingLogicalWidthForFlexBasisComputation();
        else
            m_flexItem.clearOverridingLogicalHeightForFlexBasisComputation();
    }

private:
    RenderBox& m_flexItem;
    bool m_mainAxisIsInlineAxis { true };
    bool m_didOverride { false };
};

// https://drafts.csswg.org/css-flexbox/#algo-main-item
LayoutUnit RenderFlexibleBox::computeFlexBaseSizeForFlexItem(RenderBox& flexItem, LayoutUnit mainAxisBorderAndPadding, RelayoutChildren relayoutChildren)
{
    auto flexBasis = flexBasisForFlexItem(flexItem);
    ScopedFlexBasisAsFlexItemMainSize scoped(flexItem, flexBasis.tryPreferredSize().value_or(Style::PreferredSize { CSS::Keyword::MaxContent { } }), mainAxisIsFlexItemInlineAxis(flexItem));
    // FIXME: While we are supposed to ignore min/max here, clients of maybeCacheFlexItemMainIntrinsicSize may expect min/max constrained size.
    SetForScope<bool> computingBaseSizesScope(m_isComputingFlexBaseSizes, true);

    maybeCacheFlexItemMainIntrinsicSize(flexItem, relayoutChildren);

    // 9.2.3 A.
    if (flexItemMainSizeIsDefinite(flexItem, flexBasis))
        return std::max(0_lu, computeMainAxisExtentForFlexItem(flexItem, flexBasis).value());

    // 9.2.3 B.
    if (flexItemHasComputableAspectRatioAndCrossSizeIsConsideredDefinite(flexItem)) {
        auto& crossSizeLength = preferredCrossSizeLengthForFlexItem(flexItem);
        return adjustFlexItemSizeForAspectRatioCrossAxisMinAndMax(flexItem, computeMainSizeFromAspectRatioUsing(flexItem, crossSizeLength));
    }

    // FIXME: 9.2.3 C.
    // FIXME: 9.2.3 D.

    // 9.2.3 E.
    LayoutUnit mainAxisExtent;
    if (!mainAxisIsFlexItemInlineAxis(flexItem)) {
        ASSERT(!flexItem.needsLayout());
        ASSERT(m_intrinsicSizeAlongMainAxis.contains(flexItem));
        mainAxisExtent = m_intrinsicSizeAlongMainAxis.get(flexItem);
    } else {
        // We don't need to add scrollbarLogicalWidth here because the preferred
        // width includes the scrollbar, even for overflow: auto.
        mainAxisExtent = flexItem.maxPreferredLogicalWidth();
    }
    return mainAxisExtent - mainAxisBorderAndPadding;
}

void RenderFlexibleBox::performFlexLayout(RelayoutChildren relayoutChildren)
{
    if (layoutUsingFlexFormattingContext())
        return;

    // Set up our master list of flex items. All of the rest of the algorithm
    // should work off this list of a subset.
    // FIXME: That second part is not yet true.
    FlexLayoutItems allItems;
    for (auto* flexItem = m_orderIterator.first(); flexItem; flexItem = m_orderIterator.next()) {
        if (m_orderIterator.shouldSkipChild(*flexItem)) {
            // Out-of-flow children are not flex items, so we skip them here.
            if (flexItem->isOutOfFlowPositioned())
                prepareFlexItemForPositionedLayout(*flexItem);
            continue;
        }
        allItems.append(constructFlexLayoutItem(*flexItem, relayoutChildren));
        // constructFlexItem() might set the override containing block height so any value cached for definiteness might be incorrect.
        resetHasDefiniteHeight();
    }

    if (allItems.isEmpty()) {
        if (hasLineIfEmpty()) {
            auto minHeight = borderAndPaddingLogicalHeight() + lineHeight() + scrollbarLogicalHeight();
            if (height() < minHeight)
                setLogicalHeight(minHeight);
        }
        updateLogicalHeight();
        return;
    }

    FlexLineStates lineStates;
    const LayoutUnit lineBreakLength = mainAxisContentExtent(LayoutUnit::max());
    LayoutUnit gapBetweenItems = computeGap(GapType::BetweenItems);
    LayoutUnit gapBetweenLines = computeGap(GapType::BetweenLines);
    LayoutUnit crossAxisOffset = flowAwareBorderBefore() + flowAwarePaddingBefore();
    size_t nextIndex = 0;
    size_t numLines = 0;
    InspectorInstrumentation::flexibleBoxRendererBeganLayout(*this);
    while (auto lineData = computeNextFlexLine(nextIndex, allItems, lineBreakLength, gapBetweenItems)) {
        ++numLines;
        InspectorInstrumentation::flexibleBoxRendererWrappedToNextLine(*this, nextIndex);

        auto& lineItems = lineData->lineItems;

        // Cross axis margins should only be trimmed if they are on the first/last flex line
        auto shouldTrimCrossAxisStart = shouldTrimCrossAxisMarginStart() && !lineStates.size();
        auto shouldTrimCrossAxisEnd = shouldTrimCrossAxisMarginEnd() && allItems.last().renderer.ptr() == lineItems.last().renderer.ptr();
        if (shouldTrimCrossAxisStart || shouldTrimCrossAxisEnd) {
            for (auto& flexLayoutItem : lineItems) {
                if (shouldTrimCrossAxisStart)
                    trimCrossAxisMarginStart(flexLayoutItem);
                if (shouldTrimCrossAxisEnd)
                    trimCrossAxisMarginEnd(flexLayoutItem);
            }
        }
        auto containerMainInnerSize = mainAxisContentExtent(lineData->sumHypotheticalMainSize);
        // availableFreeSpace is the initial amount of free space in this flexbox.
        // remainingFreeSpace starts out at the same value but as we place and lay
        // out flex items we subtract from it. Note that both values can be
        // negative.
        auto remainingFreeSpace = containerMainInnerSize - lineData->sumFlexBaseSize;
        auto flexSign = (lineData->sumHypotheticalMainSize < containerMainInnerSize) ? FlexSign::PositiveFlexibility : FlexSign::NegativeFlexibility;
        freezeInflexibleItems(flexSign, lineItems, remainingFreeSpace, lineData->totalFlexGrow, lineData->totalFlexShrink, lineData->totalWeightedFlexShrink);
        // The initial free space gets calculated after freezing inflexible items.
        // https://drafts.csswg.org/css-flexbox/#resolve-flexible-lengths step 3
        const LayoutUnit initialFreeSpace = remainingFreeSpace;
        while (!resolveFlexibleLengths(flexSign, lineItems, initialFreeSpace, remainingFreeSpace, lineData->totalFlexGrow, lineData->totalFlexShrink, lineData->totalWeightedFlexShrink)) {
            ASSERT(lineData->totalFlexGrow >= 0);
            ASSERT(lineData->totalWeightedFlexShrink >= 0);
        }
        
        // Recalculate the remaining free space. The adjustment for flex factors
        // between 0..1 means we can't just use remainingFreeSpace here.
        remainingFreeSpace = containerMainInnerSize;
        for (size_t i = 0; i < lineItems.size(); ++i) {
            auto& flexLayoutItem = lineItems[i];
            ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());
            remainingFreeSpace -= flexLayoutItem.flexedMarginBoxSize();
        }
        remainingFreeSpace -= (lineItems.size() - 1) * gapBetweenItems;

        // This will std::move lineItems into a newly-created LineState.
        layoutAndPlaceFlexItems(crossAxisOffset, lineItems, remainingFreeSpace, relayoutChildren, lineStates, gapBetweenItems);
    }

    if (!lineStates.isEmpty()) {
        auto isWrapReverse = style().flexWrap() == FlexWrap::Reverse;
        auto firstLineItemsCountInOriginalOrder = lineStates.first().flexLayoutItems.size();
        auto lastLineItemsCountInOriginalOrder = lineStates.first().flexLayoutItems.size();

        m_numberOfFlexItemsOnFirstLine = !isWrapReverse ? firstLineItemsCountInOriginalOrder : lastLineItemsCountInOriginalOrder;
        m_numberOfFlexItemsOnLastLine = !isWrapReverse ? lastLineItemsCountInOriginalOrder : firstLineItemsCountInOriginalOrder;
    }

    if (hasLineIfEmpty()) {
        // Even if computeNextFlexLine returns true, the flexbox might not have
        // a line because all our children might be out of flow positioned.
        // Instead of just checking if we have a line, make sure the flexbox
        // has at least a line's worth of height to cover this case.
        LayoutUnit minHeight = borderAndPaddingLogicalHeight() + lineHeight() + scrollbarLogicalHeight();
        if (size().height() < minHeight)
            setLogicalHeight(minHeight);
    }

    if (!isColumnFlow() && numLines > 1)
        setLogicalHeight(logicalHeight() + computeGap(GapType::BetweenLines) * (numLines - 1));

    updateLogicalHeight();
    repositionLogicalHeightDependentFlexItems(lineStates, gapBetweenLines);
}

std::optional<RenderFlexibleBox::FlexingLineData> RenderFlexibleBox::computeNextFlexLine(size_t& nextIndex, const FlexLayoutItems& allItems, LayoutUnit lineBreakLength, LayoutUnit gapBetweenItems)
{
    if (nextIndex >= allItems.size())
        return { };

    FlexingLineData lineData;
    // Trim main axis margin for item at the start of the flex line
    if (nextIndex < allItems.size() && shouldTrimMainAxisMarginStart())
        trimMainAxisMarginStart(allItems[nextIndex]);
    for (; nextIndex < allItems.size(); ++nextIndex) {
        const auto& flexLayoutItem = allItems[nextIndex];
        auto& style = flexLayoutItem.style();
        ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());
        if (isMultiline() && (lineData.sumHypotheticalMainSize + flexLayoutItem.hypotheticalMainAxisMarginBoxSize() > lineBreakLength && !canFitItemWithTrimmedMarginEnd(flexLayoutItem, lineData.sumHypotheticalMainSize, lineBreakLength)) && !lineData.lineItems.isEmpty())
            break;
        lineData.lineItems.append(flexLayoutItem);
        lineData.sumFlexBaseSize += flexLayoutItem.flexBaseMarginBoxSize() + gapBetweenItems;
        lineData.totalFlexGrow += style.flexGrow();
        lineData.totalFlexShrink += style.flexShrink();
        lineData.totalWeightedFlexShrink += style.flexShrink() * flexLayoutItem.flexBaseContentSize;
        lineData.sumHypotheticalMainSize += flexLayoutItem.hypotheticalMainAxisMarginBoxSize() + gapBetweenItems;
    }

    if (!lineData.lineItems.isEmpty()) {
        // We added a gap after every item but there shouldn't be one after the last item, so subtract it here. Note that
        // sums might be negative here due to negative margins in flex items.
        lineData.sumHypotheticalMainSize -= gapBetweenItems;
        lineData.sumFlexBaseSize -= gapBetweenItems;
    }

    ASSERT(lineData.lineItems.size() > 0 || nextIndex == allItems.size());
    // Trim main axis margin for item at the end of the flex line
    if (lineData.lineItems.size() && shouldTrimMainAxisMarginEnd()) {
        auto lastItem = lineData.lineItems.last();
        removeMarginEndFromFlexSizes(lastItem, lineData.sumFlexBaseSize, lineData.sumHypotheticalMainSize);
        trimMainAxisMarginEnd(lastItem);
    }
    return lineData;
}

LayoutUnit RenderFlexibleBox::autoMarginOffsetInMainAxis(const FlexLayoutItems& flexLayoutItems, LayoutUnit& availableFreeSpace)
{
    if (availableFreeSpace <= 0_lu)
        return 0_lu;
    
    int numberOfAutoMargins = 0;
    bool isHorizontal = isHorizontalFlow();
    for (auto& flexLayoutItem : flexLayoutItems) {
        auto& flexItemStyle = flexLayoutItem.style();
        ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());
        if (isHorizontal) {
            if (flexItemStyle.marginLeft().isAuto())
                ++numberOfAutoMargins;
            if (flexItemStyle.marginRight().isAuto())
                ++numberOfAutoMargins;
        } else {
            if (flexItemStyle.marginTop().isAuto())
                ++numberOfAutoMargins;
            if (flexItemStyle.marginBottom().isAuto())
                ++numberOfAutoMargins;
        }
    }
    if (!numberOfAutoMargins)
        return 0_lu;
    
    LayoutUnit sizeOfAutoMargin = availableFreeSpace / numberOfAutoMargins;
    availableFreeSpace = 0_lu;
    return sizeOfAutoMargin;
}

void RenderFlexibleBox::updateAutoMarginsInMainAxis(RenderBox& flexItem, LayoutUnit autoMarginOffset)
{
    ASSERT(autoMarginOffset >= 0_lu);
    
    if (isHorizontalFlow()) {
        if (flexItem.style().marginLeft().isAuto())
            flexItem.setMarginLeft(autoMarginOffset);
        if (flexItem.style().marginRight().isAuto())
            flexItem.setMarginRight(autoMarginOffset);
    } else {
        if (flexItem.style().marginTop().isAuto())
            flexItem.setMarginTop(autoMarginOffset);
        if (flexItem.style().marginBottom().isAuto())
            flexItem.setMarginBottom(autoMarginOffset);
    }
}

bool RenderFlexibleBox::hasAutoMarginsInCrossAxis(const RenderBox& flexItem) const
{
    if (isHorizontalFlow())
        return flexItem.style().marginTop().isAuto() || flexItem.style().marginBottom().isAuto();
    return flexItem.style().marginLeft().isAuto() || flexItem.style().marginRight().isAuto();
}

LayoutUnit RenderFlexibleBox::availableAlignmentSpaceForFlexItem(LayoutUnit lineCrossAxisExtent, const RenderBox& flexItem)
{
    LayoutUnit flexItemCrossExtent = crossAxisMarginExtentForFlexItem(flexItem) + crossAxisExtentForFlexItem(flexItem);
    return lineCrossAxisExtent - flexItemCrossExtent;
}

bool RenderFlexibleBox::updateAutoMarginsInCrossAxis(RenderBox& flexItem, LayoutUnit availableAlignmentSpace)
{
    ASSERT(!flexItem.isOutOfFlowPositioned());
    ASSERT(availableAlignmentSpace >= 0_lu);
    
    bool isHorizontal = isHorizontalFlow();
    auto& topOrLeft = isHorizontal ? flexItem.style().marginTop() : flexItem.style().marginLeft();
    auto& bottomOrRight = isHorizontal ? flexItem.style().marginBottom() : flexItem.style().marginRight();
    if (topOrLeft.isAuto() && bottomOrRight.isAuto()) {
        adjustAlignmentForFlexItem(flexItem, availableAlignmentSpace / 2);
        if (isHorizontal) {
            flexItem.setMarginTop(availableAlignmentSpace / 2);
            flexItem.setMarginBottom(availableAlignmentSpace / 2);
        } else {
            flexItem.setMarginLeft(availableAlignmentSpace / 2);
            flexItem.setMarginRight(availableAlignmentSpace / 2);
        }
        return true;
    }
    bool shouldAdjustTopOrLeft = true;
    if (isColumnFlow() && flexItem.writingMode().isInlineFlipped()) {
        // For column flows, only make this adjustment if topOrLeft corresponds to
        // the "before" margin, so that flipForRightToLeftColumn will do the right
        // thing.
        shouldAdjustTopOrLeft = false;
    }
    if (!isColumnFlow() && flexItem.writingMode().isBlockFlipped()) {
        // If we are a flipped writing mode, we need to adjust the opposite side.
        // This is only needed for row flows because this only affects the
        // block-direction axis.
        shouldAdjustTopOrLeft = false;
    }
    
    if (topOrLeft.isAuto()) {
        if (shouldAdjustTopOrLeft)
            adjustAlignmentForFlexItem(flexItem, availableAlignmentSpace);
        
        if (isHorizontal)
            flexItem.setMarginTop(availableAlignmentSpace);
        else
            flexItem.setMarginLeft(availableAlignmentSpace);
        return true;
    }

    if (bottomOrRight.isAuto()) {
        if (!shouldAdjustTopOrLeft)
            adjustAlignmentForFlexItem(flexItem, availableAlignmentSpace);
        
        if (isHorizontal)
            flexItem.setMarginBottom(availableAlignmentSpace);
        else
            flexItem.setMarginRight(availableAlignmentSpace);
        return true;
    }
    return false;
}

LayoutUnit RenderFlexibleBox::marginBoxAscentForFlexItem(const RenderBox& flexItem)
{
    auto isHorizontalFlow = this->isHorizontalFlow();
    auto direction = isHorizontalFlow ? HorizontalLine : VerticalLine;

    if (!mainAxisIsFlexItemInlineAxis(flexItem))
        return synthesizedBaseline(flexItem, style(), direction, BorderBox) + flowAwareMarginBeforeForFlexItem(flexItem);
    auto ascent = alignmentForFlexItem(flexItem) == ItemPosition::LastBaseline ? flexItem.lastLineBaseline() : flexItem.firstLineBaseline();
    if (!ascent)
        return synthesizedBaseline(flexItem, style(), direction, BorderBox) + flowAwareMarginBeforeForFlexItem(flexItem);

    if (!flexItem.writingMode().isBlockMatchingAny(writingMode())) {
        // Baseline from flex item with opposite block direction needs to be resolved as if flex item had the same block direction.
        //  _____________________________ <- flex box top/left (e.g. writing-mode: vertical-rl)
        // |        __________________   |
        // |       |  20px |    80px  |<-- flex item with vertical-lr (top is at visual left)
        // |       |<----->|<-------->|  |
        // |       top     baseline   |  |
        // where computed baseline is 20px and resolved (as if flex item shares the block direction with flex box) is 80px.
        ascent = flexItem.logicalHeight() - *ascent;
    }

    if (isHorizontalFlow ? flexItem.isScrollContainerY() : flexItem.isScrollContainerX())
        return std::max(0_lu, std::min(*ascent, crossAxisExtentForFlexItem(flexItem))) + flowAwareMarginBeforeForFlexItem(flexItem);
    return *ascent + flowAwareMarginBeforeForFlexItem(flexItem);;
}

LayoutUnit RenderFlexibleBox::computeFlexItemMarginValue(const Style::MarginEdge& margin)
{
    // When resolving the margins, we use the content size for resolving percent and calc (for percents in calc expressions) margins.
    // Fortunately, percent margins are always computed with respect to the block's width, even for margin-top and margin-bottom.
    return Style::evaluateMinimum(margin, contentBoxLogicalWidth());
}

void RenderFlexibleBox::prepareOrderIteratorAndMargins()
{
    OrderIteratorPopulator populator(m_orderIterator);

    for (auto& flexItem : childrenOfType<RenderBox>(*this)) {
        if (!populator.collectChild(flexItem))
            continue;

        // Before running the flex algorithm, 'auto' has a margin of 0.
        // Also, if we're not auto sizing, we don't do a layout that computes the start/end margins.
        if (isHorizontalFlow()) {
            flexItem.setMarginLeft(computeFlexItemMarginValue(flexItem.style().marginLeft()));
            flexItem.setMarginRight(computeFlexItemMarginValue(flexItem.style().marginRight()));
        } else {
            flexItem.setMarginTop(computeFlexItemMarginValue(flexItem.style().marginTop()));
            flexItem.setMarginBottom(computeFlexItemMarginValue(flexItem.style().marginBottom()));
        }
    }
}

std::pair<LayoutUnit, LayoutUnit> RenderFlexibleBox::computeFlexItemMinMaxSizes(RenderBox& flexItem)
{
    auto max = maxMainSizeLengthForFlexItem(flexItem);
    std::optional<LayoutUnit> maxExtent = std::nullopt;
    if (max.isSpecified() || max.isIntrinsic())
        maxExtent = computeMainAxisExtentForFlexItem(flexItem, max);

    auto min = minMainSizeLengthForFlexItem(flexItem);
    // Intrinsic sizes in child's block axis are handled by the min-size:auto code path.
    if (min.isSpecified() || (min.isIntrinsic() && mainAxisIsFlexItemInlineAxis(flexItem))) {
        auto minExtent = computeMainAxisExtentForFlexItem(flexItem, min).value_or(0_lu);
        // We must never return a min size smaller than the min preferred size for tables.
        if (flexItem.isRenderTable() && mainAxisIsFlexItemInlineAxis(flexItem))
            minExtent = std::max(minExtent, flexItem.minPreferredLogicalWidth());
        return { minExtent, maxExtent.value_or(LayoutUnit::max()) };
    }
    
    if (shouldApplyMinSizeAutoForFlexItem(flexItem)) {
        // FIXME: If the min value is expected to be valid here, we need to come up with a non optional version of computeMainAxisExtentForFlexItem and
        // ensure it's valid through the virtual calls of computeIntrinsicLogicalContentHeightUsing.
        LayoutUnit contentSize;
        auto& flexItemCrossSizeLength = preferredCrossSizeLengthForFlexItem(flexItem);

        bool canComputeSizeThroughAspectRatio = flexItem.isRenderReplaced() && flexItemHasComputableAspectRatio(flexItem) && flexItemCrossSizeIsDefinite(flexItem, flexItemCrossSizeLength);

        if (canComputeSizeThroughAspectRatio)
            contentSize = computeMainSizeFromAspectRatioUsing(flexItem, flexItemCrossSizeLength);
        else
            contentSize = computeMainAxisExtentForFlexItem(flexItem, Style::MinimumSize { CSS::Keyword::MinContent { } }).value_or(0_lu);

        if (flexItemHasAspectRatio(flexItem))
            contentSize = adjustFlexItemSizeForAspectRatioCrossAxisMinAndMax(flexItem, contentSize);
        ASSERT(contentSize >= 0);
        contentSize = std::min(contentSize, maxExtent.value_or(contentSize));
        
        auto mainSize = preferredMainSizeLengthForFlexItem(flexItem);
        if (flexItemMainSizeIsDefinite(flexItem, mainSize)) {
            LayoutUnit resolvedMainSize = computeMainAxisExtentForFlexItem(flexItem, mainSize).value_or(0);
            ASSERT(resolvedMainSize >= 0);
            LayoutUnit specifiedSize = std::min(resolvedMainSize, maxExtent.value_or(resolvedMainSize));
            return { std::min(specifiedSize, contentSize), maxExtent.value_or(LayoutUnit::max()) };
        }

        if (flexItem.isRenderReplaced() && flexItemHasComputableAspectRatioAndCrossSizeIsConsideredDefinite(flexItem)) {
            LayoutUnit transferredSize = computeMainSizeFromAspectRatioUsing(flexItem, flexItemCrossSizeLength);
            transferredSize = adjustFlexItemSizeForAspectRatioCrossAxisMinAndMax(flexItem, transferredSize);
            return { std::min(transferredSize, contentSize), maxExtent.value_or(LayoutUnit::max()) };
        }

        return { contentSize, maxExtent.value_or(LayoutUnit::max()) };
    }

    return { 0_lu, maxExtent.value_or(LayoutUnit::max()) };
}

bool RenderFlexibleBox::canUseFlexItemForPercentageResolution(const RenderBox& flexItem)
{
    ASSERT(flexItem.isFlexItem());

    auto canUseByLayoutPhase = [&] {
        if (m_inSimplifiedLayout) {
            // While in simplified layout, we should only re-compute overflow and/or re-position out-of-flow boxes, some renderers (e.g. RenderReplaced and subclasses)
            // currently ignore this optimization and run regular layout.
            return true;
        }

        if (m_inPostFlexUpdateScrollbarLayout) {
            // Unfortunately we run layout on flex content _after_ performing flex layout to ensure scrollbars are up to date (see endAndCommitUpdateScrollInfoAfterLayoutTransaction/updateScrollInfoAfterLayout).
            // We need to let this content run percent resolution as if we were still in flex item layout.
            return true;
        }

        if (m_inFlexItemLayout) {
            // While running flex _item_ layout, we may only resolve percentage against the flex item when it is orthogonal to the flex container.
            return !mainAxisIsFlexItemInlineAxis(flexItem);
        }

        if (m_inFlexItemIntrinsicWidthComputation)
            return flexItemCrossSizeShouldUseContainerCrossSize(flexItem) && !isFlexItem();

        if (m_inCrossAxisLayout)
            return true;

        if (&flexItem == view().frameView().layoutContext().subtreeLayoutRoot()) {
            ASSERT(!needsLayout());
            // When the flex item is the root of a subtree layout, flex layout is not running (as we only layout the flex item's subtree).
            return false;
        }

        // Let's decide based on style when we are outside of layout (i.e. relative percent position).
        return !m_inLayout;
    };
    if (!canUseByLayoutPhase())
        return false;

    auto canUseByStyle = [&] {
        if (mainAxisIsFlexItemInlineAxis(flexItem))
            return alignmentForFlexItem(flexItem) == ItemPosition::Stretch;

        if (flexItem.style().flexGrow() == RenderStyle::initialFlexGrow() && flexItem.style().flexShrink() == 0.0f && flexItemMainSizeIsDefinite(flexItem, flexBasisForFlexItem(flexItem)))
            return true;

        return canComputePercentageFlexBasis(flexItem, Style::PreferredSize { 0_css_percentage }, UpdatePercentageHeightDescendants::Yes);
    };
    return canUseByStyle();
}

// This method is only called whenever a descendant of a flex item wants to resolve a percentage in its
// block axis (logical height). The key here is that percentages should be generally resolved before the
// flex item is flexed, meaning that they shouldn't be recomputed once the flex item has been flexed. There
// are some exceptions though that are implemented here, like the case of fully inflexible items with
// definite flex-basis, or whenever the flex container has a definite main size. See
// https://drafts.csswg.org/css-flexbox/#definite-sizes for additional details.
std::optional<LayoutUnit> RenderFlexibleBox::usedFlexItemOverridingLogicalHeightForPercentageResolution(const RenderBox& flexItem)
{
    return canUseFlexItemForPercentageResolution(flexItem) ? flexItem.overridingBorderBoxLogicalHeight() : std::nullopt;
}

LayoutUnit RenderFlexibleBox::adjustFlexItemSizeForAspectRatioCrossAxisMinAndMax(const RenderBox& flexItem, LayoutUnit flexItemSize)
{
    auto& crossMin = minCrossSizeLengthForFlexItem(flexItem);
    auto& crossMax = maxCrossSizeLengthForFlexItem(flexItem);

    if (flexItemCrossSizeIsDefinite(flexItem, crossMax)) {
        LayoutUnit maxValue = computeMainSizeFromAspectRatioUsing(flexItem, crossMax);
        flexItemSize = std::min(maxValue, flexItemSize);
    }

    if (flexItemCrossSizeIsDefinite(flexItem, crossMin)) {
        LayoutUnit minValue = computeMainSizeFromAspectRatioUsing(flexItem, crossMin);
        flexItemSize = std::max(minValue, flexItemSize);
    }
    
    return flexItemSize;
}

void RenderFlexibleBox::maybeCacheFlexItemMainIntrinsicSize(RenderBox& flexItem, RelayoutChildren relayoutChildren)
{
    if (!flexItemHasIntrinsicMainAxisSize(flexItem))
        return;

    // If this condition is true, then computeMainAxisExtentForFlexItem will call
    // flexItem.intrinsicContentLogicalHeight() and flexItem.scrollbarLogicalHeight(),
    // so if the child has intrinsic min/max/preferred size, run layout on it now to make sure
    // its logical height and scroll bars are up to date.
    updateBlockChildDirtyBitsBeforeLayout(relayoutChildren, flexItem);
    // Don't resolve percentages in children. This is especially important for the min-height calculation,
    // where we want percentages to be treated as auto. For flex-basis itself, this is not a problem because
    // by definition we have an indefinite flex basis here and thus percentages should not resolve.
    if (flexItem.needsLayout() || !m_intrinsicSizeAlongMainAxis.contains(flexItem)) {
        auto percentResolveDisableScope = FlexPercentResolveDisabler { view().frameView().layoutContext(), flexItem };
        flexItem.setChildNeedsLayout(MarkOnlyThis);
        flexItem.layoutIfNeeded();
        cacheFlexItemMainSize(flexItem);
    }
}

RenderFlexibleBox::FlexLayoutItem RenderFlexibleBox::constructFlexLayoutItem(RenderBox& flexItem, RelayoutChildren relayoutChildren)
{
    auto everHadLayout = flexItem.everHadLayout();
    flexItem.clearOverridingSize();
    if (CheckedPtr flexibleBox = dynamicDowncast<RenderFlexibleBox>(flexItem))
        flexibleBox->resetHasDefiniteHeight();

    if (everHadLayout && flexItem.hasTrimmedMargin(std::optional<MarginTrimType> { }))
        flexItem.clearTrimmedMarginsMarkings();
    
    if (flexItem.shouldInvalidatePreferredWidths())
        flexItem.setNeedsPreferredWidthsUpdate(MarkOnlyThis);

    LayoutUnit borderAndPadding = isHorizontalFlow() ? flexItem.horizontalBorderAndPaddingExtent() : flexItem.verticalBorderAndPaddingExtent();
    LayoutUnit innerFlexBaseSize = computeFlexBaseSizeForFlexItem(flexItem, borderAndPadding, relayoutChildren);
    LayoutUnit margin = isHorizontalFlow() ? flexItem.horizontalMarginExtent() : flexItem.verticalMarginExtent();
    return FlexLayoutItem(flexItem, innerFlexBaseSize, borderAndPadding, margin, computeFlexItemMinMaxSizes(flexItem), everHadLayout);
}
    
void RenderFlexibleBox::freezeViolations(Vector<FlexLayoutItem*, 4>& violations, LayoutUnit& availableFreeSpace, double& totalFlexGrow, double& totalFlexShrink, double& totalWeightedFlexShrink)
{
    for (size_t i = 0; i < violations.size(); ++i) {
        ASSERT(!violations[i]->frozen);
        auto& flexItemStyle = violations[i]->style();
        LayoutUnit flexItemSize = violations[i]->flexedContentSize;
        availableFreeSpace -= flexItemSize - violations[i]->flexBaseContentSize;
        totalFlexGrow -= flexItemStyle.flexGrow();
        totalFlexShrink -= flexItemStyle.flexShrink();
        totalWeightedFlexShrink -= flexItemStyle.flexShrink() * violations[i]->flexBaseContentSize;
        // totalWeightedFlexShrink can be negative when we exceed the precision of
        // a double when we initially calcuate totalWeightedFlexShrink. We then
        // subtract each child's weighted flex shrink with full precision, now
        // leading to a negative result. See
        // css3/flexbox/large-flex-shrink-assert.html
        totalWeightedFlexShrink = std::max(totalWeightedFlexShrink, 0.0);
        violations[i]->frozen = true;
    }
}
    
void RenderFlexibleBox::freezeInflexibleItems(FlexSign flexSign, FlexLayoutItems& flexLayoutItems, LayoutUnit& remainingFreeSpace, double& totalFlexGrow, double& totalFlexShrink, double& totalWeightedFlexShrink)
{
    // Per https://drafts.csswg.org/css-flexbox/#resolve-flexible-lengths step 2,
    // we freeze all items with a flex factor of 0 as well as those with a min/max
    // size violation.
    Vector<FlexLayoutItem*, 4> newInflexibleItems;
    for (auto& flexLayoutItem : flexLayoutItems) {
        ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());
        ASSERT(!flexLayoutItem.frozen);
        float flexFactor = (flexSign == FlexSign::PositiveFlexibility) ? flexLayoutItem.style().flexGrow() : flexLayoutItem.style().flexShrink();
        if (!flexFactor || (flexSign == FlexSign::PositiveFlexibility && flexLayoutItem.flexBaseContentSize > flexLayoutItem.hypotheticalMainContentSize) || (flexSign == FlexSign::NegativeFlexibility && flexLayoutItem.flexBaseContentSize < flexLayoutItem.hypotheticalMainContentSize)) {
            flexLayoutItem.flexedContentSize = flexLayoutItem.hypotheticalMainContentSize;
            newInflexibleItems.append(&flexLayoutItem);
        }
    }
    freezeViolations(newInflexibleItems, remainingFreeSpace, totalFlexGrow, totalFlexShrink, totalWeightedFlexShrink);
}

// Returns true if we successfully ran the algorithm and sized the flex items.
bool RenderFlexibleBox::resolveFlexibleLengths(FlexSign flexSign, FlexLayoutItems& flexLayoutItems, LayoutUnit initialFreeSpace, LayoutUnit& remainingFreeSpace, double& totalFlexGrow, double& totalFlexShrink, double& totalWeightedFlexShrink)
{
    LayoutUnit totalViolation;
    LayoutUnit usedFreeSpace;
    Vector<FlexLayoutItem*, 4> minViolations;
    Vector<FlexLayoutItem*, 4> maxViolations;

    double sumFlexFactors = (flexSign == FlexSign::PositiveFlexibility) ? totalFlexGrow : totalFlexShrink;
    if (sumFlexFactors > 0 && sumFlexFactors < 1) {
        LayoutUnit fractional(initialFreeSpace * sumFlexFactors);
        if (fractional.abs() < remainingFreeSpace.abs())
            remainingFreeSpace = fractional;
    }

    for (auto& flexLayoutItem : flexLayoutItems) {
        // This check also covers out-of-flow children.
        if (flexLayoutItem.frozen)
            continue;
        
        auto& flexItemStyle = flexLayoutItem.style();
        LayoutUnit flexItemSize = flexLayoutItem.flexBaseContentSize;
        double extraSpace = 0;
        if (remainingFreeSpace > 0 && totalFlexGrow > 0 && flexSign == FlexSign::PositiveFlexibility && std::isfinite(totalFlexGrow))
            extraSpace = remainingFreeSpace * flexItemStyle.flexGrow() / totalFlexGrow;
        else if (remainingFreeSpace < 0 && totalWeightedFlexShrink > 0 && flexSign == FlexSign::NegativeFlexibility && std::isfinite(totalWeightedFlexShrink) && flexItemStyle.flexShrink())
            extraSpace = remainingFreeSpace * flexItemStyle.flexShrink() * flexLayoutItem.flexBaseContentSize / totalWeightedFlexShrink;
        if (std::isfinite(extraSpace))
            flexItemSize += LayoutUnit::fromFloatRound(extraSpace);

        LayoutUnit adjustedFlexItemSize = flexLayoutItem.constrainSizeByMinMax(flexItemSize);
        ASSERT(adjustedFlexItemSize >= 0);
        flexLayoutItem.flexedContentSize = adjustedFlexItemSize;
        usedFreeSpace += adjustedFlexItemSize - flexLayoutItem.flexBaseContentSize;
        
        LayoutUnit violation = adjustedFlexItemSize - flexItemSize;
        if (violation > 0)
            minViolations.append(&flexLayoutItem);
        else if (violation < 0)
            maxViolations.append(&flexLayoutItem);
        totalViolation += violation;
    }
    
    if (totalViolation)
        freezeViolations(totalViolation < 0 ? maxViolations : minViolations, remainingFreeSpace, totalFlexGrow, totalFlexShrink, totalWeightedFlexShrink);
    else
        remainingFreeSpace -= usedFreeSpace;
    
    return !totalViolation;
}

inline ContentPosition resolveLeftRightAlignment(ContentPosition position, const RenderStyle& style, bool isReversed)
{
    if (position == ContentPosition::Left || position == ContentPosition::Right) {
        auto leftRightAxisDirection = RenderFlexibleBox::leftRightAxisDirectionFromStyle(style);
        position = (style.justifyContent().isEndward(leftRightAxisDirection, isReversed))
            ? ContentPosition::End : ContentPosition::Start;
    }
    return position;
}

static LayoutUnit initialJustifyContentOffset(const RenderStyle& style, LayoutUnit availableFreeSpace, unsigned numberOfFlexItems, bool isReversed)
{
    ContentPosition justifyContent = style.resolvedJustifyContentPosition(contentAlignmentNormalBehavior());
    ContentDistribution justifyContentDistribution = style.resolvedJustifyContentDistribution(contentAlignmentNormalBehavior());

    if (availableFreeSpace < 0 && style.justifyContent().overflow() == OverflowAlignment::Safe) {
        ASSERT(justifyContent != ContentPosition::Normal);
        justifyContent = ContentPosition::Start;
    }

    // First of all resolve Left and Right so we could convert it to their equivalent properties handled bellow.
    // If the property's axis is not parallel with either left<->right axis, this value behaves as start. Currently,
    // the only case where the property's axis is not parallel with either left<->right axis is in a column flexbox.
    // https: //www.w3.org/TR/css-align-3/#valdef-justify-content-left
    justifyContent = resolveLeftRightAlignment(justifyContent, style, isReversed);
    ASSERT(justifyContent != ContentPosition::Left);
    ASSERT(justifyContent != ContentPosition::Right);

    if (justifyContent == ContentPosition::FlexEnd
        || (justifyContent == ContentPosition::End && !isReversed)
        || (justifyContent == ContentPosition::Start && isReversed))
        return availableFreeSpace;
    if (justifyContent == ContentPosition::Center)
        return availableFreeSpace / 2;
    if (justifyContentDistribution == ContentDistribution::SpaceAround) {
        if (!numberOfFlexItems)
            return availableFreeSpace / 2;
        if (availableFreeSpace > 0)
            return availableFreeSpace / (2 * numberOfFlexItems);
        return { };
    }
    if (justifyContentDistribution == ContentDistribution::SpaceEvenly) {
        if (!numberOfFlexItems)
            return availableFreeSpace / 2;
        if (availableFreeSpace > 0)
            return availableFreeSpace / (numberOfFlexItems + 1);
        return { };
    }
    return { };
}

static LayoutUnit justifyContentSpaceBetweenFlexItems(LayoutUnit availableFreeSpace, ContentDistribution justifyContentDistribution, unsigned numberOfFlexItems)
{
    if (availableFreeSpace > 0 && numberOfFlexItems > 1) {
        if (justifyContentDistribution == ContentDistribution::SpaceBetween)
            return availableFreeSpace / (numberOfFlexItems - 1);
        if (justifyContentDistribution == ContentDistribution::SpaceAround)
            return availableFreeSpace / numberOfFlexItems;
        if (justifyContentDistribution == ContentDistribution::SpaceEvenly)
            return availableFreeSpace / (numberOfFlexItems + 1);
    }
    return 0;
}

static LayoutUnit alignmentOffset(LayoutUnit availableFreeSpace, ItemPosition position, std::optional<LayoutUnit> ascent, std::optional<LayoutUnit> maxAscent, bool isWrapReverse)
{
    switch (position) {
    case ItemPosition::Legacy:
    case ItemPosition::Auto:
    case ItemPosition::Normal:
        ASSERT_NOT_REACHED();
        break;
    case ItemPosition::Start:
    case ItemPosition::End:
    case ItemPosition::SelfStart:
    case ItemPosition::SelfEnd:
    case ItemPosition::Left:
    case ItemPosition::Right:
        ASSERT_NOT_REACHED("%u alignmentForFlexItem should have transformed this position value to something we handle below.", static_cast<uint8_t>(position));
        break;
    case ItemPosition::Stretch:
        // Actual stretching must be handled by the caller. Since wrap-reverse
        // flips cross start and cross end, stretch children should be aligned
        // with the cross end. This matters because applyStretchAlignment
        // doesn't always stretch or stretch fully (explicit cross size given, or
        // stretching constrained by max-height/max-width). For flex-start and
        // flex-end this is handled by alignmentForFlexItem().
        if (isWrapReverse)
            return availableFreeSpace;
        break;
    case ItemPosition::FlexStart:
        break;
    case ItemPosition::FlexEnd:
        return availableFreeSpace;
    case ItemPosition::Center:
    case ItemPosition::AnchorCenter:
        return availableFreeSpace / 2;
    case ItemPosition::Baseline:
    case ItemPosition::LastBaseline: 
        return maxAscent.value_or(0_lu) - ascent.value_or(0_lu);
    }
    return 0;
}

void RenderFlexibleBox::setOverridingMainSizeForFlexItem(RenderBox& flexItem, LayoutUnit preferredSize)
{
    if (mainAxisIsFlexItemInlineAxis(flexItem))
        flexItem.setOverridingBorderBoxLogicalWidth(preferredSize + flexItem.borderAndPaddingLogicalWidth());
    else
        flexItem.setOverridingBorderBoxLogicalHeight(preferredSize + flexItem.borderAndPaddingLogicalHeight());
}

LayoutUnit RenderFlexibleBox::staticMainAxisPositionForPositionedFlexItem(const RenderBox& flexItem)
{
    auto flexItemMainExtent = mainAxisMarginExtentForFlexItem(flexItem) + mainAxisExtentForFlexItem(flexItem);
    auto availableSpace = mainAxisContentExtent(contentBoxLogicalHeight()) - flexItemMainExtent;
    auto isReverse = isColumnOrRowReverse();
    LayoutUnit offset = initialJustifyContentOffset(style(), availableSpace, { }, isReverse);
    if (isReverse)
        offset = availableSpace - offset;
    return offset;
}

LayoutUnit RenderFlexibleBox::staticCrossAxisPositionForPositionedFlexItem(const RenderBox& flexItem)
{
    auto availableSpace = availableAlignmentSpaceForFlexItem(crossAxisContentExtent(), flexItem);
    auto safety = overflowAlignmentForFlexItem(flexItem);
    auto align = alignmentForFlexItem(flexItem);
    if (availableSpace < 0 && safety == OverflowAlignment::Safe)
        align = ItemPosition::FlexStart;
    return alignmentOffset(availableSpace, align, std::nullopt, std::nullopt, style().flexWrap() == FlexWrap::Reverse);
}

LayoutUnit RenderFlexibleBox::staticInlinePositionForPositionedFlexItem(const RenderBox& flexItem)
{
    return startOffsetForContent() + (isColumnFlow() ? staticCrossAxisPositionForPositionedFlexItem(flexItem) : staticMainAxisPositionForPositionedFlexItem(flexItem));
}

LayoutUnit RenderFlexibleBox::staticBlockPositionForPositionedFlexItem(const RenderBox& flexItem)
{
    return borderAndPaddingBefore() + (isColumnFlow() ? staticMainAxisPositionForPositionedFlexItem(flexItem) : staticCrossAxisPositionForPositionedFlexItem(flexItem));
}

bool RenderFlexibleBox::setStaticPositionForPositionedLayout(const RenderBox& flexItem)
{
    bool positionChanged = false;
    auto* layer = flexItem.layer();
    if (flexItem.style().hasStaticInlinePosition(writingMode().isHorizontal())) {
        LayoutUnit inlinePosition = staticInlinePositionForPositionedFlexItem(flexItem);
        if (layer->staticInlinePosition() != inlinePosition) {
            layer->setStaticInlinePosition(inlinePosition);
            positionChanged = true;
        }
    }
    if (flexItem.style().hasStaticBlockPosition(writingMode().isHorizontal())) {
        LayoutUnit blockPosition = staticBlockPositionForPositionedFlexItem(flexItem);
        if (layer->staticBlockPosition() != blockPosition) {
            layer->setStaticBlockPosition(blockPosition);
            positionChanged = true;
        }
    }
    return positionChanged;
}

// This refers to https://drafts.csswg.org/css-flexbox-1/#definite-sizes, section 1).
LayoutUnit RenderFlexibleBox::computeCrossSizeForFlexItemUsingContainerCrossSize(const RenderBox& flexItem) const
{
    if (isColumnFlow())
        return contentBoxLogicalWidth();

    // Keep this sync'ed with flexItemCrossSizeShouldUseContainerCrossSize().
    auto definiteSizeValue = [&] {
        // Let's compute the definite size value for the flex item (value that we can resolve without running layout).
        auto isHorizontal = isHorizontalFlow();
        auto size = isHorizontal ? style().height() : style().width();
        ASSERT(size.isFixed() || (size.isPercent() && availableLogicalHeightForPercentageComputation()));
        LayoutUnit definiteValue;
        if (auto fixedSize = size.tryFixed())
            definiteValue = LayoutUnit { fixedSize->value };
        else if (size.isPercent())
            definiteValue = availableLogicalHeightForPercentageComputation().value_or(0_lu);

        auto maximumSize = isHorizontal ? style().maxHeight() : style().maxWidth();
        if (auto fixedMaximumSize = maximumSize.tryFixed())
            definiteValue = std::min(definiteValue, LayoutUnit { fixedMaximumSize->value });

        auto minimumSize = isHorizontal ? style().minHeight() : style().minWidth();
        if (auto fixedMinimumSize = minimumSize.tryFixed())
            definiteValue = std::max(definiteValue, LayoutUnit { fixedMinimumSize->value });

        return definiteValue;
    };
    return std::max(0_lu, definiteSizeValue() - crossAxisMarginExtentForFlexItem(flexItem));
}

void RenderFlexibleBox::prepareFlexItemForPositionedLayout(RenderBox& flexItem)
{
    ASSERT(flexItem.isOutOfFlowPositioned());
    flexItem.containingBlock()->addOutOfFlowBox(flexItem);
    auto* layer = flexItem.layer();
    LayoutUnit staticInlinePosition = flowAwareBorderStart() + flowAwarePaddingStart();
    if (layer->staticInlinePosition() != staticInlinePosition) {
        layer->setStaticInlinePosition(staticInlinePosition);
        if (flexItem.style().hasStaticInlinePosition(writingMode().isHorizontal()))
            flexItem.setChildNeedsLayout(MarkOnlyThis);
    }

    LayoutUnit staticBlockPosition = flowAwareBorderBefore() + flowAwarePaddingBefore();
    if (layer->staticBlockPosition() != staticBlockPosition) {
        layer->setStaticBlockPosition(staticBlockPosition);
        if (flexItem.style().hasStaticBlockPosition(writingMode().isHorizontal()))
            flexItem.setChildNeedsLayout(MarkOnlyThis);
    }
}

inline OverflowAlignment RenderFlexibleBox::overflowAlignmentForFlexItem(const RenderBox& flexItem) const
{
    return flexItem.style().resolvedAlignSelf(&style(), selfAlignmentNormalBehavior()).overflow();
}

ItemPosition RenderFlexibleBox::alignmentForFlexItem(const RenderBox& flexItem) const
{
    ItemPosition align = flexItem.style().resolvedAlignSelf(&style(), selfAlignmentNormalBehavior()).position();
    ASSERT(align != ItemPosition::Auto && align != ItemPosition::Normal);
    // Left and Right are only for justify-*.
    ASSERT(align != ItemPosition::Left && align != ItemPosition::Right);

    // We can safely return here because start/end are not affected by a reversed flex-wrap because the
    // alignment container is the flex line, and in a wrap reversed flex container the start and end within
    // a flex line are still the same. Contrary to this flex-start/flex-end depend on the flex container
    // start/end edges which are flipped in the case of wrap-reverse.
    if (align == ItemPosition::Start)
        return ItemPosition::FlexStart;
    if (align == ItemPosition::End)
        return ItemPosition::FlexEnd;

    if (align == ItemPosition::SelfStart || align == ItemPosition::SelfEnd) {
        bool hasSameDirection = isHorizontalFlow()
            ? writingMode().isAnyTopToBottom() == flexItem.writingMode().isAnyTopToBottom()
            : writingMode().isAnyLeftToRight() == flexItem.writingMode().isAnyLeftToRight();
        return hasSameDirection == (align == ItemPosition::SelfStart)
            ? ItemPosition::FlexStart : ItemPosition::FlexEnd;
    }

    if (style().flexWrap() == FlexWrap::Reverse) {
        if (align == ItemPosition::FlexStart)
            align = ItemPosition::FlexEnd;
        else if (align == ItemPosition::FlexEnd)
            align = ItemPosition::FlexStart;
    }

    return align;
}

void RenderFlexibleBox::resetAutoMarginsAndLogicalTopInCrossAxis(RenderBox& flexItem)
{
    if (hasAutoMarginsInCrossAxis(flexItem)) {
        flexItem.updateLogicalHeight();
        if (isHorizontalFlow()) {
            if (flexItem.style().marginTop().isAuto())
                flexItem.setMarginTop(0_lu);
            if (flexItem.style().marginBottom().isAuto())
                flexItem.setMarginBottom(0_lu);
        } else {
            if (flexItem.style().marginLeft().isAuto())
                flexItem.setMarginLeft(0_lu);
            if (flexItem.style().marginRight().isAuto())
                flexItem.setMarginRight(0_lu);
        }
    }
}

bool RenderFlexibleBox::needToStretchFlexItemLogicalHeight(const RenderBox& flexItem) const
{
    // This function is a little bit magical. It relies on the fact that blocks
    // intrinsically "stretch" themselves in their inline axis, i.e. a <div> has
    // an implicit width: 100%. So the child will automatically stretch if our
    // cross axis is the child's inline axis. That's the case if:
    // - We are horizontal and the child is in vertical writing mode
    // - We are vertical and the child is in horizontal writing mode
    // Otherwise, we need to stretch if the cross axis size is auto.
    if (alignmentForFlexItem(flexItem) != ItemPosition::Stretch)
        return false;

    if (isHorizontalFlow() != flexItem.isHorizontalWritingMode())
        return false;

    // Aspect ratio is properly handled by RenderReplaced during layout.
    if (flexItem.isRenderReplaced() && flexItemHasAspectRatio(flexItem))
        return false;

    return flexItem.style().logicalHeight().isAuto();
}

bool RenderFlexibleBox::flexItemHasIntrinsicMainAxisSize(const RenderBox& flexItem)
{
    if (mainAxisIsFlexItemInlineAxis(flexItem))
        return false;

    auto flexBasis = flexBasisForFlexItem(flexItem);
    auto minSize = minMainSizeLengthForFlexItem(flexItem);
    auto maxSize = maxMainSizeLengthForFlexItem(flexItem);
    // FIXME: we must run flexItemMainSizeIsDefinite() because it might end up calling computePercentageLogicalHeight()
    // which has some side effects like calling addPercentHeightDescendant() for example so it is not possible to skip
    // the call for example by moving it to the end of the conditional expression. This is error-prone and we should
    // refactor computePercentageLogicalHeight() at some point so that it only computes stuff without those side effects.
    if (!flexItemMainSizeIsDefinite(flexItem, flexBasis) || minSize.isIntrinsic() || maxSize.isIntrinsic())
        return true;

    if (shouldApplyMinSizeAutoForFlexItem(flexItem))
        return true;

    return false;
}

Overflow RenderFlexibleBox::mainAxisOverflowForFlexItem(const RenderBox& flexItem) const
{
    if (isHorizontalFlow())
        return flexItem.style().overflowX();
    return flexItem.style().overflowY();
}

Overflow RenderFlexibleBox::crossAxisOverflowForFlexItem(const RenderBox& flexItem) const
{
    if (isHorizontalFlow())
        return flexItem.style().overflowY();
    return flexItem.style().overflowX();
}

bool RenderFlexibleBox::flexItemHasPercentHeightDescendants(const RenderBox& renderer) const
{
    // FIXME: This function can be removed soon after webkit.org/b/204318 is fixed. Evaluate whether the
    // skipContainingBlockForPercentHeightCalculation() check below should be moved to the caller in that case.
    CheckedPtr renderBlock = dynamicDowncast<RenderBlock>(renderer);
    if (!renderBlock)
        return false;

    // FlexibleBoxImpl's like RenderButton might wrap their children in anonymous blocks. Those anonymous blocks are
    // skipped for percentage height calculations in RenderBox::computePercentageLogicalHeight() and thus
    // addPercentHeightDescendant() is never called for them. This means that this method would always wrongly
    // return false for a child of a <button> with a percentage height.
    if (hasPercentHeightDescendants() && skipContainingBlockForPercentHeightCalculation(renderer, isHorizontalWritingMode() != renderer.isHorizontalWritingMode())) {
        for (auto& descendant : *percentHeightDescendants()) {
            if (renderBlock->isContainingBlockAncestorFor(descendant))
                return true;
        }
    }

    if (!renderBlock->hasPercentHeightDescendants())
        return false;

    auto* percentHeightDescendants = renderBlock->percentHeightDescendants();
    if (!percentHeightDescendants)
        return false;

    for (auto& descendant : *percentHeightDescendants) {
        bool hasOutOfFlowAncestor = false;
        for (auto* ancestor = descendant.containingBlock(); ancestor && ancestor != renderBlock.get(); ancestor = ancestor->containingBlock()) {
            if (ancestor->isOutOfFlowPositioned()) {
                hasOutOfFlowAncestor = true;
                break;
            }
        }
        if (!hasOutOfFlowAncestor)
            return true;
    }
    return false;
}

static LayoutUnit contentAlignmentStartOverflow(LayoutUnit availableFreeSpace, ContentPosition position, ContentDistribution distribution, OverflowAlignment safety, bool isReverse)
{
    if (availableFreeSpace >= 0 || safety == OverflowAlignment::Safe)
        return 0_lu;

    if (distribution == ContentDistribution::SpaceAround
        || distribution == ContentDistribution::SpaceEvenly)
        return -availableFreeSpace / 2;

    switch (position) {
    case ContentPosition::Start:
    case ContentPosition::Baseline:
    case ContentPosition::LastBaseline:
        return 0_lu;
    case ContentPosition::FlexStart:
        return isReverse ? -availableFreeSpace : 0_lu;
    case ContentPosition::Center:
        return -availableFreeSpace / 2;
    case ContentPosition::End:
        return -availableFreeSpace;
    case ContentPosition::FlexEnd:
        return isReverse ? 0_lu : -availableFreeSpace;
    default:
        ASSERT((distribution == ContentDistribution::Default && position == ContentPosition::Normal) // Normal alignment.
            || distribution == ContentDistribution::Stretch
            || distribution == ContentDistribution::SpaceBetween);
        return isReverse ? -availableFreeSpace : 0_lu;
    }
}

void RenderFlexibleBox::layoutAndPlaceFlexItems(LayoutUnit& crossAxisOffset, FlexLayoutItems& flexLayoutItems, LayoutUnit availableFreeSpace, RelayoutChildren relayoutChildren, FlexLineStates& lineStates, LayoutUnit gapBetweenItems)
{
    LayoutUnit autoMarginOffset = autoMarginOffsetInMainAxis(flexLayoutItems, availableFreeSpace);
    LayoutUnit mainAxisOffset = flowAwareBorderStart() + flowAwarePaddingStart();
    mainAxisOffset += initialJustifyContentOffset(style(), availableFreeSpace, flexLayoutItems.size(), isColumnOrRowReverse());
    if (style().flexDirection() == FlexDirection::RowReverse)
        mainAxisOffset += isHorizontalFlow() ? verticalScrollbarWidth() : horizontalScrollbarHeight();

    if (availableFreeSpace < 0) {
        ContentPosition position = style().resolvedJustifyContentPosition(contentAlignmentNormalBehavior());
        ContentDistribution distribution = style().resolvedJustifyContentDistribution(contentAlignmentNormalBehavior());
        OverflowAlignment safety = style().justifyContent().overflow();
        position = resolveLeftRightAlignment(position, style(), isColumnOrRowReverse());
        LayoutUnit overflow = contentAlignmentStartOverflow(availableFreeSpace, position, distribution, safety, isColumnOrRowReverse());
        m_justifyContentStartOverflow = std::max(m_justifyContentStartOverflow, overflow);
    }

    LayoutUnit totalMainExtent = mainAxisExtent();
    LayoutUnit maxFlexItemCrossAxisExtent;

    LayoutUnit maxAscent;
    LayoutUnit maxDescent = LayoutUnit::min();
    LayoutUnit lastBaselineMaxAscent;
    std::optional<BaselineAlignmentState> baselineAlignmentState;

    ContentDistribution distribution = style().resolvedJustifyContentDistribution(contentAlignmentNormalBehavior());
    bool shouldFlipMainAxis = !isColumnFlow() && !isLeftToRightFlow();
    auto flexLayoutScope = SetForScope(m_inFlexItemLayout, true);
    for (size_t i = 0; i < flexLayoutItems.size(); ++i) {
        auto& flexLayoutItem = flexLayoutItems[i];
        auto& flexItem = flexLayoutItem.renderer.get();

        ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());

        setOverridingMainSizeForFlexItem(flexItem, flexLayoutItem.flexedContentSize);
        // The flexed content size and the override size include the scrollbar
        // width, so we need to compare to the size including the scrollbar.
        // FIXME: Should it include the scrollbar?
        if (flexLayoutItem.flexedContentSize != mainAxisContentExtentForFlexItemIncludingScrollbar(flexItem))
            flexItem.setChildNeedsLayout(MarkOnlyThis);
        else {
            // To avoid double applying margin changes in
            // updateAutoMarginsInCrossAxis, we reset the margins here.
            resetAutoMarginsAndLogicalTopInCrossAxis(flexItem);
        }
        // We may have already forced relayout for orthogonal flowing children in
        // computeInnerFlexBaseSizeForFlexItem.
        bool forceFlexItemRelayout = relayoutChildren == RelayoutChildren::Yes && !m_relaidOutFlexItems.contains(flexItem);
        if (!forceFlexItemRelayout && flexItemHasPercentHeightDescendants(flexItem)) {
            // Have to force another relayout even though the child is sized
            // correctly, because its descendants are not sized correctly yet. Our
            // previous layout of the child was done without an override height set.
            // So, redo it here.
            forceFlexItemRelayout = true;
        }
        updateFlexItemDirtyBitsBeforeLayout(forceFlexItemRelayout, flexItem);
        if (!flexItem.needsLayout())
            flexItem.markForPaginationRelayoutIfNeeded();
        if (flexItem.needsLayout())
            m_relaidOutFlexItems.add(flexItem);
        flexItem.layoutIfNeeded();
        if (!flexLayoutItem.everHadLayout && flexItem.checkForRepaintDuringLayout()) {
            flexItem.repaint();
            flexItem.repaintOverhangingFloats(true);
        }

        updateAutoMarginsInMainAxis(flexItem, autoMarginOffset);

        LayoutUnit flexItemCrossAxisMarginBoxExtent;

        auto alignment = alignmentForFlexItem(flexItem);
        if ((alignment == ItemPosition::Baseline || alignment == ItemPosition::LastBaseline) && !hasAutoMarginsInCrossAxis(flexItem)) {
            LayoutUnit ascent = marginBoxAscentForFlexItem(flexItem);
            LayoutUnit descent = (crossAxisMarginExtentForFlexItem(flexItem) + crossAxisExtentForFlexItem(flexItem)) - ascent;
            maxDescent = std::max(maxDescent, descent);

            if (!baselineAlignmentState) {
                auto alignmentContextAxis = style().isRowFlexDirection() ? LogicalBoxAxis::Inline : LogicalBoxAxis::Block;
                baselineAlignmentState = { flexItem, alignment, ascent, alignmentContextAxis, style().writingMode() };
            } else
                baselineAlignmentState->updateSharedGroup(flexItem, alignment, ascent);

            if (alignment == ItemPosition::Baseline) {
                maxAscent =  std::max(maxAscent, ascent);
                flexItemCrossAxisMarginBoxExtent = maxAscent + maxDescent;
            } else {
                lastBaselineMaxAscent = std::max(lastBaselineMaxAscent, ascent);
                flexItemCrossAxisMarginBoxExtent = lastBaselineMaxAscent + maxDescent;
            }

        } else
            flexItemCrossAxisMarginBoxExtent = crossAxisIntrinsicExtentForFlexItem(flexItem) + crossAxisMarginExtentForFlexItem(flexItem);

        if (!isColumnFlow())
            setLogicalHeight(std::max(logicalHeight(), crossAxisOffset + flowAwareBorderAfter() + flowAwarePaddingAfter() + flexItemCrossAxisMarginBoxExtent + crossAxisScrollbarExtent()));
        maxFlexItemCrossAxisExtent = std::max(maxFlexItemCrossAxisExtent, flexItemCrossAxisMarginBoxExtent);

        mainAxisOffset += flowAwareMarginStartForFlexItem(flexItem);

        LayoutUnit flexItemMainExtent = mainAxisExtentForFlexItem(flexItem);
        // In an RTL column situation, this will apply the margin-right/margin-end
        // on the left. This will be fixed later in flipForRightToLeftColumn.
        LayoutPoint location(shouldFlipMainAxis ? totalMainExtent - mainAxisOffset - flexItemMainExtent : mainAxisOffset, crossAxisOffset + flowAwareMarginBeforeForFlexItem(flexItem));
        setFlowAwareLocationForFlexItem(flexItem, location);
        mainAxisOffset += flexItemMainExtent + flowAwareMarginEndForFlexItem(flexItem);

        if (i != flexLayoutItems.size() - 1) {
            // The last item does not get extra space added.
            mainAxisOffset += justifyContentSpaceBetweenFlexItems(availableFreeSpace, distribution, flexLayoutItems.size()) + gapBetweenItems;
        }

        // FIXME: Deal with pagination.
    }

    if (isColumnFlow())
        setLogicalHeight(std::max(logicalHeight(), mainAxisOffset + flowAwareBorderEnd() + flowAwarePaddingEnd() + scrollbarLogicalHeight()));

    if (style().flexDirection() == FlexDirection::ColumnReverse) {
        // We have to do an extra pass for column-reverse to reposition the flex
        // items since the start depends on the height of the flexbox, which we
        // only know after we've positioned all the flex items.
        updateLogicalHeight();
        layoutColumnReverse(flexLayoutItems, crossAxisOffset, availableFreeSpace, gapBetweenItems);
    }

    lineStates.append(LineState(crossAxisOffset, maxFlexItemCrossAxisExtent, baselineAlignmentState, WTFMove(flexLayoutItems)));
    crossAxisOffset += maxFlexItemCrossAxisExtent;
}

void RenderFlexibleBox::layoutColumnReverse(const FlexLayoutItems& flexLayoutItems, LayoutUnit crossAxisOffset, LayoutUnit availableFreeSpace, LayoutUnit gapBetweenItems)
{
    // This is similar to the logic in layoutAndPlaceFlexItems, except we place
    // the children starting from the end of the flexbox. We also don't need to
    // layout anything since we're just moving the children to a new position.
    LayoutUnit mainAxisOffset = logicalHeight() - flowAwareBorderEnd() - flowAwarePaddingEnd();
    mainAxisOffset -= initialJustifyContentOffset(style(), availableFreeSpace, flexLayoutItems.size(), isColumnOrRowReverse());
    mainAxisOffset -= isHorizontalFlow() ? verticalScrollbarWidth() : horizontalScrollbarHeight();

    ContentDistribution distribution = style().resolvedJustifyContentDistribution(contentAlignmentNormalBehavior());

    for (size_t i = 0; i < flexLayoutItems.size(); ++i) {
        auto& flexItem = flexLayoutItems[i].renderer;
        ASSERT(!flexItem->isOutOfFlowPositioned());
        mainAxisOffset -= mainAxisExtentForFlexItem(flexItem) + flowAwareMarginEndForFlexItem(flexItem);
        setFlowAwareLocationForFlexItem(flexItem, LayoutPoint(mainAxisOffset, crossAxisOffset + flowAwareMarginBeforeForFlexItem(flexItem)));
        mainAxisOffset -= flowAwareMarginStartForFlexItem(flexItem);
        
        if (i != flexLayoutItems.size() - 1) {
            // The last item does not get extra space added.
            mainAxisOffset -= justifyContentSpaceBetweenFlexItems(availableFreeSpace, distribution, flexLayoutItems.size()) + gapBetweenItems;
        }
    }
}

static LayoutUnit initialAlignContentOffset(LayoutUnit availableFreeSpace, ContentPosition alignContent, ContentDistribution alignContentDistribution, OverflowAlignment safety, unsigned numberOfLines, bool isReversed)
{
    if (availableFreeSpace < 0 && safety == OverflowAlignment::Safe) {
        ASSERT(alignContent != ContentPosition::Normal);
        alignContent = ContentPosition::Start;
    }

    if (alignContent == ContentPosition::FlexEnd
        || (alignContent == ContentPosition::End && !isReversed)
        || (alignContent == ContentPosition::Start && isReversed))
        return availableFreeSpace;
    if (alignContent == ContentPosition::Center)
        return availableFreeSpace / 2;
    if (alignContentDistribution == ContentDistribution::SpaceAround) {
        if (availableFreeSpace > 0 && numberOfLines)
            return availableFreeSpace / (2 * numberOfLines);
        if (availableFreeSpace < 0)
            return std::max(0_lu, availableFreeSpace / 2);
    }
    if (alignContentDistribution == ContentDistribution::SpaceEvenly) {
        if (availableFreeSpace > 0)
            return availableFreeSpace / (numberOfLines + 1);
        // Fallback to 'safe center'
        return std::max(0_lu, availableFreeSpace / 2);
    }
    return 0_lu;
}

static LayoutUnit alignContentSpaceBetweenFlexItems(LayoutUnit availableFreeSpace, ContentDistribution alignContentDistribution, unsigned numberOfLines)
{
    if (availableFreeSpace > 0 && numberOfLines > 1) {
        if (alignContentDistribution == ContentDistribution::SpaceBetween)
            return availableFreeSpace / (numberOfLines - 1);
        if (alignContentDistribution == ContentDistribution::SpaceAround || alignContentDistribution == ContentDistribution::Stretch)
            return availableFreeSpace / numberOfLines;
        if (alignContentDistribution == ContentDistribution::SpaceEvenly)
            return availableFreeSpace / (numberOfLines + 1);
    }
    return 0_lu;
}

void RenderFlexibleBox::alignFlexLines(FlexLineStates& lineStates, LayoutUnit gapBetweenLines)
{
    if (lineStates.isEmpty() || !isMultiline())
        return;

    ContentPosition position = style().resolvedAlignContentPosition(contentAlignmentNormalBehavior());
    ContentDistribution distribution = style().resolvedAlignContentDistribution(contentAlignmentNormalBehavior());
    OverflowAlignment safety = style().alignContent().overflow();
    bool isWrapReverse = style().flexWrap() == FlexWrap::Reverse;

    if (position == ContentPosition::FlexStart && !gapBetweenLines && safety != OverflowAlignment::Safe && !isWrapReverse)
        return;

    size_t numLines = lineStates.size();
    LayoutUnit availableCrossAxisSpace = crossAxisContentExtent() - (numLines - 1) * gapBetweenLines;
    for (size_t i = 0; i < numLines; ++i)
        availableCrossAxisSpace -= lineStates[i].crossAxisExtent;

    m_alignContentStartOverflow = contentAlignmentStartOverflow(availableCrossAxisSpace, position, distribution, safety, isWrapReverse);
    LayoutUnit lineOffset = initialAlignContentOffset(availableCrossAxisSpace, position, distribution, safety, numLines, isWrapReverse);
    for (unsigned lineNumber = 0; lineNumber < numLines; ++lineNumber) {
        LineState& lineState = lineStates[lineNumber];
        lineState.crossAxisOffset += lineOffset;
        for (auto& flexLayoutItem : lineState.flexLayoutItems)
            adjustAlignmentForFlexItem(flexLayoutItem.renderer, lineOffset);
        
        if (distribution == ContentDistribution::Stretch && availableCrossAxisSpace > 0)
            lineStates[lineNumber].crossAxisExtent += availableCrossAxisSpace / static_cast<unsigned>(numLines);

        lineOffset += alignContentSpaceBetweenFlexItems(availableCrossAxisSpace, distribution, numLines) + gapBetweenLines;
    }
}
    
void RenderFlexibleBox::adjustAlignmentForFlexItem(RenderBox& flexItem, LayoutUnit delta)
{
    ASSERT(!flexItem.isOutOfFlowPositioned());
    setFlowAwareLocationForFlexItem(flexItem, flowAwareLocationForFlexItem(flexItem) + LayoutSize(0_lu, delta));
}
    
void RenderFlexibleBox::alignFlexItems(FlexLineStates& lineStates)
{
    for (LineState& lineState : lineStates) {
        LayoutUnit lineCrossAxisExtent = lineState.crossAxisExtent;
        auto baselineAlignmentState = lineState.baselineAlignmentState;

        if (lineState.baselineAlignmentState)
            performBaselineAlignment(lineState);

        for (auto& flexLayoutItem : lineState.flexLayoutItems) {
            ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());

            auto safety = overflowAlignmentForFlexItem(flexLayoutItem.renderer);
            auto position = alignmentForFlexItem(flexLayoutItem.renderer);
            if (updateAutoMarginsInCrossAxis(flexLayoutItem.renderer, std::max(0_lu, availableAlignmentSpaceForFlexItem(lineCrossAxisExtent, flexLayoutItem.renderer))) || position == ItemPosition::Baseline || position == ItemPosition::LastBaseline)
                continue;
            
            if (position == ItemPosition::Stretch)
                applyStretchAlignmentToFlexItem(flexLayoutItem.renderer, lineCrossAxisExtent);
            LayoutUnit availableSpace = availableAlignmentSpaceForFlexItem(lineCrossAxisExtent, flexLayoutItem.renderer);
            if (availableSpace < 0 && safety == OverflowAlignment::Safe)
                position = ItemPosition::FlexStart; // See Start == FlexStart assumption in alignmentForFlexItem().
            LayoutUnit offset = alignmentOffset(availableSpace, position, std::nullopt, std::nullopt, style().flexWrap() == FlexWrap::Reverse);
            adjustAlignmentForFlexItem(flexLayoutItem.renderer, offset);
        }
    }
}

void RenderFlexibleBox::performBaselineAlignment(LineState& lineState)
{
    ASSERT(lineState.baselineAlignmentState);

    auto lineCrossAxisExtent = lineState.crossAxisExtent;
    bool containerHasWrapReverse = style().flexWrap() == FlexWrap::Reverse;

    auto flexItemWritingModeForBaselineAlignment = [&](const RenderBox& flexItem) {
        if (mainAxisIsFlexItemInlineAxis(flexItem))
            return flexItem.style().writingMode();

        auto alignmentContextAxis = style().isRowFlexDirection() ? LogicalBoxAxis::Inline : LogicalBoxAxis::Block;
        return BaselineAlignmentState::usedWritingModeForBaselineAlignment(alignmentContextAxis, writingMode(), flexItem.writingMode());
    };

    auto shouldAdjustItemTowardsCrossAxisEnd = [&](const FlowDirection& flexItemBlockFlowDirection, ItemPosition alignment) {
        ASSERT(alignment == ItemPosition::Baseline || alignment == ItemPosition::LastBaseline);

        // The direction in which we are aligning (i.e. direction of the cross axis) must be parallel with the direction of the flex item's used writing mode
        ASSERT_IMPLIES(crossAxisDirection() == RenderFlexibleBox::Direction::TopToBottom || crossAxisDirection() == RenderFlexibleBox::Direction::BottomToTop, flexItemBlockFlowDirection == RenderFlexibleBox::Direction::TopToBottom || flexItemBlockFlowDirection == RenderFlexibleBox::Direction::BottomToTop);
        ASSERT_IMPLIES(crossAxisDirection() == RenderFlexibleBox::Direction::LeftToRight || crossAxisDirection() == RenderFlexibleBox::Direction::RightToLeft, flexItemBlockFlowDirection == RenderFlexibleBox::Direction::LeftToRight || flexItemBlockFlowDirection == RenderFlexibleBox::Direction::RightToLeft);

        // For first baseline aligned items, if its block direction is the opposite of
        // the cross axis direction, then that means its fallback alignment (safe self-start)
        // is in the direction of the end of the cross axis
        //
        // For last baseline aligned items, if its block direction is in the same direction as
        // the cross axis direction, then that means its fallback alignment (safe self-end) is
        // in the direction of the end of the cross axis
        if (alignment == ItemPosition::Baseline)
            return crossAxisDirection() != flexItemBlockFlowDirection;
        return crossAxisDirection() == flexItemBlockFlowDirection;
    };

    for (auto& baselineSharingGroup : lineState.baselineAlignmentState.value().sharedGroups()) {
        LayoutUnit minMarginAfterBaseline = LayoutUnit::max();
        for (auto& flexItem : baselineSharingGroup) {
            auto position = alignmentForFlexItem(flexItem);
            ASSERT(position == ItemPosition::Baseline || position == ItemPosition::LastBaseline);
            auto offset = alignmentOffset(availableAlignmentSpaceForFlexItem(lineCrossAxisExtent, flexItem), position, marginBoxAscentForFlexItem(flexItem), baselineSharingGroup.maxAscent(), containerHasWrapReverse);
            adjustAlignmentForFlexItem(flexItem, offset);

            if (shouldAdjustItemTowardsCrossAxisEnd(flexItemWritingModeForBaselineAlignment(flexItem).blockDirection(), position))
                minMarginAfterBaseline = std::min(minMarginAfterBaseline, availableAlignmentSpaceForFlexItem(lineCrossAxisExtent, flexItem) - offset);
        }
        // css-align-3 9.3 part 3:
        // Position the aligned baseline-sharing group within the alignment container according to its
        // fallback alignment. The fallback alignment of a baseline-sharing group is the fallback alignment
        // of its items as resolved to physical directions.
        if (minMarginAfterBaseline) {
            for (auto& flexItem : baselineSharingGroup) {
                if (shouldAdjustItemTowardsCrossAxisEnd(flexItemWritingModeForBaselineAlignment(flexItem).blockDirection(), alignmentForFlexItem(flexItem)) && !hasAutoMarginsInCrossAxis(flexItem))
                    adjustAlignmentForFlexItem(flexItem, minMarginAfterBaseline);
            }
        }
    }
}

void RenderFlexibleBox::applyStretchAlignmentToFlexItem(RenderBox& flexItem, LayoutUnit lineCrossAxisExtent)
{
    if (mainAxisIsFlexItemInlineAxis(flexItem) && flexItem.style().logicalHeight().isAuto()) {
        LayoutUnit stretchedLogicalHeight = std::max(flexItem.borderAndPaddingLogicalHeight(),
        lineCrossAxisExtent - crossAxisMarginExtentForFlexItem(flexItem));
        ASSERT(!flexItem.needsLayout());
        LayoutUnit desiredLogicalHeight = flexItem.constrainLogicalHeightByMinMax(stretchedLogicalHeight, cachedFlexItemIntrinsicContentLogicalHeight(flexItem));
        
        // FIXME: Can avoid laying out here in some cases. See https://webkit.org/b/87905.
        bool flexItemNeedsRelayout = desiredLogicalHeight != flexItem.logicalHeight();
        if (auto* block = dynamicDowncast<RenderBlock>(flexItem); block && block->hasPercentHeightDescendants() && m_relaidOutFlexItems.contains(flexItem)) {
            // Have to force another relayout even though the child is sized
            // correctly, because its descendants are not sized correctly yet. Our
            // previous layout of the child was done without an override height set.
            // So, redo it here.
            flexItemNeedsRelayout = true;
        }
        if (flexItemNeedsRelayout || !flexItem.overridingBorderBoxLogicalHeight())
            flexItem.setOverridingBorderBoxLogicalHeight(desiredLogicalHeight);
        if (flexItemNeedsRelayout) {
            SetForScope resetFlexItemLogicalHeight(m_shouldResetFlexItemLogicalHeightBeforeLayout, true);
            // We cache the child's intrinsic content logical height to avoid it being
            // reset to the stretched height.
            // FIXME: This is fragile. RenderBoxes should be smart enough to
            // determine their intrinsic content logical height correctly even when
            // there's an overrideHeight.
            LayoutUnit flexItemIntrinsicContentLogicalHeight = cachedFlexItemIntrinsicContentLogicalHeight(flexItem);
            flexItem.setChildNeedsLayout(MarkOnlyThis);
            
            // Don't use layoutChildIfNeeded to avoid setting cross axis cached size twice.
            flexItem.layoutIfNeeded();

            setCachedFlexItemIntrinsicContentLogicalHeight(flexItem, flexItemIntrinsicContentLogicalHeight);
        }
    } else if (!mainAxisIsFlexItemInlineAxis(flexItem) && flexItem.style().logicalWidth().isAuto()) {
        LayoutUnit flexItemWidth = std::max(0_lu, lineCrossAxisExtent - crossAxisMarginExtentForFlexItem(flexItem));
        flexItemWidth = flexItem.constrainLogicalWidthByMinMax(flexItemWidth, crossAxisContentExtent(), *this);
        
        if (flexItemWidth != flexItem.logicalWidth()) {
            flexItem.setOverridingBorderBoxLogicalWidth(flexItemWidth);
            flexItem.setChildNeedsLayout(MarkOnlyThis);
            flexItem.layoutIfNeeded();
        }
    }
}

void RenderFlexibleBox::flipForRightToLeftColumn(const FlexLineStates& lineStates)
{
    if (writingMode().isLogicalLeftInlineStart() || !isColumnFlow())
        return;
    
    LayoutUnit crossExtent = crossAxisExtent();
    for (size_t lineNumber = 0; lineNumber < lineStates.size(); ++lineNumber) {
        const LineState& lineState = lineStates[lineNumber];
        for (auto& flexLayoutItem : lineState.flexLayoutItems) {
            ASSERT(!flexLayoutItem.renderer->isOutOfFlowPositioned());
            
            LayoutPoint location = flowAwareLocationForFlexItem(flexLayoutItem.renderer);
            // For vertical flows, setFlowAwareLocationForFlexItem will transpose x and
            // y, so using the y axis for a column cross axis extent is correct.
            location.setY(crossExtent - crossAxisExtentForFlexItem(flexLayoutItem.renderer) - location.y());
            if (!isHorizontalWritingMode())
                location.move(LayoutSize(0, -horizontalScrollbarHeight()));
            setFlowAwareLocationForFlexItem(flexLayoutItem.renderer, location);
        }
    }
}

void RenderFlexibleBox::flipForWrapReverse(const FlexLineStates& lineStates, LayoutUnit crossAxisStartEdge)
{
    LayoutUnit contentExtent = crossAxisContentExtent();
    for (size_t lineNumber = 0; lineNumber < lineStates.size(); ++lineNumber) {
        const LineState& lineState = lineStates[lineNumber];
        for (auto& flexLayoutItem : lineState.flexLayoutItems) {
            LayoutUnit lineCrossAxisExtent = lineStates[lineNumber].crossAxisExtent;
            LayoutUnit originalOffset = lineStates[lineNumber].crossAxisOffset - crossAxisStartEdge;
            LayoutUnit newOffset = contentExtent - originalOffset - lineCrossAxisExtent;
            adjustAlignmentForFlexItem(flexLayoutItem.renderer, newOffset - originalOffset);
        }
    }
}

std::optional<TextDirection> RenderFlexibleBox::leftRightAxisDirectionFromStyle(const RenderStyle& style)
{
    if (!style.isColumnFlexDirection()) // Prioritize text direction.
        return style.writingMode().bidiDirection();

    if (style.writingMode().isVertical()) { // Fall back to block direction if possible.
        return style.writingMode().isBlockLeftToRight()
            ? TextDirection::LTR
            : TextDirection::RTL;
    }

    return std::nullopt;
}

LayoutOptionalOutsets RenderFlexibleBox::allowedLayoutOverflow() const
{
    LayoutOptionalOutsets allowance = RenderBox::allowedLayoutOverflow();

    bool isColumnar = style().isColumnFlexDirection();
    if (isHorizontalWritingMode()) {
        allowance.top() = isColumnar ? m_justifyContentStartOverflow : m_alignContentStartOverflow;
        if (writingMode().isInlineLeftToRight())
            allowance.left() = isColumnar ? m_alignContentStartOverflow : m_justifyContentStartOverflow;
        else
            allowance.right() = isColumnar ? m_alignContentStartOverflow : m_justifyContentStartOverflow;
    } else {
        allowance.left() = isColumnar ? m_justifyContentStartOverflow : m_alignContentStartOverflow;
        if (writingMode().isInlineTopToBottom())
            allowance.top() = isColumnar ? m_alignContentStartOverflow : m_justifyContentStartOverflow;
        else
            allowance.bottom() = isColumnar ? m_alignContentStartOverflow : m_justifyContentStartOverflow;
    }

    return allowance;
}

LayoutUnit RenderFlexibleBox::computeGap(RenderFlexibleBox::GapType gapType) const
{
    // row-gap is used for gaps between flex items in column flows or for gaps between lines in row flows.
    bool usesRowGap = (gapType == GapType::BetweenItems) == isColumnFlow();
    auto& gap = usesRowGap ? style().rowGap() : style().columnGap();
    if (gap.isNormal()) [[likely]]
        return { };

    auto availableSize = usesRowGap ? availableLogicalHeightForPercentageComputation().value_or(0_lu) : contentBoxLogicalWidth();
    return Style::evaluateMinimum(gap, availableSize);
}

bool RenderFlexibleBox::layoutUsingFlexFormattingContext()
{
    if (m_hasFlexFormattingContextLayout && !*m_hasFlexFormattingContextLayout) {
        // FIXME: Avoid continous content checking on (potentially) unsupported content. This ensures no pref impact on cases like resize etc.
        // Remove when canUseForFlexLayout becomes less expensive.
        return false;
    }

    m_hasFlexFormattingContextLayout = LayoutIntegration::canUseForFlexLayout(*this);
    if (!*m_hasFlexFormattingContextLayout)
        return false;

    auto flexLayout = LayoutIntegration::FlexLayout { *this };
    flexLayout.updateFormattingContexGeometries();

    flexLayout.layout();
    setLogicalHeight(std::max(logicalHeight(), borderAndPaddingLogicalHeight() + flexLayout.contentBoxLogicalHeight()));
    updateLogicalHeight();
    return true;
}

const RenderBox* RenderFlexibleBox::firstBaselineCandidateOnLine(OrderIterator flexItemIterator, size_t numberOfItemsOnLine) const
{
    // Note that "first" here means in iterator order and not logical flex order (caller can pass in reversed order).
    size_t index = 0;
    const RenderBox* baselineFlexItem = nullptr;
    for (auto* flexItem = flexItemIterator.first(); flexItem; flexItem = flexItemIterator.next()) {
        if (flexItemIterator.shouldSkipChild(*flexItem))
            continue;
        auto flexItemPosition = alignmentForFlexItem(*flexItem);
        if ((flexItemPosition == ItemPosition::Baseline || flexItemPosition == ItemPosition::LastBaseline)
            && mainAxisIsFlexItemInlineAxis(*flexItem) && !hasAutoMarginsInCrossAxis(*flexItem))
            return flexItem;
        if (!baselineFlexItem)
            baselineFlexItem = flexItem;
        if (++index == numberOfItemsOnLine)
            return baselineFlexItem;
    }
    return nullptr;
}

const RenderBox* RenderFlexibleBox::lastBaselineCandidateOnLine(OrderIterator flexItemIterator, size_t numberOfItemsOnLine) const
{
    // Note that "last" here means in iterator order and not logical flex order (caller can pass in reversed order).
    size_t index = 0;
    RenderBox* baselineFlexItem = nullptr;
    for (auto* flexItem = flexItemIterator.first(); flexItem; flexItem = flexItemIterator.next()) {
        if (flexItemIterator.shouldSkipChild(*flexItem))
            continue;
        auto flexItemPosition = alignmentForFlexItem(*flexItem);
        if ((flexItemPosition == ItemPosition::Baseline || flexItemPosition == ItemPosition::LastBaseline)
            && mainAxisIsFlexItemInlineAxis(*flexItem) && !hasAutoMarginsInCrossAxis(*flexItem))
            baselineFlexItem = flexItem;
        if (++index == numberOfItemsOnLine)
            return baselineFlexItem ? baselineFlexItem : flexItem;
    }
    return nullptr;
}

const RenderBox* RenderFlexibleBox::flexItemForFirstBaseline() const
{
    // Looking for baseline flex candidate on visually first line.
    auto useLastLine = style().flexWrap() == FlexWrap::Reverse;
    auto useLastItem = style().flexDirection() == FlexDirection::RowReverse || style().flexDirection() == FlexDirection::ColumnReverse;

    if (!useLastLine) {
        if (!useLastItem) {
            // Logically (and visually) first item on logically (and visually) first line.
            return firstBaselineCandidateOnLine(m_orderIterator, m_numberOfFlexItemsOnFirstLine);
        }
        // Logically last (but visually first) item on logically (and visually) first line.
        return lastBaselineCandidateOnLine(m_orderIterator, m_numberOfFlexItemsOnFirstLine);
    }

    if (!useLastItem) {
        // Logically (and visually) first item on logically last (but visually first) line.
        return lastBaselineCandidateOnLine(m_orderIterator.reverse(), m_numberOfFlexItemsOnLastLine);
    }
    // Logically last (but visually first) item on logically last (but visually first) line.
    return firstBaselineCandidateOnLine(m_orderIterator.reverse(), m_numberOfFlexItemsOnLastLine);
}

const RenderBox* RenderFlexibleBox::flexItemForLastBaseline() const
{
    // Looking for baseline flex candidate on visually last line.
    auto useLastLine = style().flexWrap() == FlexWrap::Reverse;
    auto useLastItem = style().flexDirection() == FlexDirection::RowReverse || style().flexDirection() == FlexDirection::ColumnReverse;

    if (!useLastLine) {
        if (!useLastItem) {
            // Logically (and visually) last item on logically (and visually) last line.
            return firstBaselineCandidateOnLine(m_orderIterator.reverse(), m_numberOfFlexItemsOnLastLine);
        }
        // Logically first (but visually last) item  on logically (and visually) last line.
        return lastBaselineCandidateOnLine(m_orderIterator.reverse(), m_numberOfFlexItemsOnLastLine);
    }

    if (!useLastItem) {
        // Logically (and visually) last item on logically first (but visually last) line.
        return lastBaselineCandidateOnLine(m_orderIterator, m_numberOfFlexItemsOnFirstLine);
    }
    // Logically first (but visually last) item on logically last (but visually first) line.
    return firstBaselineCandidateOnLine(m_orderIterator, m_numberOfFlexItemsOnFirstLine);
}

}
