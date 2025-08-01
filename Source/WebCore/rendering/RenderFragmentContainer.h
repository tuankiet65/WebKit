/*
 * Copyright (C) 2011 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER “AS IS” AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

#include "LayerFragment.h"
#include "RenderBlockFlow.h"
#include "RenderFragmentedFlow.h"
#include "VisiblePosition.h"
#include <memory>

namespace WebCore {

class Element;
class RenderBox;
class RenderBoxFragmentInfo;
class RenderFragmentedFlow;

class RenderFragmentContainer : public RenderBlockFlow {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(RenderFragmentContainer);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(RenderFragmentContainer);
public:
    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) override;

    void setFragmentedFlowPortionRect(const LayoutRect& rect) { m_fragmentedFlowPortionRect = rect; }
    LayoutRect fragmentedFlowPortionRect() const { return m_fragmentedFlowPortionRect; }
    LayoutRect fragmentedFlowPortionOverflowRect() const;

    LayoutPoint fragmentedFlowPortionLocation() const;

    virtual void attachFragment();
    virtual void detachFragment();

    RenderFragmentedFlow* fragmentedFlow() const { return m_fragmentedFlow.get(); }

    // Valid fragments do not create circular dependencies with other flows.
    bool isValid() const { return m_isValid; }
    void setIsValid(bool valid) { m_isValid = valid; }

    RenderBoxFragmentInfo* renderBoxFragmentInfo(const RenderBox&) const;
    RenderBoxFragmentInfo* setRenderBoxFragmentInfo(const RenderBox&, LayoutUnit logicalLeftInset, LayoutUnit logicalRightInset,
        bool containingBlockChainIsInset);
    std::unique_ptr<RenderBoxFragmentInfo> takeRenderBoxFragmentInfo(const RenderBox&);
    void removeRenderBoxFragmentInfo(const RenderBox&);

    void deleteAllRenderBoxFragmentInfo();

    bool isFirstFragment() const;
    bool isLastFragment() const;
    virtual bool shouldClipFragmentedFlowContent() const;

    // These methods represent the width and height of a "page" and for a RenderFragmentContainer they are just the
    // content width and content height of a fragment. For RenderFragmentContainerSets, however, they will be the width and
    // height of a single column or page in the set.
    virtual LayoutUnit pageLogicalWidth() const;
    virtual LayoutUnit pageLogicalHeight() const;

    LayoutUnit logicalTopOfFragmentedFlowContentRect(const LayoutRect&) const;
    LayoutUnit logicalBottomOfFragmentedFlowContentRect(const LayoutRect&) const;
    LayoutUnit logicalTopForFragmentedFlowContent() const { return logicalTopOfFragmentedFlowContentRect(fragmentedFlowPortionRect()); };
    LayoutUnit logicalBottomForFragmentedFlowContent() const { return logicalBottomOfFragmentedFlowContentRect(fragmentedFlowPortionRect()); };

    // This method represents the logical height of the entire flow thread portion used by the fragment or set.
    // For RenderFragmentContainers it matches logicalPaginationHeight(), but for sets it is the height of all the pages
    // or columns added together.
    virtual LayoutUnit logicalHeightOfAllFragmentedFlowContent() const;

    // The top of the nearest page inside the fragment. For RenderFragmentContainers, this is just the logical top of the
    // flow thread portion we contain. For sets, we have to figure out the top of the nearest column or
    // page.
    virtual LayoutUnit pageLogicalTopForOffset(LayoutUnit offset) const;

    // Whether or not this fragment is a set.
    virtual bool isRenderFragmentContainerSet() const { return false; }
    
    virtual void repaintFragmentedFlowContent(const LayoutRect& repaintRect) const;

    virtual void collectLayerFragments(LayerFragments&, const LayoutRect&, const LayoutRect&) const { }

    void addLayoutOverflowForBox(const RenderBox&, const LayoutRect&);
    void addVisualOverflowForBox(const RenderBox&, const LayoutRect&);
    LayoutRect visualOverflowRectForBox(const RenderBox&) const;
    LayoutRect layoutOverflowRectForBoxForPropagation(const RenderBox&);
    LayoutRect visualOverflowRectForBoxForPropagation(const RenderBox&);

    LayoutRect rectFlowPortionForBox(const RenderBox&, const LayoutRect&) const;
    
    void setFragmentObjectsFragmentStyle();
    void restoreFragmentObjectsOriginalStyle();

    bool canHaveChildren() const override { return false; }
    bool canHaveGeneratedChildren() const override { return true; }
    VisiblePosition positionForPoint(const LayoutPoint&, HitTestSource, const RenderFragmentContainer*) override;

    virtual Vector<LayoutRect> fragmentRectsForFlowContentRect(const LayoutRect&) const;
    virtual bool contentRectSpansFragments(const LayoutRect&) const { return false; }

protected:
    RenderFragmentContainer(Type, Element&, RenderStyle&&, RenderFragmentedFlow*);
    RenderFragmentContainer(Type, Document&, RenderStyle&&, RenderFragmentedFlow*);
    virtual ~RenderFragmentContainer();

    RenderOverflow* overflowForBox(const RenderBox&) const;

    void computePreferredLogicalWidths() override;
    void computeIntrinsicLogicalWidths(LayoutUnit& minLogicalWidth, LayoutUnit& maxLogicalWidth) const override;

    LayoutRect overflowRectForFragmentedFlowPortion(const LayoutRect& fragmentedFlowPortionRect, bool isFirstPortion, bool isLastPortion) const;
    void repaintFragmentedFlowContentRectangle(const LayoutRect& repaintRect, const LayoutRect& fragmentedFlowPortionRect, const LayoutPoint& fragmentLocation, const LayoutRect* fragmentedFlowPortionClipRect = 0) const;

    LayoutRect fragmentedFlowContentRectangle(const LayoutRect&, const LayoutRect& fragmentedFlowPortionRect, const LayoutPoint& fragmentLocation, const LayoutRect* fragmentedFlowPortionClipRect = 0) const;

private:
    ASCIILiteral renderName() const override { return "RenderFragmentContainer"_s; }

    void insertedIntoTree() override;
    void willBeRemovedFromTree() override;

    virtual void installFragmentedFlow();

    LayoutPoint mapFragmentPointIntoFragmentedFlowCoordinates(const LayoutPoint&);
    LayoutRect computedVisualOverflowRectForBox(const RenderBox&) const;
    LayoutRect computedLayoutOverflowRectForBox(const RenderBox&) const;

protected:
    SingleThreadWeakPtr<RenderFragmentedFlow> m_fragmentedFlow;

private:
    LayoutRect m_fragmentedFlowPortionRect;

    // This map holds unique information about a block that is split across fragments.
    // A RenderBoxFragmentInfo* tells us about any layout information for a RenderBox that
    // is unique to the fragment. For now it just holds logical width information for RenderBlocks, but eventually
    // it will also hold a custom style for any box (for fragment styling).
    using RenderBoxFragmentInfoMap = SingleThreadWeakHashMap<const RenderBox, std::unique_ptr<RenderBoxFragmentInfo>>;
    RenderBoxFragmentInfoMap m_renderBoxFragmentInfo;

    bool m_isValid { false };
};

class CurrentRenderFragmentContainerMaintainer {
    WTF_MAKE_NONCOPYABLE(CurrentRenderFragmentContainerMaintainer);
public:
    CurrentRenderFragmentContainerMaintainer(RenderFragmentContainer&);
    ~CurrentRenderFragmentContainerMaintainer();

    RenderFragmentContainer& fragment() const { return m_fragment; }
private:
    RenderFragmentContainer& m_fragment;
};

#ifndef NDEBUG
TextStream& operator<<(TextStream&, const RenderFragmentContainer&);
#endif

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderFragmentContainer, isRenderFragmentContainer())
