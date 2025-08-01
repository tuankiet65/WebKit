/*
 * Copyright (C) 1997 Martin Jones (mjones@kde.org)
 *           (C) 1997 Torben Weis (weis@kde.org)
 *           (C) 1998 Waldo Bastian (bastian@kde.org)
 *           (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003-2024 Apple Inc. All rights reserved.
 * Copyright (C) 2016-2017 Google Inc. All rights reserved.
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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

#include "config.h"
#include "RenderTableCell.h"

#include "BackgroundPainter.h"
#include "BorderPainter.h"
#include "CollapsedBorderValue.h"
#include "ElementInlines.h"
#include "FillLayer.h"
#include "FloatQuad.h"
#include "GraphicsContext.h"
#include "HTMLNames.h"
#include "HTMLTableCellElement.h"
#include "PaintInfo.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderObjectInlines.h"
#include "RenderTableCellInlines.h"
#include "RenderTableCol.h"
#include "RenderTableInlines.h"
#include "RenderTheme.h"
#include "RenderView.h"
#include "Settings.h"
#include "StyleBoxShadow.h"
#include "StyleProperties.h"
#include "TransformState.h"
#include <ranges>
#include <wtf/StackStats.h>
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(MATHML)
#include "MathMLElement.h"
#include "MathMLNames.h"
#endif

namespace WebCore {

using namespace HTMLNames;

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderTableCell);

struct SameSizeAsRenderTableCell : public RenderBlockFlow {
    unsigned bitfields;
    LayoutUnit padding[2];
};

static_assert(sizeof(RenderTableCell) == sizeof(SameSizeAsRenderTableCell), "RenderTableCell should stay small");
static_assert(sizeof(CollapsedBorderValue) <= 24, "CollapsedBorderValue should stay small");

RenderTableCell::RenderTableCell(Element& element, RenderStyle&& style)
    : RenderBlockFlow(Type::TableCell, element, WTFMove(style))
    , m_column(unsetColumnIndex)
    , m_cellWidthChanged(false)
    , m_hasColSpan(false)
    , m_hasRowSpan(false)
    , m_hasEmptyCollapsedBeforeBorder(false)
    , m_hasEmptyCollapsedAfterBorder(false)
    , m_hasEmptyCollapsedStartBorder(false)
    , m_hasEmptyCollapsedEndBorder(false)
{
    // We only update the flags when notified of DOM changes in colSpanOrRowSpanChanged()
    // so we need to set their initial values here in case something asks for colSpan()/rowSpan() before then.
    updateColAndRowSpanFlags();
    ASSERT(isRenderTableCell());
}

RenderTableCell::RenderTableCell(Document& document, RenderStyle&& style)
    : RenderBlockFlow(Type::TableCell, document, WTFMove(style))
    , m_column(unsetColumnIndex)
    , m_cellWidthChanged(false)
    , m_hasColSpan(false)
    , m_hasRowSpan(false)
    , m_hasEmptyCollapsedBeforeBorder(false)
    , m_hasEmptyCollapsedAfterBorder(false)
    , m_hasEmptyCollapsedStartBorder(false)
    , m_hasEmptyCollapsedEndBorder(false)
{
    ASSERT(isRenderTableCell());
}

RenderTableCell::~RenderTableCell() = default;

ASCIILiteral RenderTableCell::renderName() const
{
    return (isAnonymous() || isPseudoElement()) ? "RenderTableCell (anonymous)"_s : "RenderTableCell"_s;
}

void RenderTableCell::willBeRemovedFromTree()
{
    RenderBlockFlow::willBeRemovedFromTree();
    if (!table() || !section())
        return;
    RenderTableSection* section = this->section();
    table()->invalidateCollapsedBorders();
    section->removeCachedCollapsedBorders(*this);
    section->setNeedsCellRecalc();
}

unsigned RenderTableCell::parseColSpanFromDOM() const
{
    ASSERT(element());
    if (auto* cell = dynamicDowncast<HTMLTableCellElement>(*element()))
        return std::min<unsigned>(cell->colSpan(), maxColumnIndex);
#if ENABLE(MATHML)
    auto* mathMLElement = dynamicDowncast<MathMLElement>(*element());
    if (mathMLElement && mathMLElement->hasTagName(MathMLNames::mtdTag))
        return std::min<unsigned>(mathMLElement->colSpan(), maxColumnIndex);
#endif
    return 1;
}

unsigned RenderTableCell::parseRowSpanFromDOM() const
{
    ASSERT(element());
    if (auto* cell = dynamicDowncast<HTMLTableCellElement>(*element()))
        return std::min<unsigned>(cell->rowSpan(), maxRowIndex);
#if ENABLE(MATHML)
    auto* mathMLElement = dynamicDowncast<MathMLElement>(*element());
    if (mathMLElement && mathMLElement->hasTagName(MathMLNames::mtdTag))
        return std::min<unsigned>(mathMLElement->rowSpan(), maxRowIndex);
#endif
    return 1;
}

void RenderTableCell::updateColAndRowSpanFlags()
{
    // The vast majority of table cells do not have a colspan or rowspan,
    // so we keep a bool to know if we need to bother reading from the DOM.
    m_hasColSpan = element() && parseColSpanFromDOM() != 1;
    m_hasRowSpan = element() && parseRowSpanFromDOM() != 1;
}

void RenderTableCell::colSpanOrRowSpanChanged()
{
    ASSERT(element());
#if ENABLE(MATHML)
    ASSERT(element()->hasTagName(tdTag) || element()->hasTagName(thTag) || element()->hasTagName(MathMLNames::mtdTag));
#else
    ASSERT(element()->hasTagName(tdTag) || element()->hasTagName(thTag));
#endif

    updateColAndRowSpanFlags();

    // FIXME: I suspect that we could return early here if !m_hasColSpan && !m_hasRowSpan.

    setNeedsLayoutAndPreferredWidthsUpdate();
    if (parent() && section())
        section()->setNeedsCellRecalc();
}

Style::PreferredSize RenderTableCell::logicalWidthFromColumns(RenderTableCol* firstColForThisCell, const Style::PreferredSize& widthFromStyle) const
{
    ASSERT(firstColForThisCell && firstColForThisCell == table()->colElement(col()));
    RenderTableCol* tableCol = firstColForThisCell;

    unsigned colSpanCount = colSpan();
    LayoutUnit colWidthSum;
    for (unsigned i = 1; i <= colSpanCount; i++) {
        auto& colWidth = tableCol->style().logicalWidth();

        auto fixedColWidth = colWidth.tryFixed();

        // Percentage value should be returned only for colSpan == 1.
        // Otherwise we return original width for the cell.
        if (!fixedColWidth) {
            if (colSpanCount > 1)
                return widthFromStyle;
            return colWidth;
        }

        colWidthSum += fixedColWidth->value;
        tableCol = tableCol->nextColumn();
        // If no next <col> tag found for the span we just return what we have for now.
        if (!tableCol)
            break;
    }

    // Column widths specified on <col> apply to the border box of the cell, see bug 8126.
    // FIXME: Why is border/padding ignored in the negative width case?
    if (colWidthSum > 0)
        return Style::PreferredSize::Fixed { std::max<LayoutUnit>(0, colWidthSum - borderAndPaddingLogicalWidth()) };
    return Style::PreferredSize::Fixed { colWidthSum };
}

void RenderTableCell::computePreferredLogicalWidths()
{
    // The child cells rely on the grids up in the sections to do their computePreferredLogicalWidths work.  Normally the sections are set up early, as table
    // cells are added, but relayout can cause the cells to be freed, leaving stale pointers in the sections'
    // grids.  We must refresh those grids before the child cells try to use them.
    table()->recalcSectionsIfNeeded();

    // We don't want the preferred width from children to be affected by any
    // notional height on the cell, such as can happen when a percent sized image
    // scales up its width to match the available height. Setting a zero override
    // height prevents this from happening.
    auto overridingLogicalHeight = this->overridingBorderBoxLogicalHeight();
    if (overridingLogicalHeight)
        setOverridingBorderBoxLogicalHeight({ });
    RenderBlockFlow::computePreferredLogicalWidths();
    if (overridingLogicalHeight)
        setOverridingBorderBoxLogicalHeight(*overridingLogicalHeight);

    if (!element() || !style().autoWrap() || !element()->hasAttributeWithoutSynchronization(nowrapAttr))
        return;

    if (auto fixedLogicalWidth = styleOrColLogicalWidth().tryFixed()) {
        // Nowrap is set, but we didn't actually use it because of the
        // fixed width set on the cell. Even so, it is a WinIE/Moz trait
        // to make the minwidth of the cell into the fixed width. They do this
        // even in strict mode, so do not make this a quirk. Affected the top
        // of hiptop.com.
        m_minPreferredLogicalWidth = std::max(LayoutUnit(fixedLogicalWidth->value), m_minPreferredLogicalWidth);
    }
}

LayoutRect RenderTableCell::frameRectForStickyPositioning() const
{
    // RenderTableCell has the RenderTableRow as the container, but is positioned relatively
    // to the RenderTableSection. The sticky positioning algorithm assumes that elements are
    // positioned relatively to their container, so we correct for that here.
    ASSERT(parentBox());
    auto returnValue = frameRect();
    returnValue.move(-parentBox()->locationOffset());
    return returnValue;
}

bool RenderTableCell::computeIntrinsicPadding(LayoutUnit rowHeight)
{
    LayoutUnit oldIntrinsicPaddingBefore = intrinsicPaddingBefore();
    LayoutUnit oldIntrinsicPaddingAfter = intrinsicPaddingAfter();
    LayoutUnit logicalHeightWithoutIntrinsicPadding = logicalHeight() - oldIntrinsicPaddingBefore - oldIntrinsicPaddingAfter;

    auto intrinsicPaddingBefore = oldIntrinsicPaddingBefore;
    auto alignment = style().verticalAlign();
    if (auto alignContent = style().alignContent(); !alignContent.isNormal()) {
        // align-content overrides vertical-align
        if (alignContent.position() == ContentPosition::Baseline)
            alignment = CSS::Keyword::Baseline { };
        else if (alignContent.isCentered())
            alignment = CSS::Keyword::Middle { };
        else if (alignContent.isStartward())
            alignment = CSS::Keyword::Top { };
        else if (alignContent.isEndward())
            alignment = CSS::Keyword::Bottom { };
    }

    auto applyStandard = [&] {
        auto baseline = cellBaselinePosition();
        auto needsIntrinsicPadding = baseline > borderAndPaddingBefore() || !logicalHeight();
        if (needsIntrinsicPadding)
            intrinsicPaddingBefore = section()->rowBaseline(rowIndex()) - (baseline - oldIntrinsicPaddingBefore);
    };

    WTF::switchOn(alignment,
        [&](const CSS::Keyword::Sub&) {
            applyStandard();
        },
        [&](const CSS::Keyword::Super&) {
            applyStandard();
        },
        [&](const CSS::Keyword::TextTop&) {
            applyStandard();
        },
        [&](const CSS::Keyword::TextBottom&) {
            applyStandard();
        },
        [&](const CSS::Keyword::Baseline&) {
            applyStandard();
        },
        [&](const CSS::Keyword::Top&) {
            // Do nothing.
        },
        [&](const CSS::Keyword::Middle&) {
            intrinsicPaddingBefore = (rowHeight - logicalHeightWithoutIntrinsicPadding) / 2;
        },
        [&](const CSS::Keyword::Bottom&) {
            intrinsicPaddingBefore = rowHeight - logicalHeightWithoutIntrinsicPadding;
        },
        [&](const CSS::Keyword::WebkitBaselineMiddle&) {
            // Do nothing.
        },
        [&](const Style::VerticalAlign::Length&) {
            applyStandard();
        }
    );

    LayoutUnit intrinsicPaddingAfter = rowHeight - logicalHeightWithoutIntrinsicPadding - intrinsicPaddingBefore;
    setIntrinsicPaddingBefore(intrinsicPaddingBefore);
    setIntrinsicPaddingAfter(intrinsicPaddingAfter);

    return intrinsicPaddingBefore != oldIntrinsicPaddingBefore || intrinsicPaddingAfter != oldIntrinsicPaddingAfter;
}

void RenderTableCell::updateLogicalWidth()
{
}

void RenderTableCell::setCellLogicalWidth(LayoutUnit tableLayoutLogicalWidth)
{
    if (tableLayoutLogicalWidth == logicalWidth())
        return;

    setNeedsLayout(MarkOnlyThis);
    row()->setChildNeedsLayout(MarkOnlyThis);

    setLogicalWidth(tableLayoutLogicalWidth);
    setCellWidthChanged(true);
}

void RenderTableCell::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;

    int oldCellBaseline = cellBaselinePosition();
    layoutBlock(cellWidthChanged() ? RelayoutChildren::Yes : RelayoutChildren::No);

    // If we have replaced content, the intrinsic height of our content may have changed since the last time we laid out. If that's the case the intrinsic padding we used
    // for layout (the padding required to push the contents of the cell down to the row's baseline) is included in our new height and baseline and makes both
    // of them wrong. So if our content's intrinsic height has changed push the new content up into the intrinsic padding and relayout so that the rest of
    // table and row layout can use the correct baseline and height for this cell.
    if (isBaselineAligned() && section()->rowBaseline(rowIndex()) && cellBaselinePosition() > section()->rowBaseline(rowIndex())) {
        LayoutUnit newIntrinsicPaddingBefore = std::max<LayoutUnit>(0, intrinsicPaddingBefore() - std::max<LayoutUnit>(0, cellBaselinePosition() - oldCellBaseline));
        setIntrinsicPaddingBefore(newIntrinsicPaddingBefore);
        setNeedsLayout(MarkOnlyThis);
        layoutBlock(cellWidthChanged() ? RelayoutChildren::Yes : RelayoutChildren::No);
    }
    invalidateHasEmptyCollapsedBorders();
    
    // FIXME: This value isn't the intrinsic content logical height, but we need
    // to update the value as its used by flexbox layout. crbug.com/367324
    cacheIntrinsicContentLogicalHeightForFlexItem(contentBoxLogicalHeight());

    setCellWidthChanged(false);
}

RectEdges<LayoutUnit> RenderTableCell::padding() const
{
    auto top = computedCSSPaddingTop();
    auto right = computedCSSPaddingRight();
    auto bottom = computedCSSPaddingBottom();
    auto left = computedCSSPaddingLeft();

    if (isHorizontalWritingMode()) {
        bool isTopToBottom = writingMode().isBlockTopToBottom();
        top += isTopToBottom ? intrinsicPaddingBefore() : intrinsicPaddingAfter();
        bottom += isTopToBottom ? intrinsicPaddingAfter() : intrinsicPaddingBefore();
    } else {
        bool isLeftToRight = writingMode().isBlockLeftToRight();
        left += isLeftToRight ? intrinsicPaddingBefore() : intrinsicPaddingAfter();
        right += isLeftToRight ? intrinsicPaddingAfter() : intrinsicPaddingBefore();
    }

    return {
        top,
        right,
        bottom,
        left
    };
}

LayoutUnit RenderTableCell::paddingTop() const
{
    LayoutUnit result = computedCSSPaddingTop();
    if (!isHorizontalWritingMode())
        return result;
    return result + (writingMode().isBlockTopToBottom() ? intrinsicPaddingBefore() : intrinsicPaddingAfter());
}

LayoutUnit RenderTableCell::paddingBottom() const
{
    LayoutUnit result = computedCSSPaddingBottom();
    if (!isHorizontalWritingMode())
        return result;
    return result + (writingMode().isBlockTopToBottom()? intrinsicPaddingAfter() : intrinsicPaddingBefore());
}

LayoutUnit RenderTableCell::paddingLeft() const
{
    LayoutUnit result = computedCSSPaddingLeft();
    if (isHorizontalWritingMode())
        return result;
    return result + (writingMode().isBlockLeftToRight() ? intrinsicPaddingBefore() : intrinsicPaddingAfter());
}

LayoutUnit RenderTableCell::paddingRight() const
{   
    LayoutUnit result = computedCSSPaddingRight();
    if (isHorizontalWritingMode())
        return result;
    return result + (writingMode().isBlockLeftToRight() ? intrinsicPaddingAfter() : intrinsicPaddingBefore());
}

LayoutUnit RenderTableCell::paddingBefore() const
{
    return computedCSSPaddingBefore() + intrinsicPaddingBefore();
}

LayoutUnit RenderTableCell::paddingAfter() const
{
    return computedCSSPaddingAfter() + intrinsicPaddingAfter();
}

void RenderTableCell::setOverridingLogicalHeightFromRowHeight(LayoutUnit rowHeight)
{
    clearIntrinsicPadding();
    setOverridingBorderBoxLogicalHeight(rowHeight);
}

LayoutSize RenderTableCell::offsetFromContainer(const RenderElement& container, const LayoutPoint& point, bool* offsetDependsOnPoint) const
{
    ASSERT(&container == this->container());

    LayoutSize offset = RenderBlockFlow::offsetFromContainer(container, point, offsetDependsOnPoint);
    if (auto* containerOfRow = container.container(); containerOfRow && parent())
        offset -= parentBox()->offsetFromContainer(*containerOfRow, point);

    return offset;
}

auto RenderTableCell::localRectsForRepaint(RepaintOutlineBounds repaintOutlineBounds) const -> RepaintRects
{
    // If the table grid is dirty, we cannot get reliable information about adjoining cells,
    // so we ignore outside borders. This should not be a problem because it means that
    // the table is going to recalculate the grid, relayout and repaint its current rect, which
    // includes any outside borders of this cell.
    if (!table()->collapseBorders() || table()->needsSectionRecalc())
        return RenderBlockFlow::localRectsForRepaint(repaintOutlineBounds);

    bool flippedInline = tableWritingMode().isInlineFlipped();
    LayoutUnit outlineSize { style().outlineSize() };
    LayoutUnit left = std::max(borderHalfLeft(true), outlineSize);
    LayoutUnit right = std::max(borderHalfRight(true), outlineSize);
    LayoutUnit top = std::max(borderHalfTop(true), outlineSize);
    LayoutUnit bottom = std::max(borderHalfBottom(true), outlineSize);
    if ((left && !flippedInline) || (right && flippedInline)) {
        if (RenderTableCell* before = table()->cellBefore(this)) {
            top = std::max(top, before->borderHalfTop(true));
            bottom = std::max(bottom, before->borderHalfBottom(true));
        }
    }
    if ((left && flippedInline) || (right && !flippedInline)) {
        if (RenderTableCell* after = table()->cellAfter(this)) {
            top = std::max(top, after->borderHalfTop(true));
            bottom = std::max(bottom, after->borderHalfBottom(true));
        }
    }
    if (top) {
        if (RenderTableCell* above = table()->cellAbove(this)) {
            left = std::max(left, above->borderHalfLeft(true));
            right = std::max(right, above->borderHalfRight(true));
        }
    }
    if (bottom) {
        if (RenderTableCell* below = table()->cellBelow(this)) {
            left = std::max(left, below->borderHalfLeft(true));
            right = std::max(right, below->borderHalfRight(true));
        }
    }

    LayoutPoint location(std::max<LayoutUnit>(left, -visualOverflowRect().x()), std::max<LayoutUnit>(top, -visualOverflowRect().y()));
    auto overflowRect = LayoutRect(-location.x(), -location.y(), location.x() + std::max(width() + right, visualOverflowRect().maxX()), location.y() + std::max(height() + bottom, visualOverflowRect().maxY()));

    // FIXME: layoutDelta needs to be applied in parts before/after transforms and
    // repaint containers. https://bugs.webkit.org/show_bug.cgi?id=23308
    overflowRect.move(view().frameView().layoutContext().layoutDelta());

    auto rects = RepaintRects { overflowRect };
    if (repaintOutlineBounds == RepaintOutlineBounds::Yes)
        rects.outlineBoundsRect = localOutlineBoundsRepaintRect();

    return rects;
}

auto RenderTableCell::computeVisibleRectsInContainer(const RepaintRects& rects, const RenderLayerModelObject* container, VisibleRectContext context) const -> std::optional<RepaintRects>
{
    if (container == this)
        return rects;

    auto adjustedRects = rects;
    if ((!view().frameView().layoutContext().isPaintOffsetCacheEnabled() || container || context.options.contains(VisibleRectContextOption::UseEdgeInclusiveIntersection)) && parent())
        adjustedRects.moveBy(-parentBox()->location()); // Rows are in the same coordinate space, so don't add their offset in.

    return RenderBlockFlow::computeVisibleRectsInContainer(adjustedRects, container, context);
}

LayoutUnit RenderTableCell::cellBaselinePosition() const
{
    // <http://www.w3.org/TR/2007/CR-CSS21-20070719/tables.html#height-layout>: The baseline of a cell is the baseline of
    // the first in-flow line box in the cell, or the first in-flow table-row in the cell, whichever comes first. If there
    // is no such line box or table-row, the baseline is the bottom of content edge of the cell box.
    return firstLineBaseline().value_or(borderAndPaddingBefore() + contentBoxLogicalHeight());
}

static inline void markCellDirtyWhenCollapsedBorderChanges(RenderTableCell* cell)
{
    if (!cell)
        return;
    cell->setNeedsLayoutAndPreferredWidthsUpdate();
}

void RenderTableCell::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    ASSERT(style().display() == DisplayType::TableCell);
    ASSERT(!row() || row()->rowIndexWasSet());

    RenderBlockFlow::styleDidChange(diff, oldStyle);
    setHasVisibleBoxDecorations(true); // FIXME: Optimize this to only set to true if necessary.

    if (parent() && section() && oldStyle && style().height() != oldStyle->height())
        section()->rowLogicalHeightChanged(rowIndex());

    // Our intrinsic padding pushes us down to align with the baseline of other cells on the row. If our vertical-align
    // has changed then so will the padding needed to align with other cells - clear it so we can recalculate it from scratch.
    if (oldStyle && (style().verticalAlign() != oldStyle->verticalAlign() || style().alignContent() != oldStyle->alignContent()))
        clearIntrinsicPadding();

    if (CheckedPtr table = this->table(); table && oldStyle) {
        table->invalidateCollapsedBordersAfterStyleChangeIfNeeded(*oldStyle, style(), this);
        if (table->collapseBorders() && diff == StyleDifference::Layout) {
            markCellDirtyWhenCollapsedBorderChanges(table->cellBelow(this));
            markCellDirtyWhenCollapsedBorderChanges(table->cellAbove(this));
            markCellDirtyWhenCollapsedBorderChanges(table->cellBefore(this));
            markCellDirtyWhenCollapsedBorderChanges(table->cellAfter(this));
        }
    }
}

// The following rules apply for resolving conflicts and figuring out which border
// to use.
// (1) Borders with the 'border-style' of 'hidden' take precedence over all other conflicting 
// borders. Any border with this value suppresses all borders at this location.
// (2) Borders with a style of 'none' have the lowest priority. Only if the border properties of all 
// the elements meeting at this edge are 'none' will the border be omitted (but note that 'none' is 
// the default value for the border style.)
// (3) If none of the styles are 'hidden' and at least one of them is not 'none', then narrow borders 
// are discarded in favor of wider ones. If several have the same 'border-width' then styles are preferred 
// in this order: 'double', 'solid', 'dashed', 'dotted', 'ridge', 'outset', 'groove', and the lowest: 'inset'.
// (4) If border styles differ only in color, then a style set on a cell wins over one on a row, 
// which wins over a row group, column, column group and, lastly, table. It is undefined which color 
// is used when two elements of the same type disagree.
static bool compareBorders(const CollapsedBorderValue& border1, const CollapsedBorderValue& border2)
{
    // Sanity check the values passed in. The null border have lowest priority.
    if (!border2.exists())
        return false;
    if (!border1.exists())
        return true;

    // Rule #1 above.
    if (border1.style() == BorderStyle::Hidden)
        return false;
    if (border2.style() == BorderStyle::Hidden)
        return true;
    
    // Rule #2 above.  A style of 'none' has lowest priority and always loses to any other border.
    if (border2.style() == BorderStyle::None)
        return false;
    if (border1.style() == BorderStyle::None)
        return true;

    // The first part of rule #3 above. Wider borders win.
    if (border1.width() != border2.width())
        return border1.width() < border2.width();
    
    // The borders have equal width.  Sort by border style.
    if (border1.style() != border2.style())
        return border1.style() < border2.style();
    
    // The border have the same width and style.  Rely on precedence (cell over row over row group, etc.)
    return border1.precedence() < border2.precedence();
}

static CollapsedBorderValue chooseBorder(const CollapsedBorderValue& border1, const CollapsedBorderValue& border2)
{
    const CollapsedBorderValue& border = compareBorders(border1, border2) ? border2 : border1;
    return border.style() == BorderStyle::Hidden ? CollapsedBorderValue() : border;
}

bool RenderTableCell::hasStartBorderAdjoiningTable() const
{
    return !col();
}

bool RenderTableCell::hasEndBorderAdjoiningTable() const
{
    return table()->colToEffCol(col() + colSpan() - 1) == table()->numEffCols() - 1;
}

static CollapsedBorderValue emptyBorder()
{
    return CollapsedBorderValue(BorderValue(), Color(), BorderPrecedence::Cell);
}

CollapsedBorderValue RenderTableCell::collapsedStartBorder(IncludeBorderColorOrNot includeColor) const
{
    if (!table() || !section())
        return emptyBorder();

    if (m_hasEmptyCollapsedStartBorder)
        return emptyBorder();

    if (table()->collapsedBordersAreValid())
        return section()->cachedCollapsedBorder(*this, CBSStart);

    CollapsedBorderValue result = computeCollapsedStartBorder(includeColor);
    setHasEmptyCollapsedBorder(CBSStart, !result.width());
    if (includeColor && !m_hasEmptyCollapsedStartBorder)
        section()->setCachedCollapsedBorder(*this, CBSStart, result);
    return result;
}

CollapsedBorderValue RenderTableCell::computeCollapsedStartBorder(IncludeBorderColorOrNot includeColor) const
{
    // For the start border, we need to check, in order of precedence:
    // (1) Our start border.
    CSSPropertyID startColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderInlineStartColor, tableWritingMode()) : CSSPropertyInvalid;
    CSSPropertyID endColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderInlineEndColor, tableWritingMode()) : CSSPropertyInvalid;
    CollapsedBorderValue result(style().borderStart(tableWritingMode()), includeColor ? style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Cell);

    RenderTable* table = this->table();
    if (!table)
        return result;
    // (2) The end border of the preceding cell.
    RenderTableCell* cellBefore = table->cellBefore(this);
    if (cellBefore) {
        CollapsedBorderValue cellBeforeAdjoiningBorder = CollapsedBorderValue(cellBefore->borderAdjoiningCellAfter(*this), includeColor ? cellBefore->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Cell);
        // |result| should be the 2nd argument as |cellBefore| should win in case of equality per CSS 2.1 (Border conflict resolution, point 4).
        result = chooseBorder(cellBeforeAdjoiningBorder, result);
        if (!result.exists())
            return result;
    }

    bool startBorderAdjoinsTable = hasStartBorderAdjoiningTable();
    if (startBorderAdjoinsTable) {
        // (3) Our row's start border.
        result = chooseBorder(result, CollapsedBorderValue(row()->borderAdjoiningStartCell(*this), includeColor ? parent()->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Row));
        if (!result.exists())
            return result;

        // (4) Our row group's start border.
        result = chooseBorder(result, CollapsedBorderValue(section()->borderAdjoiningStartCell(*this), includeColor ? section()->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::RowGroup));
        if (!result.exists())
            return result;
    }
    
    // (5) Our column and column group's start borders.
    bool startColEdge;
    bool endColEdge;
    if (RenderTableCol* colElt = table->colElement(col(), &startColEdge, &endColEdge)) {
        if (colElt->isTableColumnGroup() && startColEdge) {
            // The |colElt| is a column group and is also the first colgroup (in case of spanned colgroups).
            result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellStartBorder(), includeColor ? colElt->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::ColumnGroup));
            if (!result.exists())
                return result;
        } else if (!colElt->isTableColumnGroup()) {
            // We first consider the |colElt| and irrespective of whether it is a spanned col or not, we apply
            // its start border. This is as per HTML5 which states that: "For the purposes of the CSS table model,
            // the col element is expected to be treated as if it was present as many times as its span attribute specifies".
            result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellStartBorder(), includeColor ? colElt->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Column));
            if (!result.exists())
                return result;
            // Next, apply the start border of the enclosing colgroup but only if it is adjacent to the cell's edge.
            if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroupIfAdjacentBefore()) {
                result = chooseBorder(result, CollapsedBorderValue(enclosingColumnGroup->borderAdjoiningCellStartBorder(), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                if (!result.exists())
                    return result;
            }
        }
    }
    
    // (6) The end border of the preceding column.
    if (cellBefore) {
        if (RenderTableCol* colElt = table->colElement(col() - 1, &startColEdge, &endColEdge)) {
            if (colElt->isTableColumnGroup() && endColEdge) {
                // The element is a colgroup and is also the last colgroup (in case of spanned colgroups).
                result = chooseBorder(CollapsedBorderValue(colElt->borderAdjoiningCellAfter(*this), includeColor ? colElt->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::ColumnGroup), result);
                if (!result.exists())
                    return result;
            } else if (colElt->isTableColumn()) {
                // Resolve the collapsing border against the col's border ignoring any 'span' as per HTML5.
                result = chooseBorder(CollapsedBorderValue(colElt->borderAdjoiningCellAfter(*this), includeColor ? colElt->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Column), result);
                if (!result.exists())
                    return result;
                // Next, if the previous col has a parent colgroup then its end border should be applied
                // but only if it is adjacent to the cell's edge.
                if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroupIfAdjacentAfter()) {
                    result = chooseBorder(CollapsedBorderValue(enclosingColumnGroup->borderAdjoiningCellEndBorder(), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::ColumnGroup), result);
                    if (!result.exists())
                        return result;
                }
            }
        }
    }

    if (startBorderAdjoinsTable) {
        // (7) The table's start border.
        result = chooseBorder(result, CollapsedBorderValue(table->style().borderStart(), includeColor ? table->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Table));
        if (!result.exists())
            return result;
    }
    
    return result;
}

CollapsedBorderValue RenderTableCell::collapsedEndBorder(IncludeBorderColorOrNot includeColor) const
{
    if (!table() || !section())
        return emptyBorder();

    if (m_hasEmptyCollapsedEndBorder)
        return emptyBorder();

    if (table()->collapsedBordersAreValid())
        return section()->cachedCollapsedBorder(*this, CBSEnd);

    CollapsedBorderValue result = computeCollapsedEndBorder(includeColor);
    setHasEmptyCollapsedBorder(CBSEnd, !result.width());
    if (includeColor && !m_hasEmptyCollapsedEndBorder)
        section()->setCachedCollapsedBorder(*this, CBSEnd, result);
    return result;
}

CollapsedBorderValue RenderTableCell::computeCollapsedEndBorder(IncludeBorderColorOrNot includeColor) const
{
    // For end border, we need to check, in order of precedence:
    // (1) Our end border.
    CSSPropertyID startColorProperty = includeColor
        ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderInlineStartColor, tableWritingMode()) : CSSPropertyInvalid;
    CSSPropertyID endColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderInlineEndColor, tableWritingMode()) : CSSPropertyInvalid;
    CollapsedBorderValue result = CollapsedBorderValue(style().borderEnd(tableWritingMode()), includeColor ? style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Cell);

    RenderTable* table = this->table();
    if (!table)
        return result;
    // Note: We have to use the effective column information instead of whether we have a cell after as a table doesn't
    // have to be regular (any row can have less cells than the total cell count).
    bool isEndColumn = table->colToEffCol(col() + colSpan() - 1) == table->numEffCols() - 1;
    // (2) The start border of the following cell.
    if (!isEndColumn) {
        if (RenderTableCell* cellAfter = table->cellAfter(this)) {
            CollapsedBorderValue cellAfterAdjoiningBorder = CollapsedBorderValue(cellAfter->borderAdjoiningCellBefore(*this), includeColor ? cellAfter->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Cell);
            result = chooseBorder(result, cellAfterAdjoiningBorder);
            if (!result.exists())
                return result;
        }
    }

    bool endBorderAdjoinsTable = hasEndBorderAdjoiningTable();
    if (endBorderAdjoinsTable) {
        // (3) Our row's end border.
        result = chooseBorder(result, CollapsedBorderValue(row()->borderAdjoiningEndCell(*this), includeColor ? parent()->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Row));
        if (!result.exists())
            return result;
        
        // (4) Our row group's end border.
        result = chooseBorder(result, CollapsedBorderValue(section()->borderAdjoiningEndCell(*this), includeColor ? section()->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::RowGroup));
        if (!result.exists())
            return result;
    }
    
    // (5) Our column and column group's end borders.
    bool startColEdge;
    bool endColEdge;
    if (RenderTableCol* colElt = table->colElement(col() + colSpan() - 1, &startColEdge, &endColEdge)) {
        if (colElt->isTableColumnGroup() && endColEdge) {
            // The element is a colgroup and is also the last colgroup (in case of spanned colgroups).
            result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellEndBorder(), includeColor ? colElt->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::ColumnGroup));
            if (!result.exists())
                return result;
        } else if (!colElt->isTableColumnGroup()) {
            // First apply the end border of the column irrespective of whether it is spanned or not. This is as per
            // HTML5 which states that: "For the purposes of the CSS table model, the col element is expected to be
            // treated as if it was present as many times as its span attribute specifies".
            result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellEndBorder(), includeColor ? colElt->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Column));
            if (!result.exists())
                return result;
            // Next, if it has a parent colgroup then we apply its end border but only if it is adjacent to the cell.
            if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroupIfAdjacentAfter()) {
                result = chooseBorder(result, CollapsedBorderValue(enclosingColumnGroup->borderAdjoiningCellEndBorder(), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                if (!result.exists())
                    return result;
            }
        }
    }
    
    // (6) The start border of the next column.
    if (!isEndColumn) {
        if (RenderTableCol* colElt = table->colElement(col() + colSpan(), &startColEdge, &endColEdge)) {
            if (colElt->isTableColumnGroup() && startColEdge) {
                // This case is a colgroup without any col, we only compute it if it is adjacent to the cell's edge.
                result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellBefore(*this), includeColor ? colElt->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                if (!result.exists())
                    return result;
            } else if (colElt->isTableColumn()) {
                // Resolve the collapsing border against the col's border ignoring any 'span' as per HTML5.
                result = chooseBorder(result, CollapsedBorderValue(colElt->borderAdjoiningCellBefore(*this), includeColor ? colElt->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::Column));
                if (!result.exists())
                    return result;
                // If we have a parent colgroup, resolve the border only if it is adjacent to the cell.
                if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroupIfAdjacentBefore()) {
                    result = chooseBorder(result, CollapsedBorderValue(enclosingColumnGroup->borderAdjoiningCellStartBorder(), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(startColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                    if (!result.exists())
                        return result;
                }
            }
        }
    }

    if (endBorderAdjoinsTable) {
        // (7) The table's end border.
        result = chooseBorder(result, CollapsedBorderValue(table->style().borderEnd(), includeColor ? table->style().visitedDependentColorWithColorFilter(endColorProperty) : Color(), BorderPrecedence::Table));
        if (!result.exists())
            return result;
    }
    
    return result;
}

CollapsedBorderValue RenderTableCell::collapsedBeforeBorder(IncludeBorderColorOrNot includeColor) const
{
    if (!table() || !section())
        return emptyBorder();

    if (m_hasEmptyCollapsedBeforeBorder)
        return emptyBorder();

    if (table()->collapsedBordersAreValid())
        return section()->cachedCollapsedBorder(*this, CBSBefore);

    CollapsedBorderValue result = computeCollapsedBeforeBorder(includeColor);
    setHasEmptyCollapsedBorder(CBSBefore, !result.width());
    if (includeColor && !m_hasEmptyCollapsedBeforeBorder)
        section()->setCachedCollapsedBorder(*this, CBSBefore, result);
    return result;
}

CollapsedBorderValue RenderTableCell::computeCollapsedBeforeBorder(IncludeBorderColorOrNot includeColor) const
{
    // For before border, we need to check, in order of precedence:
    // (1) Our before border.
    CSSPropertyID beforeColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderBlockStartColor, tableWritingMode()) : CSSPropertyInvalid;
    CSSPropertyID afterColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderBlockEndColor, tableWritingMode()) : CSSPropertyInvalid;
    CollapsedBorderValue result = CollapsedBorderValue(style().borderBefore(tableWritingMode()), includeColor ? style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Cell);
    
    RenderTable* table = this->table();
    if (!table)
        return result;
    RenderTableCell* previousCell = table->cellAbove(this);
    if (previousCell) {
        // (2) A before cell's after border.
        result = chooseBorder(CollapsedBorderValue(previousCell->style().borderAfter(), includeColor ? previousCell->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Cell), result);
        if (!result.exists())
            return result;
    }
    
    // (3) Our row's before border.
    result = chooseBorder(result, CollapsedBorderValue(parent()->style().borderBefore(tableWritingMode()), includeColor ? parent()->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Row));
    if (!result.exists())
        return result;
    
    // (4) The previous row's after border.
    if (previousCell) {
        RenderTableRow* previousRow = nullptr;
        if (previousCell->section() == section())
            previousRow = dynamicDowncast<RenderTableRow>(parent()->previousSibling());
        else
            previousRow = previousCell->section()->lastRow();
    
        if (previousRow) {
            result = chooseBorder(CollapsedBorderValue(previousRow->style().borderAfter(), includeColor ? previousRow->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Row), result);
            if (!result.exists())
                return result;
        }
    }
    
    // Now check row groups.
    RenderTableSection* currSection = section();
    if (!rowIndex()) {
        // (5) Our row group's before border.
        result = chooseBorder(result, CollapsedBorderValue(currSection->style().borderBefore(tableWritingMode()), includeColor ? currSection->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::RowGroup));
        if (!result.exists())
            return result;
        
        // (6) Previous row group's after border.
        currSection = table->sectionAbove(currSection, SkipEmptySections);
        if (currSection) {
            result = chooseBorder(CollapsedBorderValue(currSection->style().borderAfter(), includeColor ? currSection->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::RowGroup), result);
            if (!result.exists())
                return result;
        }
    }
    
    if (!currSection) {
        // (8) Our column and column group's before borders.
        RenderTableCol* colElt = table->colElement(col());
        if (colElt) {
            result = chooseBorder(result, CollapsedBorderValue(colElt->style().borderBefore(tableWritingMode()), includeColor ? colElt->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Column));
            if (!result.exists())
                return result;
            if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroup()) {
                result = chooseBorder(result, CollapsedBorderValue(enclosingColumnGroup->style().borderBefore(tableWritingMode()), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                if (!result.exists())
                    return result;
            }
        }
        
        // (9) The table's before border.
        result = chooseBorder(result, CollapsedBorderValue(table->style().borderBefore(), includeColor ? table->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Table));
        if (!result.exists())
            return result;
    }
    
    return result;
}

CollapsedBorderValue RenderTableCell::collapsedAfterBorder(IncludeBorderColorOrNot includeColor) const
{
    if (!table() || !section())
        return emptyBorder();

    if (m_hasEmptyCollapsedAfterBorder)
        return emptyBorder();

    if (table()->collapsedBordersAreValid())
        return section()->cachedCollapsedBorder(*this, CBSAfter);

    CollapsedBorderValue result = computeCollapsedAfterBorder(includeColor);
    setHasEmptyCollapsedBorder(CBSAfter, !result.width());
    if (includeColor && !m_hasEmptyCollapsedAfterBorder)
        section()->setCachedCollapsedBorder(*this, CBSAfter, result);
    return result;
}

CollapsedBorderValue RenderTableCell::computeCollapsedAfterBorder(IncludeBorderColorOrNot includeColor) const
{
    // For after border, we need to check, in order of precedence:
    // (1) Our after border.
    CSSPropertyID beforeColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderBlockStartColor, tableWritingMode()) : CSSPropertyInvalid;
    CSSPropertyID afterColorProperty = includeColor ? CSSProperty::resolveDirectionAwareProperty(CSSPropertyBorderBlockEndColor, tableWritingMode()) : CSSPropertyInvalid;
    CollapsedBorderValue result = CollapsedBorderValue(style().borderAfter(tableWritingMode()), includeColor ? style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Cell);
    
    RenderTable* table = this->table();
    if (!table)
        return result;
    RenderTableCell* nextCell = table->cellBelow(this);
    if (nextCell) {
        // (2) An after cell's before border.
        result = chooseBorder(result, CollapsedBorderValue(nextCell->style().borderBefore(tableWritingMode()), includeColor ? nextCell->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Cell));
        if (!result.exists())
            return result;
    }
    
    // (3) Our row's after border. (FIXME: Deal with rowspan!)
    result = chooseBorder(result, CollapsedBorderValue(parent()->style().borderAfter(), includeColor ? parent()->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Row));
    if (!result.exists())
        return result;
    
    // (4) The next row's before border.
    if (nextCell) {
        result = chooseBorder(result, CollapsedBorderValue(nextCell->parent()->style().borderBefore(tableWritingMode()), includeColor ? nextCell->parent()->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::Row));
        if (!result.exists())
            return result;
    }
    
    // Now check row groups.
    RenderTableSection* currSection = section();
    if (rowIndex() + rowSpan() >= currSection->numRows()) {
        // (5) Our row group's after border.
        result = chooseBorder(result, CollapsedBorderValue(currSection->style().borderAfter(tableWritingMode()), includeColor ? currSection->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::RowGroup));
        if (!result.exists())
            return result;
        
        // (6) Following row group's before border.
        currSection = table->sectionBelow(currSection, SkipEmptySections);
        if (currSection) {
            result = chooseBorder(result, CollapsedBorderValue(currSection->style().borderBefore(tableWritingMode()), includeColor ? currSection->style().visitedDependentColorWithColorFilter(beforeColorProperty) : Color(), BorderPrecedence::RowGroup));
            if (!result.exists())
                return result;
        }
    }
    
    if (!currSection) {
        // (8) Our column and column group's after borders.
        RenderTableCol* colElt = table->colElement(col());
        if (colElt) {
            result = chooseBorder(result, CollapsedBorderValue(colElt->style().borderAfter(tableWritingMode()), includeColor ? colElt->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Column));
            if (!result.exists()) return result;
            if (RenderTableCol* enclosingColumnGroup = colElt->enclosingColumnGroup()) {
                result = chooseBorder(result, CollapsedBorderValue(enclosingColumnGroup->style().borderAfter(tableWritingMode()), includeColor ? enclosingColumnGroup->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::ColumnGroup));
                if (!result.exists())
                    return result;
            }
        }
        
        // (9) The table's after border.
        result = chooseBorder(result, CollapsedBorderValue(table->style().borderAfter(tableWritingMode()), includeColor ? table->style().visitedDependentColorWithColorFilter(afterColorProperty) : Color(), BorderPrecedence::Table));
        if (!result.exists())
            return result;
    }
    
    return result;    
}

inline CollapsedBorderValue RenderTableCell::cachedCollapsedLeftBorder(const WritingMode writingMode) const
{
    if (writingMode.isHorizontal())
        return writingMode.isInlineLeftToRight() ? section()->cachedCollapsedBorder(*this, CBSStart) : section()->cachedCollapsedBorder(*this, CBSEnd);
    return writingMode.isBlockLeftToRight() ? section()->cachedCollapsedBorder(*this, CBSBefore) : section()->cachedCollapsedBorder(*this, CBSAfter);
}

inline CollapsedBorderValue RenderTableCell::cachedCollapsedRightBorder(const WritingMode writingMode) const
{
    if (writingMode.isHorizontal())
        return writingMode.isInlineLeftToRight() ? section()->cachedCollapsedBorder(*this, CBSEnd) : section()->cachedCollapsedBorder(*this, CBSStart);
    return writingMode.isBlockLeftToRight() ? section()->cachedCollapsedBorder(*this, CBSAfter) : section()->cachedCollapsedBorder(*this, CBSBefore);
}

inline CollapsedBorderValue RenderTableCell::cachedCollapsedTopBorder(const WritingMode writingMode) const
{
    if (writingMode.isHorizontal())
        return writingMode.isBlockTopToBottom() ? section()->cachedCollapsedBorder(*this, CBSBefore) : section()->cachedCollapsedBorder(*this, CBSAfter);
    return writingMode.isInlineTopToBottom() ? section()->cachedCollapsedBorder(*this, CBSStart) : section()->cachedCollapsedBorder(*this, CBSEnd);
}

inline CollapsedBorderValue RenderTableCell::cachedCollapsedBottomBorder(const WritingMode writingMode) const
{
    if (writingMode.isHorizontal())
        return writingMode.isBlockTopToBottom() ? section()->cachedCollapsedBorder(*this, CBSAfter) : section()->cachedCollapsedBorder(*this, CBSBefore);
    return writingMode.isInlineTopToBottom() ? section()->cachedCollapsedBorder(*this, CBSEnd) : section()->cachedCollapsedBorder(*this, CBSStart);
}

RectEdges<LayoutUnit> RenderTableCell::borderWidths() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderWidths();

    if (!table->collapseBorders())
        return RenderBlockFlow::borderWidths();

    return {
        borderHalfTop(false),
        borderHalfRight(false),
        borderHalfBottom(false),
        borderHalfLeft(false)
    };
}

LayoutUnit RenderTableCell::borderLeft() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderLeft();
    return table->collapseBorders() ? borderHalfLeft(false) : RenderBlockFlow::borderLeft();
}

LayoutUnit RenderTableCell::borderRight() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderRight();
    return table->collapseBorders() ? borderHalfRight(false) : RenderBlockFlow::borderRight();
}

LayoutUnit RenderTableCell::borderTop() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderTop();
    return table->collapseBorders() ? borderHalfTop(false) : RenderBlockFlow::borderTop();
}

LayoutUnit RenderTableCell::borderBottom() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderBottom();
    return table->collapseBorders() ? borderHalfBottom(false) : RenderBlockFlow::borderBottom();
}

// FIXME: https://bugs.webkit.org/show_bug.cgi?id=46191, make the collapsed border drawing
// work with different block flow values instead of being hard-coded to top-to-bottom.
LayoutUnit RenderTableCell::borderStart() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderStart();
    return table->collapseBorders() ? borderHalfStart(false) : RenderBlockFlow::borderStart();
}

LayoutUnit RenderTableCell::borderEnd() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderEnd();
    return table->collapseBorders() ? borderHalfEnd(false) : RenderBlockFlow::borderEnd();
}

LayoutUnit RenderTableCell::borderBefore() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderBefore();
    return table->collapseBorders() ? borderHalfBefore(false) : RenderBlockFlow::borderBefore();
}

LayoutUnit RenderTableCell::borderAfter() const
{
    RenderTable* table = this->table();
    if (!table)
        return RenderBlockFlow::borderAfter();
    return table->collapseBorders() ? borderHalfAfter(false) : RenderBlockFlow::borderAfter();
}

LayoutUnit RenderTableCell::borderHalfLeft(bool outer) const
{
    WritingMode writingMode = this->tableWritingMode();
    if (writingMode.isHorizontal())
        return writingMode.isInlineLeftToRight() ? borderHalfStart(outer) : borderHalfEnd(outer);
    return writingMode.isBlockLeftToRight() ? borderHalfBefore(outer) : borderHalfAfter(outer);
}

LayoutUnit RenderTableCell::borderHalfRight(bool outer) const
{
    WritingMode writingMode = this->tableWritingMode();
    if (writingMode.isHorizontal())
        return writingMode.isInlineLeftToRight() ? borderHalfEnd(outer) : borderHalfStart(outer);
    return writingMode.isBlockLeftToRight() ? borderHalfAfter(outer) : borderHalfBefore(outer);
}

LayoutUnit RenderTableCell::borderHalfTop(bool outer) const
{
    WritingMode writingMode = this->tableWritingMode();
    if (writingMode.isHorizontal())
        return writingMode.isBlockTopToBottom() ? borderHalfBefore(outer) : borderHalfAfter(outer);
    return writingMode.isInlineTopToBottom() ? borderHalfStart(outer) : borderHalfEnd(outer);
}

LayoutUnit RenderTableCell::borderHalfBottom(bool outer) const
{
    WritingMode writingMode = this->tableWritingMode();
    if (writingMode.isHorizontal())
        return writingMode.isBlockTopToBottom() ? borderHalfAfter(outer) : borderHalfBefore(outer);
    return writingMode.isInlineTopToBottom() ? borderHalfEnd(outer) : borderHalfStart(outer);
}

LayoutUnit RenderTableCell::borderHalfStart(bool outer) const
{
    CollapsedBorderValue border = collapsedStartBorder(DoNotIncludeBorderColor);
    if (border.exists())
        return CollapsedBorderValue::adjustedCollapsedBorderWidth(border.width(), document().deviceScaleFactor(), !(tableWritingMode().isInlineFlipped() ^ outer));
    return 0;
}
    
LayoutUnit RenderTableCell::borderHalfEnd(bool outer) const
{
    CollapsedBorderValue border = collapsedEndBorder(DoNotIncludeBorderColor);
    if (border.exists())
        return CollapsedBorderValue::adjustedCollapsedBorderWidth(border.width(), document().deviceScaleFactor(), tableWritingMode().isInlineFlipped() ^ outer);
    return 0;
}

LayoutUnit RenderTableCell::borderHalfBefore(bool outer) const
{
    CollapsedBorderValue border = collapsedBeforeBorder(DoNotIncludeBorderColor);
    if (border.exists())
        return CollapsedBorderValue::adjustedCollapsedBorderWidth(border.width(), document().deviceScaleFactor(), !(tableWritingMode().isBlockFlipped() ^ outer));
    return 0;
}

LayoutUnit RenderTableCell::borderHalfAfter(bool outer) const
{
    CollapsedBorderValue border = collapsedAfterBorder(DoNotIncludeBorderColor);
    if (border.exists())
        return CollapsedBorderValue::adjustedCollapsedBorderWidth(border.width(), document().deviceScaleFactor(), tableWritingMode().isBlockFlipped() ^ outer);
    return 0;
}

void RenderTableCell::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    ASSERT(paintInfo.phase != PaintPhase::CollapsedTableBorders);
    RenderBlockFlow::paint(paintInfo, paintOffset);
}

struct CollapsedBorder {
    CollapsedBorderValue borderValue;
    BoxSide side;
    bool shouldPaint;
    LayoutUnit x1;
    LayoutUnit y1;
    LayoutUnit x2;
    LayoutUnit y2;
    BorderStyle style;
};

class CollapsedBorders {
public:
    CollapsedBorders()
        : m_count(0)
    {
    }
    
    void addBorder(const CollapsedBorderValue& borderValue, BoxSide borderSide, bool shouldPaint,
        LayoutUnit x1, LayoutUnit y1, LayoutUnit x2, LayoutUnit y2, BorderStyle borderStyle)
    {
        if (borderValue.exists() && shouldPaint) {
            m_borders[m_count].borderValue = borderValue;
            m_borders[m_count].side = borderSide;
            m_borders[m_count].shouldPaint = shouldPaint;
            m_borders[m_count].x1 = x1;
            m_borders[m_count].x2 = x2;
            m_borders[m_count].y1 = y1;
            m_borders[m_count].y2 = y2;
            m_borders[m_count].style = borderStyle;
            m_count++;
        }
    }

    CollapsedBorder* nextBorder()
    {
        for (unsigned i = 0; i < m_count; i++) {
            if (m_borders[i].borderValue.exists() && m_borders[i].shouldPaint) {
                m_borders[i].shouldPaint = false;
                return &m_borders[i];
            }
        }
        
        return 0;
    }
    
    std::array<CollapsedBorder, 4> m_borders;
    unsigned m_count;
};

static void addBorderStyle(RenderTable::CollapsedBorderValues& borderValues,
                           CollapsedBorderValue borderValue)
{
    if (!borderValue.exists())
        return;
    size_t count = borderValues.size();
    for (size_t i = 0; i < count; ++i)
        if (borderValues[i].isSameIgnoringColor(borderValue))
            return;
    borderValues.append(borderValue);
}

void RenderTableCell::collectBorderValues(RenderTable::CollapsedBorderValues& borderValues) const
{
    addBorderStyle(borderValues, collapsedStartBorder());
    addBorderStyle(borderValues, collapsedEndBorder());
    addBorderStyle(borderValues, collapsedBeforeBorder());
    addBorderStyle(borderValues, collapsedAfterBorder());
}

void RenderTableCell::sortBorderValues(RenderTable::CollapsedBorderValues& borderValues)
{
    std::ranges::sort(borderValues, compareBorders);
}

void RenderTableCell::paintCollapsedBorders(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    ASSERT(paintInfo.phase == PaintPhase::CollapsedTableBorders);

    if (!paintInfo.shouldPaintWithinRoot(*this) || style().usedVisibility() != Visibility::Visible)
        return;

    LayoutRect localRepaintRect = paintInfo.rect;
    LayoutRect paintRect = LayoutRect(paintOffset + location(), frameRect().size());
    if (paintRect.y() - table()->outerBorderTop() >= localRepaintRect.maxY())
        return;

    if (paintRect.maxY() + table()->outerBorderBottom() <= localRepaintRect.y())
        return;

    GraphicsContext& graphicsContext = paintInfo.context();
    if (!table()->currentBorderValue() || graphicsContext.paintingDisabled())
        return;

    WritingMode writingMode = tableWritingMode();
    CollapsedBorderValue leftVal = cachedCollapsedLeftBorder(writingMode);
    CollapsedBorderValue rightVal = cachedCollapsedRightBorder(writingMode);
    CollapsedBorderValue topVal = cachedCollapsedTopBorder(writingMode);
    CollapsedBorderValue bottomVal = cachedCollapsedBottomBorder(writingMode);

    // Adjust our x/y/width/height so that we paint the collapsed borders at the correct location.
    LayoutUnit topWidth = topVal.width();
    LayoutUnit bottomWidth = bottomVal.width();
    LayoutUnit leftWidth = leftVal.width();
    LayoutUnit rightWidth = rightVal.width();

    float deviceScaleFactor = document().deviceScaleFactor();
    LayoutUnit leftHalfCollapsedBorder = CollapsedBorderValue::adjustedCollapsedBorderWidth(leftWidth , deviceScaleFactor, false);
    LayoutUnit topHalfCollapsedBorder = CollapsedBorderValue::adjustedCollapsedBorderWidth(topWidth, deviceScaleFactor, false);
    LayoutUnit righHalftCollapsedBorder = CollapsedBorderValue::adjustedCollapsedBorderWidth(rightWidth, deviceScaleFactor, true);
    LayoutUnit bottomHalfCollapsedBorder = CollapsedBorderValue::adjustedCollapsedBorderWidth(bottomWidth, deviceScaleFactor, true);
    
    LayoutRect borderRect = LayoutRect(paintRect.x() - leftHalfCollapsedBorder,
        paintRect.y() - topHalfCollapsedBorder,
        paintRect.width() + leftHalfCollapsedBorder + righHalftCollapsedBorder,
        paintRect.height() + topHalfCollapsedBorder + bottomHalfCollapsedBorder);

    BorderStyle topStyle = collapsedBorderStyle(topVal.style());
    BorderStyle bottomStyle = collapsedBorderStyle(bottomVal.style());
    BorderStyle leftStyle = collapsedBorderStyle(leftVal.style());
    BorderStyle rightStyle = collapsedBorderStyle(rightVal.style());
    
    bool renderTop = topStyle > BorderStyle::Hidden && !topVal.isTransparent() && floorToDevicePixel(topWidth, deviceScaleFactor);
    bool renderBottom = bottomStyle > BorderStyle::Hidden && !bottomVal.isTransparent() && floorToDevicePixel(bottomWidth, deviceScaleFactor);
    bool renderLeft = leftStyle > BorderStyle::Hidden && !leftVal.isTransparent() && floorToDevicePixel(leftWidth, deviceScaleFactor);
    bool renderRight = rightStyle > BorderStyle::Hidden && !rightVal.isTransparent() && floorToDevicePixel(rightWidth, deviceScaleFactor);

    // We never paint diagonals at the joins.  We simply let the border with the highest
    // precedence paint on top of borders with lower precedence.  
    CollapsedBorders borders;
    borders.addBorder(topVal, BoxSide::Top, renderTop, borderRect.x(), borderRect.y(), borderRect.maxX(), borderRect.y() + topWidth, topStyle);
    borders.addBorder(bottomVal, BoxSide::Bottom, renderBottom, borderRect.x(), borderRect.maxY() - bottomWidth, borderRect.maxX(), borderRect.maxY(), bottomStyle);
    borders.addBorder(leftVal, BoxSide::Left, renderLeft, borderRect.x(), borderRect.y(), borderRect.x() + leftWidth, borderRect.maxY(), leftStyle);
    borders.addBorder(rightVal, BoxSide::Right, renderRight, borderRect.maxX() - rightWidth, borderRect.y(), borderRect.maxX(), borderRect.maxY(), rightStyle);

    bool antialias = BorderPainter::shouldAntialiasLines(graphicsContext);
    
    for (CollapsedBorder* border = borders.nextBorder(); border; border = borders.nextBorder()) {
        if (border->borderValue.isSameIgnoringColor(*table()->currentBorderValue()))
            BorderPainter::drawLineForBoxSide(graphicsContext, document(), LayoutRect(LayoutPoint(border->x1, border->y1), LayoutPoint(border->x2, border->y2)), border->side,
                border->borderValue.color(), border->style, 0, 0, antialias);
    }
}

static LayoutRect backgroundRectForRow(const RenderBox& tableRow, const RenderTable& table)
{
    LayoutRect rect = tableRow.frameRect();
    if (!table.collapseBorders()) {
        // Row frameRects include unwanted hSpacing on both inline ends.
        auto hSpacing = table.hBorderSpacing();
        LayoutUnit vSpacing = 0_lu;
        if (table.writingMode().isHorizontal())
            rect.contract({ vSpacing, hSpacing, vSpacing, hSpacing });
        else
            rect.contract({ hSpacing, vSpacing, hSpacing, vSpacing });
    }
    return rect;
}

static LayoutRect backgroundRectForSection(const RenderTableSection& tableSection, const RenderTable& table)
{
    LayoutRect rect = { { }, tableSection.size() };
    if (!table.collapseBorders()) {
        auto hSpacing = table.hBorderSpacing();
        auto vSpacing = table.vBorderSpacing();
        // All sections' size()s include unwanted vSpacing at the block-end
        // position. The first section's size() includes additional unwanted
        // vSpacing at the block-start position. All sections' size()s include
        // unwanted hSpacing on both inline ends.
        auto beforeBlockSpacing = &tableSection == table.topSection() ? vSpacing : 0_lu;
        if (table.writingMode().isHorizontal())
            rect.contract({ beforeBlockSpacing, hSpacing, vSpacing, hSpacing });
        else if (table.writingMode().isBlockFlipped())
            rect.contract({ hSpacing, beforeBlockSpacing, hSpacing, vSpacing });
        else
            rect.contract({ hSpacing, vSpacing, hSpacing, beforeBlockSpacing });
    }
    return rect;
}

void RenderTableCell::paintBackgroundsBehindCell(PaintInfo& paintInfo, LayoutPoint paintOffset, RenderBox* backgroundObject, LayoutPoint backgroundPaintOffset)
{
    ASSERT(backgroundObject);

    if (!paintInfo.shouldPaintWithinRoot(*this))
        return;

    if (style().usedVisibility() != Visibility::Visible)
        return;

    RenderTable* tableElt = table();
    if (!tableElt->collapseBorders() && style().emptyCells() == EmptyCell::Hide && !firstChild())
        return;

    const auto& style = backgroundObject->style();
    auto& bgLayer = style.backgroundLayers();

    auto color = style.visitedDependentColor(CSSPropertyBackgroundColor);
    if (!bgLayer.hasImage() && !color.isVisible())
        return;

    color = style.colorByApplyingColorFilter(color);

    LayoutPoint adjustedPaintOffset = paintOffset;
    if (backgroundObject != this)
        adjustedPaintOffset.moveBy(location());

    // Background images attached to the row or row group must span the row
    // or row group. Draw them at the backgroundObject's dimensions, but
    // clipped to this cell.
    // FIXME: This should also apply to columns and column groups.
    bool paintBackgroundObject = backgroundObject != this && bgLayer.hasImage() && !is<RenderTableCol>(backgroundObject);
    // We have to clip here because the background would paint
    // on top of the borders otherwise. This only matters for cells and rows.
    bool shouldClip = paintBackgroundObject || (backgroundObject->hasLayer() && (backgroundObject == this || backgroundObject == parent()) && tableElt->collapseBorders());
    GraphicsContextStateSaver stateSaver(paintInfo.context(), shouldClip);
    if (paintBackgroundObject)
        paintInfo.context().clip({ adjustedPaintOffset, size() });
    else if (shouldClip) {
        LayoutRect clipRect(adjustedPaintOffset.x() + borderLeft(), adjustedPaintOffset.y() + borderTop(),
            width() - borderLeft() - borderRight(), height() - borderTop() - borderBottom());
        paintInfo.context().clip(clipRect);
    }
    LayoutRect fillRect;
    if (paintBackgroundObject) {
        if (auto* tableSectionRenderer = dynamicDowncast<RenderTableSection>(backgroundObject))
            fillRect = backgroundRectForSection(*tableSectionRenderer, *tableElt);
        else
            fillRect = backgroundRectForRow(*backgroundObject, *tableElt);
        fillRect.moveBy(backgroundPaintOffset);
    } else
        fillRect = LayoutRect { adjustedPaintOffset, size() };
    auto compositeOp = document().compositeOperatorForBackgroundColor(color, *this);
    BackgroundPainter painter { *this, paintInfo };
    if (backgroundObject != this) {
        painter.setOverrideClip(FillBox::BorderBox);
        painter.setOverrideOrigin(FillBox::BorderBox);
    }
    painter.paintFillLayers(color, bgLayer, fillRect, BleedAvoidance::None, compositeOp, backgroundObject);
}

void RenderTableCell::paintBoxDecorations(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!paintInfo.shouldPaintWithinRoot(*this))
        return;

    RenderTable* table = this->table();
    if (!table->collapseBorders() && style().emptyCells() == EmptyCell::Hide && !firstChild())
        return;

    LayoutRect paintRect = LayoutRect(paintOffset, frameRect().size());
    adjustBorderBoxRectForPainting(paintRect);

    BackgroundPainter backgroundPainter { *this, paintInfo };
    backgroundPainter.paintBoxShadow(paintRect, style(), Style::ShadowStyle::Normal);

    // Paint our cell background.
    paintBackgroundsBehindCell(paintInfo, paintOffset, this, paintOffset);

    backgroundPainter.paintBoxShadow(paintRect, style(), Style::ShadowStyle::Inset);

    if (!style().hasBorder() || table->collapseBorders())
        return;

    BorderPainter borderPainter { *this, paintInfo };
    borderPainter.paintBorder(paintRect, style());
}

void RenderTableCell::paintMask(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (style().usedVisibility() != Visibility::Visible || paintInfo.phase != PaintPhase::Mask)
        return;

    RenderTable* tableElt = table();
    if (!tableElt->collapseBorders() && style().emptyCells() == EmptyCell::Hide && !firstChild())
        return;
   
    LayoutRect paintRect = LayoutRect(paintOffset, frameRect().size());
    adjustBorderBoxRectForPainting(paintRect);

    paintMaskImages(paintInfo, paintRect);
}

void RenderTableCell::scrollbarsChanged(bool horizontalScrollbarChanged, bool verticalScrollbarChanged)
{
    LayoutUnit scrollbarHeight = scrollbarLogicalHeight();
    if (!scrollbarHeight)
        return; // Not sure if we should be doing something when a scrollbar goes away or not.
    
    // We only care if the scrollbar that affects our intrinsic padding has been added.
    if ((isHorizontalWritingMode() && !horizontalScrollbarChanged) ||
        (!isHorizontalWritingMode() && !verticalScrollbarChanged))
        return;

    // Shrink our intrinsic padding as much as possible to accommodate the scrollbar.
    if ((WTF::holdsAlternative<CSS::Keyword::Middle>(style().verticalAlign()) && style().alignContent().isNormal())
        || style().alignContent().isCentered()) {
        LayoutUnit totalHeight = logicalHeight();
        LayoutUnit heightWithoutIntrinsicPadding = totalHeight - intrinsicPaddingBefore() - intrinsicPaddingAfter();
        totalHeight -= scrollbarHeight;
        LayoutUnit newBeforePadding = (totalHeight - heightWithoutIntrinsicPadding) / 2;
        LayoutUnit newAfterPadding = totalHeight - heightWithoutIntrinsicPadding - newBeforePadding;
        setIntrinsicPaddingBefore(newBeforePadding);
        setIntrinsicPaddingAfter(newAfterPadding);
    } else
        setIntrinsicPaddingAfter(intrinsicPaddingAfter() - scrollbarHeight);
}

bool RenderTableCell::hasLineIfEmpty() const
{
    if (element() && element()->hasEditableStyle())
        return true;

    return RenderBlock::hasLineIfEmpty();
}

} // namespace WebCore
