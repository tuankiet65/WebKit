/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#include "config.h"
#include "FormattingContext.h"

#include "FormattingGeometry.h"
#include "FormattingQuirks.h"
#include "LayoutBoxGeometry.h"
#include "LayoutBoxInlines.h"
#include "LayoutContainingBlockChainIterator.h"
#include "LayoutContext.h"
#include "LayoutDescendantIterator.h"
#include "LayoutElementBox.h"
#include "LayoutInitialContainingBlock.h"
#include "LayoutIntegrationBoxGeometryUpdater.h"
#include "LayoutState.h"
#include "Logging.h"
#include "RenderBlockFlow.h"
#include "RenderStyleInlines.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/TextStream.h>

namespace WebCore {
namespace Layout {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(FormattingContext);

FormattingContext::FormattingContext(const ElementBox& formattingContextRoot, LayoutState& layoutState)
    : m_root(formattingContextRoot)
    , m_layoutState(layoutState)
{
    ASSERT(formattingContextRoot.hasChild());
#ifndef NDEBUG
    layoutState.registerFormattingContext(*this);
#endif
}

FormattingContext::~FormattingContext()
{
#ifndef NDEBUG
    layoutState().deregisterFormattingContext(*this);
#endif
}

LayoutState& FormattingContext::layoutState()
{
    return m_layoutState;
}

BoxGeometry& FormattingContext::geometryForBox(const Box& layoutBox, std::optional<EscapeReason>)
{
    return m_layoutState->ensureGeometryForBox(layoutBox);
}

const BoxGeometry& FormattingContext::geometryForBox(const Box& layoutBox, std::optional<EscapeReason> escapeReason) const
{
    UNUSED_PARAM(escapeReason);
#if ASSERT_ENABLED
    auto isOkToAccessBoxGeometry = [&] {
        if (!is<InitialContainingBlock>(layoutBox) && &formattingContextRoot(layoutBox) == &root()) {
            // This is the non-escape case of accessing a box's geometry information within the same formatting context.
            return true;
        }

        if (!escapeReason) {
            // Any geometry access outside of the formatting context without a valid reason is considered an escape.
            return false;
        }

        if (*escapeReason == EscapeReason::DocumentBoxStretchesToViewportQuirk) {
            ASSERT(layoutState().inQuirksMode());
            return is<InitialContainingBlock>(layoutBox);
        }

        if (*escapeReason == EscapeReason::BodyStretchesToViewportQuirk) {
            ASSERT(layoutState().inQuirksMode());
            return is<InitialContainingBlock>(layoutBox) || layoutBox.isDocumentBox();

        }

        if (*escapeReason == EscapeReason::TableQuirkNeedsGeometryFromEstablishedFormattingContext) {
            // This is the case when the table formatting root collects geometry information from the cell's
            // formatting context to be able to determine width/height. see shouldIgnoreChildContentVerticalMargin
            ASSERT(root().establishesTableFormattingContext());
            return &formattingContextRoot(formattingContextRoot(layoutBox)) == &root();
        }

        if (*escapeReason == EscapeReason::OutOfFlowBoxNeedsInFlowGeometry) {
            // When computing the static position of an out-of-flow box, we need to gather sibling/parent geometry information
            // as if the out-of-flow box was a simple inflow box.
            // Now since the out-of-flow and the sibling/parent boxes could very well be in different containing block subtrees
            // the formatting context they live in could also be very different.
            return true;
        }

        if (*escapeReason == EscapeReason::FloatBoxIsAlwaysRelativeToFloatStateRoot) {
            // Float box top/left values are mapped relative to the FloatState's root. Inline formatting contexts(A) inherit floats from parent
            // block formatting contexts(B). Floats in these inline formatting contexts(A) need to be mapped to the parent, block formatting context(B).
            if (layoutBox.isInlineIntegrationRoot())
                return true;

            auto& formattingContextRootForBox = formattingContextRoot(layoutBox);
            return &formattingContextRootForBox == &root() || &formattingContextRootForBox == &formattingContextRoot(root());
        }

        if (*escapeReason == EscapeReason::FindFixedHeightAncestorQuirk) {
            ASSERT(layoutState().inQuirksMode());
            // Find the first containing block with fixed height quirk. See Quirks::heightValueOfNearestContainingBlockWithFixedHeight.
            // This is only to check if the targetFormattingRoot is an ancestor formatting root.
            if (is<InitialContainingBlock>(layoutBox))
                return true;
            auto& targetFormattingRoot = formattingContextRoot(layoutBox);
            auto* ancestorFormattingContextRoot = &formattingContextRoot(root());
            while (true) {
                if (&targetFormattingRoot == ancestorFormattingContextRoot)
                    return true;
                ancestorFormattingContextRoot = &FormattingContext::formattingContextRoot(*ancestorFormattingContextRoot);
                if (is<InitialContainingBlock>(*ancestorFormattingContextRoot))
                    return true;
            }
            return false;
        }

        if (*escapeReason == EscapeReason::TableNeedsAccessToTableWrapper) {
            // Tables are wrapped in a 2 level formatting context structure. A <table> element initiates a block formatting context for its principal table box
            // where the caption and the table content live. It also initiates a table wrapper box which establishes the table formatting context.
            // In many cases the TFC needs access to the parent (generated) BFC.
            return &layoutBox == &formattingContextRoot(root());
        }

        ASSERT_NOT_REACHED();
        return false;
    };
#endif
    ASSERT(isOkToAccessBoxGeometry());
    ASSERT(m_layoutState->hasBoxGeometry(layoutBox));
    return m_layoutState->geometryForBox(layoutBox);
}

const InitialContainingBlock& FormattingContext::initialContainingBlock(const Box& layoutBox)
{
    if (auto* initialContainingBlock = dynamicDowncast<InitialContainingBlock>(layoutBox))
        return *initialContainingBlock;

    auto* ancestor = &layoutBox.parent();
    for (; !is<InitialContainingBlock>(*ancestor); ancestor = &ancestor->parent()) { }
    return downcast<InitialContainingBlock>(*ancestor);
}

const ElementBox& FormattingContext::containingBlock(const Box& layoutBox)
{
    // If we ever end up here with the ICB, we must be doing something not-so-great.
    RELEASE_ASSERT(!is<InitialContainingBlock>(layoutBox));
    // The containing block in which the root element lives is a rectangle called the initial containing block.
    // For other elements, if the element's position is 'relative' or 'static', the containing block is formed by the
    // content edge of the nearest block container ancestor box or which establishes a formatting context.
    // If the element has 'position: fixed', the containing block is established by the viewport
    // If the element has 'position: absolute', the containing block is established by the nearest ancestor with a
    // 'position' of 'absolute', 'relative' or 'fixed'.
    if (!layoutBox.isPositioned() || layoutBox.isInFlowPositioned()) {
        auto* ancestor = &layoutBox.parent();
        for (; !is<InitialContainingBlock>(*ancestor); ancestor = &ancestor->parent()) {
            if (ancestor->isContainingBlockForInFlow())
                return *ancestor;
        }
        return *ancestor;
    }

    if (layoutBox.isFixedPositioned()) {
        auto* ancestor = &layoutBox.parent();
        for (; !is<InitialContainingBlock>(*ancestor); ancestor = &ancestor->parent()) {
            if (ancestor->isContainingBlockForFixedPosition())
                return *ancestor;
        }
        return *ancestor;
    }

    if (layoutBox.isOutOfFlowPositioned()) {
        auto* ancestor = &layoutBox.parent();
        for (; !is<InitialContainingBlock>(*ancestor); ancestor = &ancestor->parent()) {
            if (ancestor->isContainingBlockForOutOfFlowPosition())
                return *ancestor;
        }
        return *ancestor;
    }

    ASSERT_NOT_REACHED();
    return layoutBox.parent();    
}

#if ASSERT_ENABLED
const ElementBox& FormattingContext::formattingContextRoot(const Box& layoutBox)
{
    // We should never need to ask this question on the ICB.
    ASSERT(!is<InitialContainingBlock>(layoutBox));
    // A box lives in the same formatting context as its containing block unless the containing block establishes a formatting context.
    // However relatively positioned (inflow) inline container lives in the formatting context where its parent lives unless
    // the parent establishes a formatting context.
    //
    // <div id=outer style="position: absolute"><div id=inner><span style="position: relative">content</span></div></div>
    // While the relatively positioned inline container (span) is placed relative to its containing block "outer", it lives in the inline
    // formatting context established by "inner".
    auto& ancestor = layoutBox.isInlineLevelBox() && layoutBox.isInFlowPositioned() ? layoutBox.parent() : containingBlock(layoutBox);
    if (ancestor.establishesFormattingContext())
        return ancestor;
    return formattingContextRoot(ancestor);
}

void FormattingContext::validateGeometryConstraintsAfterLayout() const
{
    auto& root = this->root();
    // FIXME: add a descendantsOfType<> flavor that stops at nested formatting contexts
    for (auto& layoutBox : descendantsOfType<Box>(root)) {
        if (&formattingContextRoot(layoutBox) != &root)
            continue;
        auto& containingBlockGeometry = geometryForBox(containingBlock(layoutBox));
        auto& boxGeometry = geometryForBox(layoutBox);

        // 10.3.3 Block-level, non-replaced elements in normal flow
        // 10.3.7 Absolutely positioned, non-replaced elements
        if ((layoutBox.isBlockLevelBox() || layoutBox.isOutOfFlowPositioned()) && !layoutBox.isReplacedBox()) {
            // margin-left + border-left-width + padding-left + width + padding-right + border-right-width + margin-right = width of containing block
            auto containingBlockWidth = containingBlockGeometry.contentBoxWidth();
            ASSERT(boxGeometry.horizontalMarginBorderAndPadding() + boxGeometry.contentBoxWidth() == containingBlockWidth);
        }

        // 10.6.4 Absolutely positioned, non-replaced elements
        if (layoutBox.isOutOfFlowPositioned() && !layoutBox.isReplacedBox()) {
            // top + margin-top + border-top-width + padding-top + height + padding-bottom + border-bottom-width + margin-bottom + bottom = height of containing block
            auto containingBlockHeight = containingBlockGeometry.contentBoxHeight();
            ASSERT(BoxGeometry::borderBoxTop(boxGeometry) + boxGeometry.verticalMarginBorderAndPadding() + boxGeometry.contentBoxHeight() == containingBlockHeight);
        }
    }
}
#endif

}
}
