/*
 * Copyright (C) 2008-2025 Apple Inc. All rights reserved.
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

#include "config.h"

#include "AccessibilityObject.h"

#include "AXLogger.h"
#include "AXObjectCache.h"
#include "AXRemoteFrame.h"
#include "AXSearchManager.h"
#include "AXTextMarker.h"
#include "AccessibilityMockObject.h"
#include "AccessibilityRenderObject.h"
#include "AccessibilityScrollView.h"
#include "AccessibilityTable.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "ContainerNodeInlines.h"
#include "CustomElementDefaultARIA.h"
#include "DOMTokenList.h"
#include "DocumentInlines.h"
#include "EditingInlines.h"
#include "Editor.h"
#include "ElementInlines.h"
#include "ElementIterator.h"
#include "Event.h"
#include "EventDispatcher.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "FloatRect.h"
#include "FocusController.h"
#include "FrameLoader.h"
#include "FrameSelection.h"
#include "GeometryUtilities.h"
#include "HTMLAreaElement.h"
#include "HTMLBodyElement.h"
#include "HTMLDataListElement.h"
#include "HTMLDetailsElement.h"
#include "HTMLFormControlElement.h"
#include "HTMLInputElement.h"
#include "HTMLMediaElement.h"
#include "HTMLModelElement.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "HTMLSlotElement.h"
#include "HTMLTableSectionElement.h"
#include "HTMLTextAreaElement.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "LocalizedStrings.h"
#include "MathMLNames.h"
#include "NodeList.h"
#include "NodeName.h"
#include "NodeTraversal.h"
#include "Page.h"
#include "PositionInlines.h"
#include "ProgressTracker.h"
#include "Range.h"
#include "RenderElementInlines.h"
#include "RenderImage.h"
#include "RenderInline.h"
#include "RenderLayer.h"
#include "RenderLayerInlines.h"
#include "RenderListItem.h"
#include "RenderListMarker.h"
#include "RenderMenuList.h"
#include "RenderObjectInlines.h"
#include "RenderText.h"
#include "RenderTextControl.h"
#include "RenderTheme.h"
#include "RenderTreeBuilder.h"
#include "RenderView.h"
#include "RenderWidget.h"
#include "RenderedPosition.h"
#include "SVGNames.h"
#include "Settings.h"
#include "TextCheckerClient.h"
#include "TextCheckingHelper.h"
#include "TextIterator.h"
#include "UserGestureIndicator.h"
#include "VisibleUnits.h"
#include <numeric>
#include <wtf/NeverDestroyed.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringView.h>
#include <wtf/text/WTFString.h>
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {

using namespace HTMLNames;

AccessibilityObject::AccessibilityObject(AXID axID, AXObjectCache& cache)
    : AXCoreObject(axID)
    , m_axObjectCache(cache)
{
}

AccessibilityObject::~AccessibilityObject()
{
    ASSERT(isDetached());
}

void AccessibilityObject::init()
{
    m_role = determineAccessibilityRole();
}

std::optional<AXID> AccessibilityObject::treeID() const
{
    auto* cache = axObjectCache();
    return cache ? std::optional { cache->treeID() } : std::nullopt;
}

String AccessibilityObject::debugDescriptionInternal(bool verbose, std::optional<OptionSet<AXDebugStringOption>> debugOptions) const
{
    StringBuilder result;
    result.append("{"_s);
    result.append("role: "_s, accessibilityRoleToString(role()));
    result.append(", ID "_s, objectID().loggingString());

    if (debugOptions) {
        if (verbose || *debugOptions & AXDebugStringOption::Ignored)
            result.append(isIgnored() ? ", ignored"_s : emptyString());

        if (verbose || *debugOptions & AXDebugStringOption::RelativeFrame) {
            FloatRect frame = relativeFrame();
            result.append(", relativeFrame ((x: "_s, frame.x(), ", y: "_s, frame.y(), "), (w: "_s, frame.width(), ", h: "_s, frame.height(), "))"_s);
        }

        if (verbose || *debugOptions & AXDebugStringOption::RemoteFrameOffset)
            result.append(", remoteFrameOffset ("_s, remoteFrameOffset().x(), ", "_s, remoteFrameOffset().y(), ")"_s);

        if (verbose || *debugOptions & AXDebugStringOption::IsRemoteFrame)
            result.append(isRemoteFrame() ? ", remote frame"_s : emptyString());
    }

    if (auto* renderer = this->renderer())
        result.append(", "_s, renderer->debugDescription());
    else if (auto* node = this->node())
        result.append(", "_s, node->debugDescription());

    if (String extraDebugInfo = this->extraDebugInfo(); !extraDebugInfo.isEmpty()) {
        result.append(" ("_s);
        result.append(WTFMove(extraDebugInfo));
        result.append(")"_s);
    }

    result.append("}"_s);
    return result.toString();
}

void AccessibilityObject::detachRemoteParts(AccessibilityDetachmentType detachmentType)
{
    // Menu close events need to notify the platform. No element is used in the notification because it's a destruction event.
    if (detachmentType == AccessibilityDetachmentType::ElementDestroyed && role() == AccessibilityRole::Menu) {
        if (auto* cache = axObjectCache())
            cache->postNotification(nullptr, cache->document(), AXNotification::MenuClosed);
    }

    // Clear any children and call detachFromParent on them so that
    // no children are left with dangling pointers to their parent.
    clearChildren();
}

bool AccessibilityObject::isDetached() const
{
    return !wrapper();
}

OptionSet<AXAncestorFlag> AccessibilityObject::computeAncestorFlags() const
{
    OptionSet<AXAncestorFlag> computedFlags;

    if (hasAncestorFlag(AXAncestorFlag::IsInDescriptionListDetail) || matchesAncestorFlag(AXAncestorFlag::IsInDescriptionListDetail))
        computedFlags.set(AXAncestorFlag::IsInDescriptionListDetail, 1);

    if (hasAncestorFlag(AXAncestorFlag::IsInDescriptionListTerm) || matchesAncestorFlag(AXAncestorFlag::IsInDescriptionListTerm))
        computedFlags.set(AXAncestorFlag::IsInDescriptionListTerm, 1);

    if (hasAncestorFlag(AXAncestorFlag::IsInCell) || matchesAncestorFlag(AXAncestorFlag::IsInCell))
        computedFlags.set(AXAncestorFlag::IsInCell, 1);

    if (hasAncestorFlag(AXAncestorFlag::IsInRow) || matchesAncestorFlag(AXAncestorFlag::IsInRow))
        computedFlags.set(AXAncestorFlag::IsInRow, 1);

    return computedFlags;
}

OptionSet<AXAncestorFlag> AccessibilityObject::computeAncestorFlagsWithTraversal() const
{
    // If this object's flags are initialized, this traversal is unnecessary. Use AccessibilityObject::ancestorFlags() instead.
    ASSERT(!ancestorFlagsAreInitialized());

    OptionSet<AXAncestorFlag> computedFlags;
    computedFlags.set(AXAncestorFlag::FlagsInitialized, true);
    Accessibility::enumerateAncestors<AccessibilityObject>(*this, false, [&] (const AccessibilityObject& ancestor) {
        computedFlags.add(ancestor.computeAncestorFlags());
    });
    return computedFlags;
}

void AccessibilityObject::initializeAncestorFlags(const OptionSet<AXAncestorFlag>& flags)
{
    m_ancestorFlags.set(AXAncestorFlag::FlagsInitialized, true);
    m_ancestorFlags.add(flags);
}

bool AccessibilityObject::matchesAncestorFlag(AXAncestorFlag flag) const
{
    auto role = this->role();
    switch (flag) {
    case AXAncestorFlag::IsInDescriptionListDetail:
        return role == AccessibilityRole::DescriptionListDetail;
    case AXAncestorFlag::IsInDescriptionListTerm:
        return role == AccessibilityRole::DescriptionListTerm;
    case AXAncestorFlag::IsInCell:
        return role == AccessibilityRole::Cell;
    case AXAncestorFlag::IsInRow:
        return role == AccessibilityRole::Row;
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

bool AccessibilityObject::hasAncestorMatchingFlag(AXAncestorFlag flag) const
{
    return Accessibility::findAncestor<AccessibilityObject>(*this, false, [flag] (const AccessibilityObject& object) {
        if (object.ancestorFlagsAreInitialized())
            return object.ancestorFlags().contains(flag);

        return object.matchesAncestorFlag(flag);
    }) != nullptr;
}

bool AccessibilityObject::isInDescriptionListDetail() const
{
    if (ancestorFlagsAreInitialized())
        return m_ancestorFlags.contains(AXAncestorFlag::IsInDescriptionListDetail);

    return hasAncestorMatchingFlag(AXAncestorFlag::IsInDescriptionListDetail);
}

bool AccessibilityObject::isInDescriptionListTerm() const
{
    if (ancestorFlagsAreInitialized())
        return m_ancestorFlags.contains(AXAncestorFlag::IsInDescriptionListTerm);

    return hasAncestorMatchingFlag(AXAncestorFlag::IsInDescriptionListTerm);
}

bool AccessibilityObject::isInCell() const
{
    if (ancestorFlagsAreInitialized())
        return m_ancestorFlags.contains(AXAncestorFlag::IsInCell);

    return hasAncestorMatchingFlag(AXAncestorFlag::IsInCell);
}

bool AccessibilityObject::isInRow() const
{
    if (ancestorFlagsAreInitialized())
        return m_ancestorFlags.contains(AXAncestorFlag::IsInRow);

    return hasAncestorMatchingFlag(AXAncestorFlag::IsInRow);
}

// ARIA marks elements as having their accessible name derive from either their contents, or their author-provided name.
bool AccessibilityObject::accessibleNameDerivesFromContent() const
{
    // First check for objects specifically identified by ARIA.
    switch (ariaRoleAttribute()) {
    case AccessibilityRole::ApplicationAlert:
    case AccessibilityRole::ApplicationAlertDialog:
    case AccessibilityRole::ApplicationDialog:
    case AccessibilityRole::ApplicationLog:
    case AccessibilityRole::ApplicationMarquee:
    case AccessibilityRole::ApplicationStatus:
    case AccessibilityRole::ApplicationTimer:
    case AccessibilityRole::ComboBox:
    case AccessibilityRole::Definition:
    case AccessibilityRole::Document:
    case AccessibilityRole::DocumentArticle:
    case AccessibilityRole::DocumentMath:
    case AccessibilityRole::DocumentNote:
    case AccessibilityRole::LandmarkRegion:
    case AccessibilityRole::LandmarkDocRegion:
    case AccessibilityRole::Form:
    case AccessibilityRole::Grid:
    case AccessibilityRole::Group:
    case AccessibilityRole::Image:
    case AccessibilityRole::List:
    case AccessibilityRole::ListBox:
    case AccessibilityRole::LandmarkBanner:
    case AccessibilityRole::LandmarkComplementary:
    case AccessibilityRole::LandmarkContentInfo:
    case AccessibilityRole::LandmarkNavigation:
    case AccessibilityRole::LandmarkMain:
    case AccessibilityRole::LandmarkSearch:
    case AccessibilityRole::Menu:
    case AccessibilityRole::MenuBar:
    case AccessibilityRole::ProgressIndicator:
    case AccessibilityRole::Meter:
    case AccessibilityRole::RadioGroup:
    case AccessibilityRole::SectionFooter:
    case AccessibilityRole::SectionHeader:
    case AccessibilityRole::ScrollBar:
    case AccessibilityRole::Slider:
    case AccessibilityRole::SpinButton:
    case AccessibilityRole::Splitter:
    case AccessibilityRole::Table:
    case AccessibilityRole::TabList:
    case AccessibilityRole::TabPanel:
    case AccessibilityRole::TextArea:
    case AccessibilityRole::TextField:
    case AccessibilityRole::Toolbar:
    case AccessibilityRole::TreeGrid:
    case AccessibilityRole::Tree:
    case AccessibilityRole::WebApplication:
        return false;
    default:
        break;
    }
    
    // Now check for generically derived elements now that we know the element does not match a specific ARIA role.
    switch (role()) {
    case AccessibilityRole::Slider:
    case AccessibilityRole::ListBox:
        return false;
    default:
        break;
    }
    
    return true;
}

// https://github.com/w3c/aria/pull/1860
// If accname cannot be derived from content or author, accname can be derived on permitted roles
// from the first descendant element node with a heading role.
bool AccessibilityObject::accessibleNameDerivesFromHeading() const
{
    switch (role()) {
    case AccessibilityRole::ApplicationAlertDialog:
    case AccessibilityRole::ApplicationDialog:
    case AccessibilityRole::DocumentArticle:
        return true;
    default:
        return false;
    }
}

String AccessibilityObject::computedLabel()
{
    // This method is being called by WebKit inspector, which may happen at any time, so we need to update our backing store now.
    // Also hold onto this object in case updateBackingStore deletes this node.
    Ref protectedThis { *this };
    updateBackingStore();
    Vector<AccessibilityText> text;
    accessibilityText(text);
    if (text.size())
        return text[0].text;
    return { };
}

bool AccessibilityObject::isARIATextControl() const
{
    return ariaRoleAttribute() == AccessibilityRole::TextArea || ariaRoleAttribute() == AccessibilityRole::TextField || ariaRoleAttribute() == AccessibilityRole::SearchField;
}

bool AccessibilityObject::isEditableWebArea() const
{
    if (!isWebArea())
        return false;

    RefPtr page = this->page();
    if (page && page->isEditable())
        return true;

    RefPtr document = this->document();
    return document && document->inDesignMode();
}

bool AccessibilityObject::isNonNativeTextControl() const
{
    return (isARIATextControl() || hasContentEditableAttributeSet()) && !isNativeTextControl();
}

Vector<AXTextMarkerRange> AccessibilityObject::misspellingRanges() const
{
    AXTRACE("AccessibilityObject::misspellingRanges"_s);

    RefPtr node = this->node();
    if (!node)
        return { };

    RefPtr frame = node->document().frame();
    if (!frame)
        return { };

    auto* textChecker = frame->editor().textChecker();
    if (!textChecker)
        return { };

    // In order to resolve to the correct ranges, Editor::rangeForTextCheckingResult(...)
    // assumes that the selection is within the Node for which text we are calling checkTextOfParagraph.
    // Therefore, remember the current selection, set it to the beginning of the Node and restore it aftwards.
    auto originalSelection = frame->selection().selection();
    if (auto range = simpleRange()) {
        // Passing UserTriggered::No, which is the default value, guaranties that accessibility is not notified of text selection changes.
        frame->selection().setSelectedRange(SimpleRange { range->start, range->start }, Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes, UserTriggered::No);
    }

    Vector<AXTextMarkerRange> ranges;
    if (unifiedTextCheckerEnabled(frame.get())) {
        Vector<TextCheckingResult> misspellings;
        checkTextOfParagraph(*textChecker, stringValue(), TextCheckingType::Spelling, misspellings, frame->selection().selection());
        for (auto& misspelling : misspellings) {
            if (auto range = frame->editor().rangeForTextCheckingResult(misspelling))
                ranges.append(range);
        }
    } else {
        int location = -1;
        int length = 0;
        textChecker->checkSpellingOfString(stringValue(), &location, &length);
        if (location > -1 && length > 0)
            ranges = { { treeID(), objectID(), static_cast<unsigned>(location), static_cast<unsigned>(length) } };
    }

    frame->selection().setSelectedRange(originalSelection.range(), Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes, UserTriggered::No);
    return ranges;
}

std::optional<SimpleRange> AccessibilityObject::misspellingRange(const SimpleRange& start, AccessibilitySearchDirection direction) const
{
    auto node = this->node();
    if (!node)
        return std::nullopt;

    RefPtr frame = node->document().frame();
    if (!frame)
        return std::nullopt;

    if (!unifiedTextCheckerEnabled(frame.get()))
        return std::nullopt;

    Ref editor = frame->editor();

    TextCheckerClient* textChecker = editor->textChecker();
    if (!textChecker)
        return std::nullopt;

    Vector<TextCheckingResult> misspellings;
    checkTextOfParagraph(*textChecker, stringValue(), TextCheckingType::Spelling, misspellings, frame->selection().selection());

    // Find the first misspelling past the start.
    if (direction == AccessibilitySearchDirection::Next) {
        for (auto& misspelling : misspellings) {
            auto misspellingRange = editor->rangeForTextCheckingResult(misspelling);
            if (misspellingRange && is_gt(treeOrder<ComposedTree>(misspellingRange->end, start.end)))
                return *misspellingRange;
        }
    } else {
        for (auto& misspelling : makeReversedRange(misspellings)) {
            auto misspellingRange = editor->rangeForTextCheckingResult(misspelling);
            if (misspellingRange && is_lt(treeOrder<ComposedTree>(misspellingRange->start, start.start)))
                return *misspellingRange;
        }
    }

    return std::nullopt;
}

AXTextMarkerRange AccessibilityObject::textInputMarkedTextMarkerRange() const
{
    WeakPtr node = this->node();
    if (!node)
        return { };

    RefPtr frame = node->document().frame();
    if (!frame)
        return { };

    auto* cache = axObjectCache();
    if (!cache)
        return { };

    Ref editor = frame->editor();
    RefPtr object = cache->getOrCreate(editor->compositionNode());
    if (!object)
        return { };

    if (RefPtr observableObject = object->observableObject())
        object = observableObject;

    if (object->objectID() != objectID())
        return { };

    return { editor->compositionRange() };
}

AccessibilityObject* AccessibilityObject::displayContentsParent() const
{
    RefPtr parentNode = node() ? node()->parentNode() : nullptr;
    if (RefPtr parentElement = dynamicDowncast<Element>(parentNode); !parentElement || !parentElement->hasDisplayContents())
        return nullptr;

    auto* cache = axObjectCache();
    return cache ? cache->getOrCreate(*parentNode) : nullptr;
}

AccessibilityObject* AccessibilityObject::nextSiblingUnignored(unsigned limit) const
{
    ASSERT(limit);

    for (auto sibling = iterator(nextSibling()); limit && sibling; --limit, ++sibling) {
        if (!sibling->isIgnored())
            return sibling.ptr();
    }
    return nullptr;
}

AccessibilityObject* AccessibilityObject::previousSiblingUnignored(unsigned limit) const
{
    ASSERT(limit);

    for (auto sibling = iterator(previousSibling()); limit && sibling; --limit, --sibling) {
        if (!sibling->isIgnored())
            return sibling.ptr();
    }
    return nullptr;
}

FloatRect AccessibilityObject::convertFrameToSpace(const FloatRect& frameRect, AccessibilityConversionSpace conversionSpace) const
{
    ASSERT(isMainThread());

    // Find the appropriate scroll view to use to convert the contents to the window.
    RefPtr parentAccessibilityScrollView = ancestorAccessibilityScrollView(false /* includeSelf */);
    RefPtr parentScrollView = parentAccessibilityScrollView ? parentAccessibilityScrollView->scrollView() : nullptr;

    auto snappedFrameRect = snappedIntRect(IntRect(frameRect));
    if (parentScrollView)
        snappedFrameRect = parentScrollView->contentsToRootView(snappedFrameRect);
    
    if (conversionSpace == AccessibilityConversionSpace::Screen) {
        RefPtr page = this->page();
        if (!page)
            return snappedFrameRect;

        // If we have an empty chrome client (like SVG) then we should use the page
        // of the scroll view parent to help us get to the screen rect.
        if (parentAccessibilityScrollView && page->chrome().client().isEmptyChromeClient())
            page = parentAccessibilityScrollView->page();
        
        snappedFrameRect = page->chrome().rootViewToAccessibilityScreen(snappedFrameRect);
    }
    
    return snappedFrameRect;
}
    
FloatRect AccessibilityObject::relativeFrame() const
{
    auto rect = elementRect();
    rect.moveBy(remoteFrameOffset());
    return convertFrameToSpace(rect, AccessibilityConversionSpace::Page);
}

AccessibilityObject* AccessibilityObject::firstAccessibleObjectFromNode(const Node* node)
{
    return WebCore::firstAccessibleObjectFromNode(node, [] (const AccessibilityObject& accessible) {
        return !accessible.isIgnored();
    });
}

AccessibilityObject* firstAccessibleObjectFromNode(const Node* node, NOESCAPE const Function<bool(const AccessibilityObject&)>& isAccessible)
{
    RefPtr axNode = node;
    if (!axNode)
        return nullptr;

    AXObjectCache* cache = axNode->document().axObjectCache();
    if (!cache)
        return nullptr;

    RefPtr accessibleObject = cache->getOrCreate(axNode->renderer());
    while (accessibleObject && !isAccessible(*accessibleObject)) {
        axNode = NodeTraversal::next(*axNode);

        while (axNode && !axNode->renderer())
            axNode = NodeTraversal::nextSkippingChildren(*axNode);

        if (!axNode)
            return nullptr;

        accessibleObject = cache->getOrCreate(axNode->renderer());
    }

    return accessibleObject.get();
}

// FIXME: Usages of this function should be replaced by a new flag in AccessibilityObject::m_ancestorFlags.
bool AccessibilityObject::isDescendantOfRole(AccessibilityRole role) const
{
    return Accessibility::findAncestor<AccessibilityObject>(*this, false, [&role] (const AccessibilityObject& object) {
        return object.role() == role;
    }) != nullptr;
}

#if ASSERT_ENABLED
static bool isTableComponent(AXCoreObject& axObject)
{
    return axObject.isTable() || axObject.isTableColumn() || axObject.isTableRow() || axObject.isTableCell();
}
#endif

void AccessibilityObject::insertChild(AccessibilityObject& child, unsigned index, DescendIfIgnored descendIfIgnored)
{
    auto owners = child.owners();
    if (owners.size()) {
        size_t indexOfThis = owners.findIf([this] (const Ref<AXCoreObject>& object) {
            return object.ptr() == this;
        });

        if (indexOfThis == notFound) {
            // The child is aria-owned, and not by us, so we shouldn't insert it.
            return;
        }
    }

    if (is<HTMLAreaElement>(child.node())) [[unlikely]] {
        // Despite the DOM parent for <area> elements being <map>, we expose <area> elements as children
        // of the <img> using the <map>. This provides a better experience for AT users, e.g. a screenreader
        // would hear "image map" or "group" plus the image description, then the links, which provides the
        // added context for what the links represent.
        //
        // Due to the difference in DOM vs. expected AX hierarchy, make sure area elements are only inserted
        // by their associated image as children.
        if (child.parentObject() != this)
            return;
    }

    // If the parent is asking for this child's children, then either it's the first time (and clearing is a no-op),
    // or its visibility has changed. In the latter case, this child may have a stale child cached.
    // This can prevent aria-hidden changes from working correctly. Hence, whenever a parent is getting children, ensure data is not stale.
    // Only clear the child's children when we know it's in the updating chain in order to avoid unnecessary work.
    if (child.needsToUpdateChildren() || m_subtreeDirty) {
        child.clearChildren();
        // Pass m_subtreeDirty flag down to the child so that children cache gets reset properly.
        if (m_subtreeDirty)
            child.setNeedsToUpdateSubtree();
    }

#if USE(ATSPI)
    // FIXME: Consider removing this ATSPI-only branch with https://bugs.webkit.org/show_bug.cgi?id=282117.
    RefPtr displayContentsParent = child.displayContentsParent();
    // To avoid double-inserting a child of a `display: contents` element, only insert if `this` is the rightful parent.
    if (displayContentsParent && displayContentsParent != this) {
        // Make sure the display:contents parent object knows it has a child it needs to add.
        displayContentsParent->setNeedsToUpdateChildren();

        // Don't exit early for certain table components, as they rely on inserting children for which they are not the rightful parent to behave correctly.
        bool allowInsert = isTableColumn() || role() == AccessibilityRole::TableHeaderContainer;

        // AccessibilityTable::addChildren never actually calls `insertChild` for table section elements
        // (e.g. tbody, thead), so don't block this `insertChild` for display:contents section elements,
        // or else the child elements of the section element will never be inserted into the tree.
        allowInsert = allowInsert || (isAccessibilityTableInstance() && is<HTMLTableSectionElement>(displayContentsParent->element()));
        if (!allowInsert)
            return;
    }
#endif // USE(ATSPI)

    auto insert = [this] (Ref<AXCoreObject>&& object, unsigned index) {
        std::ignore = setChildIndexInParent(object.get(), index);
        m_children.insert(index, WTFMove(object));
    };

    auto thisAncestorFlags = computeAncestorFlags();
    child.initializeAncestorFlags(thisAncestorFlags);
    setIsIgnoredFromParentDataForChild(child);
    if (!includeIgnoredInCoreTree() && child.isIgnored()) {
        if (descendIfIgnored == DescendIfIgnored::Yes) {
            unsigned insertionIndex = index;
            auto childAncestorFlags = child.computeAncestorFlags();
            for (auto grandchildCoreObject : child.children()) {
                Ref grandchild = downcast<AccessibilityObject>(grandchildCoreObject.get());

                // Even though `child` is ignored, we still need to set ancestry flags based on it.
                grandchild->initializeAncestorFlags(childAncestorFlags);
                grandchild->addAncestorFlags(thisAncestorFlags);
                // Calls to `child.isIgnored()` or `child.children()` can cause layout, which in turn can cause this object to clear its m_children. This can cause `insertionIndex` to no longer be valid. Detect this and break early if necessary.
                if (insertionIndex > m_children.size())
                    break;
                insert(WTFMove(grandchild), insertionIndex);
                ++insertionIndex;
            }
        }
    } else {
        // Table component child-parent relationships often don't line up properly, hence the need for methods
        // like parentTable() and parentRow(). Exclude them from this ASSERT.
        // FIXME: We hit this ASSERT on gmail.com. https://bugs.webkit.org/show_bug.cgi?id=293264
        ASSERT(isTableComponent(child) || isTableComponent(*this) || child.parentObject() == this);
        insert(Ref { child }, index);
    }
    
    // Reset the child's m_isIgnoredFromParentData since we are done adding that child and its children.
    child.clearIsIgnoredFromParentData();
}

void AccessibilityObject::resetChildrenIndexInParent() const
{
    if (!shouldSetChildIndexInParent())
        return;

    unsigned index = 0;
    for (const auto& child : m_children) {
        bool didSet = setChildIndexInParent(child.get(), index);
        // We check shouldSetChildIndexInParent above, so this should always be true.
        ASSERT_UNUSED(didSet, didSet);
        ++index;
    }
}

AXCoreObject::AccessibilityChildrenVector AccessibilityObject::findMatchingObjects(AccessibilitySearchCriteria&& criteria)
{
    if (auto* cache = axObjectCache())
        cache->startCachingComputedObjectAttributesUntilTreeMutates();

    criteria.anchorObject = this;
    return AXSearchManager().findMatchingObjects(WTFMove(criteria));
}

// Returns the range that is fewer positions away from the reference range.
// NOTE: The after range is expected to ACTUALLY be after the reference range and the before
// range is expected to ACTUALLY be before. These are not checked for performance reasons.
static std::optional<SimpleRange> rangeClosestToRange(const SimpleRange& referenceRange, std::optional<SimpleRange>&& afterRange, std::optional<SimpleRange>&& beforeRange)
{
    if (!beforeRange)
        return WTFMove(afterRange);
    if (!afterRange)
        return WTFMove(beforeRange);
    auto distanceBefore = characterCount({ beforeRange->end, referenceRange.start });
    auto distanceAfter = characterCount({ afterRange->start, referenceRange.end });
    return WTFMove(distanceBefore <= distanceAfter ? beforeRange : afterRange);
}

std::optional<SimpleRange> AccessibilityObject::rangeOfStringClosestToRangeInDirection(const SimpleRange& referenceRange, AccessibilitySearchDirection searchDirection, const Vector<String>& searchStrings) const
{
    RefPtr frame = this->frame();
    if (!frame)
        return std::nullopt;
    
    bool isBackwardSearch = searchDirection == AccessibilitySearchDirection::Previous;
    FindOptions findOptions { FindOption::AtWordStarts, FindOption::AtWordEnds, FindOption::CaseInsensitive, FindOption::StartInSelection };
    if (isBackwardSearch)
        findOptions.add(FindOption::Backwards);
    
    std::optional<SimpleRange> closestStringRange;
    for (auto& searchString : searchStrings) {
        if (auto foundStringRange = frame->editor().rangeOfString(searchString, referenceRange, findOptions)) {
            bool foundStringIsCloser;
            if (!closestStringRange)
                foundStringIsCloser = true;
            else {
                foundStringIsCloser = isBackwardSearch
                    ? is_gt(treeOrder<ComposedTree>(foundStringRange->end, closestStringRange->end))
                    : is_lt(treeOrder<ComposedTree>(foundStringRange->start, closestStringRange->start));
            }
            if (foundStringIsCloser)
                closestStringRange = *foundStringRange;
        }
    }
    return closestStringRange;
}

VisibleSelection AccessibilityObject::selection() const
{
    RefPtr document = this->document();
    RefPtr frame = document ? document->frame() : nullptr;
    return frame ? frame->selection().selection() : VisibleSelection();
}

// Returns an collapsed range preceding the document contents if there is no selection.
// FIXME: Why is that behavior more useful than returning null in that case?
std::optional<SimpleRange> AccessibilityObject::selectionRange() const
{
    RefPtr frame = this->frame();
    if (!frame)
        return std::nullopt;

    if (auto range = frame->selection().selection().firstRange())
        return *range;

    Ref document = *frame->document();
    return { { { document.get(), 0 }, { document.get(), 0 } } };
}

std::optional<SimpleRange> AccessibilityObject::simpleRange() const
{
    auto* node = this->node();
    if (!node)
        return std::nullopt;
    return AXObjectCache::rangeForNodeContents(*node);
}

AXTextMarkerRange AccessibilityObject::textMarkerRange() const
{
    return simpleRange();
}

Vector<BoundaryPoint> AccessibilityObject::previousLineStartBoundaryPoints(const VisiblePosition& startingPosition, const SimpleRange& targetRange, unsigned positionsToRetrieve) const
{
    Vector<BoundaryPoint> boundaryPoints;
    boundaryPoints.reserveInitialCapacity(positionsToRetrieve);

    std::optional<VisiblePosition> lastPosition = startingPosition;
    for (unsigned i = 0; i < positionsToRetrieve; i++) {
        lastPosition = previousLineStartPositionInternal(*lastPosition);
        if (!lastPosition)
            break;

        auto boundaryPoint = makeBoundaryPoint(*lastPosition);
        if (!boundaryPoint || !contains(targetRange, *boundaryPoint))
            break;

        boundaryPoints.append(WTFMove(*boundaryPoint));
    }
    boundaryPoints.shrinkToFit();
    return boundaryPoints;
}

std::optional<BoundaryPoint> AccessibilityObject::lastBoundaryPointContainedInRect(const Vector<BoundaryPoint>& boundaryPoints, const BoundaryPoint& startBoundary, const FloatRect& rect, int leftIndex, int rightIndex, bool isFlippedWritingMode) const
{
    if (leftIndex > rightIndex || boundaryPoints.isEmpty())
        return std::nullopt;

    auto indexIsValid = [&] (int index) {
        return index >= 0 && static_cast<size_t>(index) < boundaryPoints.size();
    };
    auto boundaryPointContainedInRect = [&] (const BoundaryPoint& boundary) {
        return boundaryPointsContainedInRect(startBoundary, boundary, rect, isFlippedWritingMode);
    };

    int midIndex = std::midpoint(leftIndex, rightIndex);
    if (boundaryPointContainedInRect(boundaryPoints.at(midIndex))) {
        // We have a match if `midIndex` boundary point is contained in the rect, but the one at `midIndex - 1` isn't.
        if (indexIsValid(midIndex - 1) && !boundaryPointContainedInRect(boundaryPoints.at(midIndex - 1)))
            return boundaryPoints.at(midIndex);

        return lastBoundaryPointContainedInRect(boundaryPoints, startBoundary, rect, leftIndex, midIndex - 1, isFlippedWritingMode);
    }
    // And vice versa, we have a match if the `midIndex` boundary point is not contained in the rect, but the one at `midIndex + 1` is.
    if (indexIsValid(midIndex + 1) && boundaryPointContainedInRect(boundaryPoints.at(midIndex + 1)))
        return boundaryPoints.at(midIndex + 1);

    return lastBoundaryPointContainedInRect(boundaryPoints, startBoundary, rect, midIndex + 1, rightIndex, isFlippedWritingMode);
}

static IntPoint textStartPoint(const IntRect& rect, bool isFlippedWritingMode)
{
    if (!isFlippedWritingMode)
        return rect.minXMinYCorner();
    return rect.maxXMinYCorner();
}

static IntPoint textEndPoint(const IntRect& rect, bool isFlippedWritingMode)
{
    if (!isFlippedWritingMode)
        return rect.maxXMaxYCorner();
    return rect.minXMaxYCorner();
}

bool AccessibilityObject::boundaryPointsContainedInRect(const BoundaryPoint& startBoundary, const BoundaryPoint& endBoundary, const FloatRect& rect, bool isFlippedWritingMode) const
{
    auto elementRect = boundsForRange({ startBoundary, endBoundary });
    return rect.contains(textEndPoint(elementRect, isFlippedWritingMode));
}

std::optional<SimpleRange> AccessibilityObject::visibleCharacterRangeInternal(SimpleRange& range, const FloatRect& contentRect, const IntRect& startingElementRect) const
{
    if (!contentRect.intersects(startingElementRect))
        return std::nullopt;

    auto elementRect = startingElementRect;
    auto startBoundary = range.start;
    auto endBoundary = range.end;

    const auto* style = this->style();
    bool isFlipped = style && style->writingMode().isBlockFlipped();
    // In vertical-rl writing-modes (e.g. some Japanese text), text lays out vertically from right-to-left, meaning the the start of the text
    // has a larger `x`-coordinate than the end.
    bool laysOutIntoNegativeX = isFlipped && style->writingMode().isVertical();

    // Origin isn't contained in visible rect, start moving forward by line.
    while (!contentRect.contains(textStartPoint(elementRect, isFlipped))) {
        auto currentPosition = VisiblePosition(makeContainerOffsetPosition(startBoundary));
        auto nextLinePosition = nextLineEndPosition(currentPosition);
        if (nextLinePosition == currentPosition) {
            // We tried to move to the next line end, but got the same position back. Break to avoid
            // looping infinitely. It would be better if we understood *why* nextLineEndPosition
            // is returning the same position, but do this for now. If you hit this assert, please
            // file a bug with steps to reproduce.
            ASSERT_NOT_REACHED();
            break;
        }

        auto testStartBoundary = makeBoundaryPoint(nextLinePosition);
        if (!testStartBoundary || !contains(range, *testStartBoundary))
            break;

        // testStartBoundary is valid, so commit it and update the elementRect.
        startBoundary = *testStartBoundary;
        elementRect = boundsForRange(SimpleRange(startBoundary, range.end));
        if (elementRect.isEmpty() || (elementRect.x() < 0 && !laysOutIntoNegativeX) || elementRect.y() < 0)
            break;
    }

    bool didCorrectStartBoundary = false;
    // Sometimes we shrink one line too far -- check the previous line start to see if it's in bounds.
    auto previousLineStartPosition = previousLineStartPositionInternal(VisiblePosition(makeContainerOffsetPosition(startBoundary)));
    if (previousLineStartPosition) {
        if (auto previousLineStartBoundaryPoint = makeBoundaryPoint(*previousLineStartPosition)) {
            auto lineStartRect = boundsForRange(SimpleRange(*previousLineStartBoundaryPoint, range.end));
            if (previousLineStartBoundaryPoint->container.ptr() == startBoundary.container.ptr() && contentRect.contains(textStartPoint(lineStartRect, isFlipped))) {
                elementRect = lineStartRect;
                startBoundary = *previousLineStartBoundaryPoint;
                didCorrectStartBoundary = true;
            }
        }
    }

    if (!didCorrectStartBoundary) {
        // We iterated to a line-end position above. We must also check if the start of this line is in bounds.
        auto startBoundaryLineStartPosition = startOfLine(VisiblePosition(makeContainerOffsetPosition(startBoundary)));
        auto lineStartBoundaryPoint = makeBoundaryPoint(startBoundaryLineStartPosition);
        if (lineStartBoundaryPoint && lineStartBoundaryPoint->container.ptr() == startBoundary.container.ptr()) {
            auto lineStartRect = boundsForRange(SimpleRange(*lineStartBoundaryPoint, range.end));
            Ref lineStartContainer = lineStartBoundaryPoint->container;
            if (contentRect.contains(textStartPoint(lineStartRect, isFlipped))) {
                elementRect = lineStartRect;
                startBoundary = *lineStartBoundaryPoint;
            } else if (lineStartBoundaryPoint->offset < lineStartContainer->length()) {
                // Sometimes we're one character off from being in-bounds. Check for this too.
                lineStartBoundaryPoint->offset = lineStartBoundaryPoint->offset + 1;
                lineStartRect = boundsForRange(SimpleRange(*lineStartBoundaryPoint, range.end));
                lineStartBoundaryPoint->offset = lineStartBoundaryPoint->offset - 1;
                if (contentRect.contains(textStartPoint(lineStartRect, isFlipped))) {
                    elementRect = lineStartRect;
                    startBoundary = *lineStartBoundaryPoint;
                }
            }
        }
    }

    // Computing previous line start positions is cheap relative to computing boundsForRange, so compute the end boundary by
    // grabbing batches of lines and binary searching within them to minimize calls to boundsForRange.
    Vector<BoundaryPoint> boundaryPoints = { endBoundary };
    do {
        // If the first boundary point is contained in contentRect, then it's a match because we know everything in the last batch
        // of lines was not contained in contentRect.
        if (boundaryPointsContainedInRect(startBoundary, boundaryPoints.at(0), contentRect, isFlipped)) {
            endBoundary = boundaryPoints.at(0);
            break;
        }

        auto lastBoundaryPoint = boundaryPoints.last();
        elementRect = boundsForRange({ startBoundary, lastBoundaryPoint });
        if (elementRect.isEmpty())
            break;
        // Otherwise if the last boundary point is contained in contentRect, then we know some boundary point in this batch is
        // our target end boundary point.
        if (contentRect.contains(textEndPoint(elementRect, isFlipped))) {
            endBoundary = lastBoundaryPointContainedInRect(boundaryPoints, startBoundary, contentRect, isFlipped).value_or(lastBoundaryPoint);
            break;
        }
        boundaryPoints = previousLineStartBoundaryPoints(VisiblePosition(makeContainerOffsetPosition(lastBoundaryPoint)), range, 64);
    } while (!boundaryPoints.isEmpty());

    // Sometimes we shrink one line too far. Check the next line end to see if it's in bounds.
    auto nextLineEndPosition = this->nextLineEndPosition(VisiblePosition(makeContainerOffsetPosition(endBoundary)));
    auto nextLineEndBoundaryPoint = makeBoundaryPoint(nextLineEndPosition);
    if (nextLineEndBoundaryPoint && nextLineEndBoundaryPoint->container.ptr() == endBoundary.container.ptr()) {
        auto lineEndRect = boundsForRange(SimpleRange(startBoundary, *nextLineEndBoundaryPoint));

        if (contentRect.contains(textEndPoint(lineEndRect, isFlipped)))
            endBoundary = *nextLineEndBoundaryPoint;
    }

    return { { startBoundary, endBoundary } };
}

std::optional<SimpleRange> AccessibilityObject::findTextRange(const Vector<String>& searchStrings, const SimpleRange& start, AccessibilitySearchTextDirection direction) const
{
    std::optional<SimpleRange> found;
    if (direction == AccessibilitySearchTextDirection::Forward)
        found = rangeOfStringClosestToRangeInDirection(start, AccessibilitySearchDirection::Next, searchStrings);
    else if (direction == AccessibilitySearchTextDirection::Backward)
        found = rangeOfStringClosestToRangeInDirection(start, AccessibilitySearchDirection::Previous, searchStrings);
    else if (direction == AccessibilitySearchTextDirection::Closest) {
        auto foundAfter = rangeOfStringClosestToRangeInDirection(start, AccessibilitySearchDirection::Next, searchStrings);
        auto foundBefore = rangeOfStringClosestToRangeInDirection(start, AccessibilitySearchDirection::Previous, searchStrings);
        found = rangeClosestToRange(start, WTFMove(foundAfter), WTFMove(foundBefore));
    }
    if (found) {
        // If the search started within a text control, ensure that the result is inside that element.
        if (element() && element()->isTextField()) {
            if (!found->startContainer().isShadowIncludingDescendantOf(element())
                || !found->endContainer().isShadowIncludingDescendantOf(element()))
                return std::nullopt;
        }
    }
    return found;
}

Vector<SimpleRange> AccessibilityObject::findTextRanges(const AccessibilitySearchTextCriteria& criteria) const
{
    std::optional<SimpleRange> range;
    if (criteria.start == AccessibilitySearchTextStartFrom::Selection)
        range = selectionRange();
    else
        range = simpleRange();
    if (!range)
        return { };

    if (criteria.start == AccessibilitySearchTextStartFrom::Begin)
        range->end = range->start;
    else if (criteria.start == AccessibilitySearchTextStartFrom::End)
        range->start = range->end;
    else if (criteria.direction == AccessibilitySearchTextDirection::Backward)
        range->start = range->end;
    else
        range->end = range->start;

    Vector<SimpleRange> result;
    switch (criteria.direction) {
    case AccessibilitySearchTextDirection::Forward:
    case AccessibilitySearchTextDirection::Backward:
    case AccessibilitySearchTextDirection::Closest:
        if (auto foundRange = findTextRange(criteria.searchStrings, *range, criteria.direction))
            result.append(*foundRange);
        break;
    case AccessibilitySearchTextDirection::All:
        auto appendFoundRanges = [&](AccessibilitySearchTextDirection direction) {
            for (auto foundRange = range; (foundRange = findTextRange(criteria.searchStrings, *foundRange, direction)); )
                result.append(*foundRange);
        };
        appendFoundRanges(AccessibilitySearchTextDirection::Forward);
        appendFoundRanges(AccessibilitySearchTextDirection::Backward);
        break;
    }
    return result;
}

struct TextOperationRange {
    SimpleRange scope;
    CharacterRange characterRange;
};

static std::optional<TextOperationRange> textOperationRangeFromRange(const SimpleRange& range)
{
    RefPtr<Element> rootEditableElement = range.startContainer().rootEditableElement();
    if (!rootEditableElement)
        return std::nullopt;

    auto scopeStart = firstPositionInNode(rootEditableElement.get());
    auto scopeEnd = lastPositionInNode(rootEditableElement.get());

    std::optional<SimpleRange> scope = makeSimpleRange(scopeStart, scopeEnd);
    if (!scope)
        return std::nullopt;

    return TextOperationRange { *scope, characterRange(*scope, range, { }) };
}

static SimpleRange rangeFromTextOperationRange(const TextOperationRange& textOperationRange)
{
    return resolveCharacterRange(textOperationRange.scope, textOperationRange.characterRange, { });
}

Vector<String> AccessibilityObject::performTextOperation(const AccessibilityTextOperation& operation)
{
    Vector<TextOperationRange> textOperationRanges;
    textOperationRanges.reserveInitialCapacity(operation.textRanges.size());

    Vector<String> result;
    result.reserveInitialCapacity(operation.textRanges.size());

    if (operation.textRanges.isEmpty())
        return result;

    RefPtr frame = this->frame();
    if (!frame)
        return result;

    size_t replacementStringsCount = operation.replacementStrings.size();
    bool useFirstReplacementStringForAllReplacements = (replacementStringsCount == 1);

    // Precompute character ranges with respect to their root editable element because
    // the SimpleRanges stored in AccessibilityTextOperation may be invalidated after
    // performing a replacement in the same editable element.
    for (const auto& range : operation.textRanges) {
        auto textOperationRange = textOperationRangeFromRange(range);
        if (!textOperationRange) {
            ASSERT_NOT_REACHED();
            return result;
        }

        textOperationRanges.append(*textOperationRange);
    }

    for (size_t i = 0; i < textOperationRanges.size(); ++i) {
        const auto& textOperationRange = textOperationRanges[i];
        auto textRange = rangeFromTextOperationRange(textOperationRange);

        String replacementString;
        if (useFirstReplacementStringForAllReplacements)
            replacementString = operation.replacementStrings[0];
        else if (i < replacementStringsCount)
            replacementString = operation.replacementStrings[i];

        if (!frame->selection().setSelectedRange(textRange, Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes))
            continue;

        String text = plainText(textRange);
        bool replaceSelection = false;
        switch (operation.type) {
        case AccessibilityTextOperationType::Capitalize:
            replacementString = capitalize(text); // FIXME: Needs to take locale into account to work correctly.
            replaceSelection = true;
            break;
        case AccessibilityTextOperationType::Uppercase:
            replacementString = text.convertToUppercaseWithoutLocale(); // FIXME: Needs locale to work correctly.
            replaceSelection = true;
            break;
        case AccessibilityTextOperationType::Lowercase:
            replacementString = text.convertToLowercaseWithoutLocale(); // FIXME: Needs locale to work correctly.
            replaceSelection = true;
            break;
        case AccessibilityTextOperationType::Replace: {
            replaceSelection = true;
            // When applying find and replace activities, we want to match the capitalization of the replaced text,
            // (unless we're replacing with an abbreviation.)
            if (text.length() > 0
                && replacementString.length() > 2
                && replacementString != replacementString.convertToUppercaseWithoutLocale()) {
                if (text[0] == u_toupper(text[0]))
                    replacementString = capitalize(replacementString); // FIXME: Needs to take locale into account to work correctly.
                else
                    replacementString = replacementString.convertToLowercaseWithoutLocale(); // FIXME: Needs locale to work correctly.
            }
            break;
        }
        case AccessibilityTextOperationType::ReplacePreserveCase:
            replaceSelection = true;
            break;
        case AccessibilityTextOperationType::Select:
            break;
        }

        // A bit obvious, but worth noting the API contract for this method is that we should
        // return the replacement string when replacing, but the selected string if not.
        if (replaceSelection) {
            // Insert text instead of replacing when the selection length is zero, because replacements
            // aren't performed correctly in certain edge cases like at the the boundary between nodes
            // separated by spaces <p> foo <i>bar</i>[insert here] baz </p>.
            if (textOperationRange.characterRange.length)
                frame->editor().replaceSelectionWithText(replacementString, Editor::SelectReplacement::Yes, operation.smartReplace == AccessibilityTextOperationSmartReplace::No ? Editor::SmartReplace::No : Editor::SmartReplace::Yes);
            else
                frame->editor().insertText(replacementString, /* triggeringEvent */ nullptr);

            result.append(replacementString);
        } else
            result.append(text);
    }

    return result;
}

String AccessibilityObject::altTextFromAttributeOrStyle() const
{
    const auto& alt = getAttribute(altAttr);
    if (!alt.isNull()) {
        // Note that !isNull() is explicitly chosen over !isEmpty(), as alt="" is a meaningful value
        // and should be respected.
        return alt;
    }

    CheckedPtr style = this->style();
    return style ? style->altFromContent() : nullString();
}

bool AccessibilityObject::isARIAInput(AccessibilityRole ariaRole)
{
    switch (ariaRole) {
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::RadioButton:
    case AccessibilityRole::SearchField:
    case AccessibilityRole::Switch:
    case AccessibilityRole::TextField:
        return true;
    default:
        return false;
    }
}    

bool AccessibilityObject::isARIAControl(AccessibilityRole ariaRole)
{
    if (isARIAInput(ariaRole))
        return true;

    switch (ariaRole) {
    case AccessibilityRole::Button:
    case AccessibilityRole::ComboBox:
    case AccessibilityRole::ListBox:
    case AccessibilityRole::PopUpButton:
    case AccessibilityRole::Slider:
    case AccessibilityRole::TextArea:
    case AccessibilityRole::ToggleButton:
        return true;
    default:
        return false;
    }
}

bool AccessibilityObject::isRangeControl() const
{
    switch (role()) {
    case AccessibilityRole::Meter:
    case AccessibilityRole::ProgressIndicator:
    case AccessibilityRole::Slider:
    case AccessibilityRole::ScrollBar:
    case AccessibilityRole::SpinButton:
        return true;
    case AccessibilityRole::Splitter:
        return canSetFocusAttribute();
    default:
        return false;
    }
}

static IntRect boundsForRects(const LayoutRect& rect1, const LayoutRect& rect2, const SimpleRange& dataRange)
{
    LayoutRect ourRect = rect1;
    ourRect.unite(rect2);

    // If the rectangle spans lines and contains multiple text characters, use the range's bounding box intead.
    if (rect1.maxY() != rect2.maxY() && characterCount(dataRange) > 1) {
        if (auto boundingBox = unionRect(RenderObject::absoluteTextRects(dataRange)); !boundingBox.isEmpty())
            ourRect = boundingBox;
    }

    return snappedIntRect(ourRect);
}

IntRect AccessibilityRenderObject::boundsForVisiblePositionRange(const VisiblePositionRange& visiblePositionRange) const
{
    if (visiblePositionRange.isNull())
        return IntRect();

    // Create a mutable VisiblePositionRange.
    VisiblePositionRange range(visiblePositionRange);
    LayoutRect rect1 = range.start.absoluteCaretBounds();
    LayoutRect rect2 = range.end.absoluteCaretBounds();

    // Readjust for position at the edge of a line. This is to exclude line rect that doesn't need to be accounted in the range bounds
    if (rect2.y() != rect1.y()) {
        VisiblePosition endOfFirstLine = endOfLine(range.start);
        if (range.start == endOfFirstLine) {
            range.start.setAffinity(Affinity::Downstream);
            rect1 = range.start.absoluteCaretBounds();
        }
        if (range.end == endOfFirstLine) {
            range.end.setAffinity(Affinity::Upstream);
            rect2 = range.end.absoluteCaretBounds();
        }
    }

    return boundsForRects(rect1, rect2, *makeSimpleRange(range));
}

IntRect AccessibilityObject::boundsForRange(const SimpleRange& range) const
{
    auto cache = axObjectCache();
    if (!cache)
        return { };

    auto start = cache->startOrEndCharacterOffsetForRange(range, true);
    auto end = cache->startOrEndCharacterOffsetForRange(range, false);

    auto rect1 = cache->absoluteCaretBoundsForCharacterOffset(start);
    auto rect2 = cache->absoluteCaretBoundsForCharacterOffset(end);

    // Readjust for position at the edge of a line. This is to exclude line rect that doesn't need to be accounted in the range bounds.
    if (rect2.y() != rect1.y()) {
        auto endOfFirstLine = cache->endCharacterOffsetOfLine(start);
        if (start.isEqual(endOfFirstLine)) {
            start = cache->nextCharacterOffset(start, false);
            rect1 = cache->absoluteCaretBoundsForCharacterOffset(start);
        }
        if (end.isEqual(endOfFirstLine)) {
            end = cache->previousCharacterOffset(end, false);
            rect2 = cache->absoluteCaretBoundsForCharacterOffset(end);
        }
    }

    return boundsForRects(rect1, rect2, range);
}

IntPoint AccessibilityObject::linkClickPoint()
{
    ASSERT(isLink());
    /* A link bounding rect can contain points that are not part of the link.
     For instance, a link that starts at the end of a line and finishes at the
     beginning of the next line will have a bounding rect that includes the
     entire two lines. In such a case, the middle point of the bounding rect
     may not belong to the link element and thus may not activate the link.
     Hence, return the middle point of the first character in the link if exists.
     */
    if (auto range = simpleRange()) {
        auto start = VisiblePosition { makeContainerOffsetPosition(range->start) };
        auto end = start.next();
        if (contains<ComposedTree>(*range, makeBoundaryPoint(end)))
            return { boundsForRange(*makeSimpleRange(start, end)).center() };
    }
    return clickPointFromElementRect();
}

IntPoint AccessibilityObject::clickPoint()
{
    // Headings are usually much wider than their textual content. If the mid point is used, often it can be wrong.
    if (isHeading()) {
        const auto& children = unignoredChildren();
        if (children.size() == 1)
            return children.first()->clickPoint();
    }

    if (isLink())
        return linkClickPoint();

    // use the default position unless this is an editable web area, in which case we use the selection bounds.
    if (!isWebArea() || !canSetValueAttribute())
        return clickPointFromElementRect();

    return boundsForVisiblePositionRange(selection()).center();
}

IntPoint AccessibilityObject::clickPointFromElementRect() const
{
    return roundedIntPoint(elementRect().center());
}

IntRect AccessibilityObject::boundingBoxForQuads(RenderObject* obj, const Vector<FloatQuad>& quads)
{
    ASSERT(obj);
    if (!obj)
        return IntRect();
    
    FloatRect result;
    for (const auto& quad : quads) {
        FloatRect r = quad.enclosingBoundingBox();
        if (!r.isEmpty()) {
            if (obj->style().hasUsedAppearance())
                obj->theme().inflateRectForControlRenderer(*obj, r);
            result.unite(r);
        }
    }
    return snappedIntRect(LayoutRect(result));
}
    
bool AccessibilityObject::press()
{
    // The presence of the actionElement will confirm whether we should even attempt a press.
    RefPtr actionElem = actionElement();
    if (!actionElem)
        return false;
    if (RefPtr frame = actionElem->document().frame())
        frame->loader().resetMultipleFormSubmissionProtection();
    
    // Hit test at this location to determine if there is a sub-node element that should act
    // as the target of the action.
    RefPtr<Element> hitTestElement;
    RefPtr document = this->document();
    if (document) {
        constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::AccessibilityHitTest };
        HitTestResult hitTestResult { clickPoint() };
        document->hitTest(hitType, hitTestResult);
        if (RefPtr innerNode = hitTestResult.innerNode()) {
            if (RefPtr shadowHost = innerNode->shadowHost())
                hitTestElement = WTFMove(shadowHost);
            else if (RefPtr element = dynamicDowncast<Element>(*innerNode))
                hitTestElement = WTFMove(element);
            else
                hitTestElement = innerNode->parentElement();
        }
    }

    // Prefer the actionElement instead of this node, if the actionElement is inside this node.
    RefPtr pressElement = this->element();
    if (!pressElement || actionElem->isDescendantOf(*pressElement))
        pressElement = WTFMove(actionElem);

    ASSERT(pressElement);
    // Prefer the hit test element, if it is inside the target element.
    if (hitTestElement && hitTestElement->isDescendantOf(*pressElement))
        pressElement = WTFMove(hitTestElement);

    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, document.get());

    bool dispatchedEvent = false;
#if PLATFORM(IOS_FAMILY)
    if (hasTouchEventListener())
        dispatchedEvent = dispatchTouchEvent();
#endif

    return dispatchedEvent || pressElement->accessKeyAction(true) || pressElement->dispatchSimulatedClick(nullptr, SendMouseUpDownEvents);
}
    
bool AccessibilityObject::dispatchTouchEvent()
{
#if ENABLE(IOS_TOUCH_EVENTS)
    if (RefPtr frame = localMainFrame())
        return frame->eventHandler().dispatchSimulatedTouchEvent(clickPoint());
#endif
    return false;
}

LocalFrame* AccessibilityObject::frame() const
{
    Node* node = this->node();
    return node ? node->document().frame() : nullptr;
}

RefPtr<LocalFrame> AccessibilityObject::localMainFrame() const
{
    if (RefPtr page = this->page())
        return page->localMainFrame();
    return nullptr;
}

Document* AccessibilityObject::topDocument() const
{
    if (!document())
        return nullptr;
    return document()->mainFrameDocument();
}

RenderView* AccessibilityObject::topRenderer() const
{
    if (RefPtr topDocument = this->topDocument())
        return topDocument->renderView();
    return nullptr;
}

unsigned AccessibilityObject::ariaLevel() const
{
    return std::max(0, integralAttribute(aria_levelAttr));
}

String AccessibilityObject::language() const
{
    const auto& lang = getAttribute(langAttr);
    if (!lang.isEmpty())
        return lang;

    if (isScrollView() && !parentObject()) {
        // If this is the root, use the content language specified in the meta tag.
        if (RefPtr document = this->document())
            return document->contentLanguage();
    }

    // This object has no language of its own.
    return nullAtom();
}

VisiblePosition AccessibilityObject::visiblePositionForPoint(const IntPoint& point) const
{
    // convert absolute point to view coordinates
    RenderView* renderView = topRenderer();
    if (!renderView)
        return VisiblePosition();

#if PLATFORM(MAC)
    auto* frameView = &renderView->frameView();
#endif

    RefPtr<Node> innerNode;

    // Locate the node containing the point
    // FIXME: Remove this loop and instead add HitTestRequest::Type::AllowVisibleChildFrameContentOnly to the hit test request type.
    LayoutPoint pointResult;
    while (1) {
        LayoutPoint pointToUse;
#if PLATFORM(MAC)
        pointToUse = frameView->screenToContents(point);
#else
        pointToUse = point;
#endif
        constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active };
        HitTestResult result { pointToUse };
        renderView->document().hitTest(hitType, result);
        innerNode = result.innerNode();
        if (!innerNode)
            return VisiblePosition();

        RenderObject* renderer = innerNode->renderer();
        if (!renderer)
            return VisiblePosition();

        pointResult = result.localPoint();

        // done if hit something other than a widget
        RefPtr renderWidget = dynamicDowncast<RenderWidget>(*renderer);
        if (!renderWidget)
            break;

        // descend into widget (FRAME, IFRAME, OBJECT...)
        RefPtr widget = renderWidget->widget();
        RefPtr frameView = dynamicDowncast<LocalFrameView>(widget);
        if (!frameView)
            break;
        RefPtr document = frameView->frame().document();
        if (!document)
            break;

        renderView = document->renderView();
#if PLATFORM(MAC)
        // FIXME: Can this be removed? This seems like a redundant assignment.
        frameView = downcast<LocalFrameView>(widget);
#endif
    }
    
    return innerNode->renderer()->positionForPoint(pointResult, HitTestSource::User, nullptr);
}

VisiblePositionRange AccessibilityObject::visiblePositionRangeForUnorderedPositions(const VisiblePosition& visiblePos1, const VisiblePosition& visiblePos2) const
{
    if (visiblePos1.isNull() || visiblePos2.isNull())
        return VisiblePositionRange();

    // If there's no common tree scope between positions, return early.
    if (!commonTreeScope(visiblePos1.deepEquivalent().deprecatedNode(), visiblePos2.deepEquivalent().deprecatedNode()))
        return VisiblePositionRange();
    
    VisiblePosition startPos;
    VisiblePosition endPos;
    bool alreadyInOrder;

    // upstream is ordered before downstream for the same position
    if (visiblePos1 == visiblePos2 && visiblePos2.affinity() == Affinity::Upstream)
        alreadyInOrder = false;

    // use selection order to see if the positions are in order
    else
        alreadyInOrder = VisibleSelection(visiblePos1, visiblePos2).isBaseFirst();

    if (alreadyInOrder) {
        startPos = visiblePos1;
        endPos = visiblePos2;
    } else {
        startPos = visiblePos2;
        endPos = visiblePos1;
    }

    return { startPos, endPos };
}

static VisiblePosition updateAXLineStartForVisiblePosition(const VisiblePosition& visiblePosition)
{
    // A line in the accessibility sense should include floating objects, such as aligned image, as part of a line.
    // So let's update the position to include that.
    VisiblePosition tempPosition;
    VisiblePosition startPosition = visiblePosition;
    while (true) {
        tempPosition = startPosition.previous();
        if (tempPosition.isNull())
            break;
        Position p = tempPosition.deepEquivalent();
        RenderObject* renderer = p.deprecatedNode()->renderer();
        if (!renderer || (renderer->isRenderBlock() && !p.deprecatedEditingOffset()))
            break;
        if (!RenderedPosition(tempPosition).isNull())
            break;
        startPosition = tempPosition;
    }

    return startPosition;
}

VisiblePositionRange AccessibilityObject::leftLineVisiblePositionRange(const VisiblePosition& visiblePos) const
{
    if (visiblePos.isNull())
        return VisiblePositionRange();

    // make a caret selection for the position before marker position (to make sure
    // we move off of a line start)
    VisiblePosition prevVisiblePos = visiblePos.previous();
    if (prevVisiblePos.isNull())
        return VisiblePositionRange();

    VisiblePosition startPosition = startOfLine(prevVisiblePos);

    // keep searching for a valid line start position.  Unless the VisiblePosition is at the very beginning, there should
    // always be a valid line range.  However, startOfLine will return null for position next to a floating object,
    // since floating object doesn't really belong to any line.
    // This check will reposition the marker before the floating object, to ensure we get a line start.
    if (startPosition.isNull()) {
        while (startPosition.isNull() && prevVisiblePos.isNotNull()) {
            prevVisiblePos = prevVisiblePos.previous();
            startPosition = startOfLine(prevVisiblePos);
        }
    } else
        startPosition = updateAXLineStartForVisiblePosition(startPosition);

    return { startPosition, endOfLine(prevVisiblePos) };
}

VisiblePositionRange AccessibilityObject::rightLineVisiblePositionRange(const VisiblePosition& visiblePos) const
{
    if (visiblePos.isNull())
        return VisiblePositionRange();

    // make sure we move off of a line end
    VisiblePosition nextVisiblePos = visiblePos.next();
    if (nextVisiblePos.isNull())
        return VisiblePositionRange();

    VisiblePosition startPosition = startOfLine(nextVisiblePos);

    // fetch for a valid line start position
    if (startPosition.isNull()) {
        startPosition = visiblePos;
        nextVisiblePos = nextVisiblePos.next();
    } else
        startPosition = updateAXLineStartForVisiblePosition(startPosition);

    VisiblePosition endPosition = endOfLine(nextVisiblePos);

    // as long as the position hasn't reached the end of the doc,  keep searching for a valid line end position
    // Unless the VisiblePosition is at the very end, there should always be a valid line range.  However, endOfLine will
    // return null for position by a floating object, since floating object doesn't really belong to any line.
    // This check will reposition the marker after the floating object, to ensure we get a line end.
    while (endPosition.isNull() && nextVisiblePos.isNotNull()) {
        nextVisiblePos = nextVisiblePos.next();
        endPosition = endOfLine(nextVisiblePos);
    }

    return { startPosition, endPosition };
}

static VisiblePosition startOfStyleRange(const VisiblePosition& visiblePos)
{
    RenderObject* renderer = visiblePos.deepEquivalent().deprecatedNode()->renderer();
    RenderObject* startRenderer = renderer;
    auto* style = &renderer->style();

    // traverse backward by renderer to look for style change
    for (RenderObject* r = renderer->previousInPreOrder(); r; r = r->previousInPreOrder()) {
        // skip non-leaf nodes
        if (r->firstChildSlow())
            continue;

        // stop at style change
        if (&r->style() != style)
            break;

        // remember match
        startRenderer = r;
    }

    return firstPositionInOrBeforeNode(startRenderer->node());
}

static VisiblePosition endOfStyleRange(const VisiblePosition& visiblePos)
{
    RenderObject* renderer = visiblePos.deepEquivalent().deprecatedNode()->renderer();
    RenderObject* endRenderer = renderer;
    const RenderStyle& style = renderer->style();

    // traverse forward by renderer to look for style change
    for (RenderObject* r = renderer->nextInPreOrder(); r; r = r->nextInPreOrder()) {
        // skip non-leaf nodes
        if (r->firstChildSlow())
            continue;

        // stop at style change
        if (&r->style() != &style)
            break;

        // remember match
        endRenderer = r;
    }

    return lastPositionInOrAfterNode(endRenderer->node());
}

VisiblePositionRange AccessibilityObject::styleRangeForPosition(const VisiblePosition& visiblePos) const
{
    if (visiblePos.isNull())
        return { };

    return { startOfStyleRange(visiblePos), endOfStyleRange(visiblePos) };
}

// NOTE: Consider providing this utility method as AX API
VisiblePositionRange AccessibilityObject::visiblePositionRangeForRange(const CharacterRange& range) const
{
    if (range.location + range.length > getLengthForTextRange())
        return { };

    auto startPosition = visiblePositionForIndex(range.location);
    startPosition.setAffinity(Affinity::Downstream);
    return { startPosition, visiblePositionForIndex(range.location + range.length) };
}

std::optional<SimpleRange> AccessibilityObject::rangeForCharacterRange(const CharacterRange& range) const
{
    unsigned textLength = getLengthForTextRange();
    if (range.location + range.length > textLength)
        return std::nullopt;

    // Avoid setting selection to uneditable parent node in FrameSelection::setSelectedRange. See webkit.org/b/206093.
    if (!range.location && !range.length && !textLength)
        return std::nullopt;

    if (auto* cache = axObjectCache()) {
        auto start = cache->characterOffsetForIndex(range.location, this);
        auto end = cache->characterOffsetForIndex(range.location + range.length, this);
        return cache->rangeForUnorderedCharacterOffsets(start, end);
    }
    return std::nullopt;
}

VisiblePositionRange AccessibilityObject::lineRangeForPosition(const VisiblePosition& visiblePosition) const
{
    VisiblePosition startPosition = startOfLine(visiblePosition);
    VisiblePosition endPosition = endOfLine(visiblePosition);

    if (endPosition.isNull() || endPosition < startPosition) {
        // When endOfLine fails to return a plausible result, try nextLineEndPosition, which is more robust, but ensure it doesn't return a result from a subsequent line.
        VisiblePosition nextLineEnd = nextLineEndPosition(startPosition);
        while (!nextLineEnd.isNull() && nextLineEnd > startPosition && !inSameLine(nextLineEnd, startPosition))
            nextLineEnd = nextLineEnd.previous();

        if (!nextLineEnd.isNull())
            endPosition = nextLineEnd;
    }

    return { startPosition, endPosition };
}

#if PLATFORM(MAC)
AXTextMarkerRange AccessibilityObject::selectedTextMarkerRange() const
{
    auto visibleRange = selectedVisiblePositionRange();
    if (visibleRange.isNull())
        return { };
    return { visibleRange };
}
#endif // PLATFORM(MAC)

bool AccessibilityObject::replacedNodeNeedsCharacter(Node& replacedNode)
{
    // we should always be given a rendered node and a replaced node, but be safe
    // replaced nodes are either attachments (widgets) or images
    if (!isRendererReplacedElement(replacedNode.renderer()) || replacedNode.isTextNode())
        return false;

    // create an AX object, but skip it if it is not supposed to be seen
    if (CheckedPtr cache = replacedNode.renderer()->document().axObjectCache()) {
        if (RefPtr axObject = cache->getOrCreate(replacedNode))
            return !axObject->isIgnored();
    }

    return true;
}

#if PLATFORM(COCOA) && ENABLE(MODEL_ELEMENT)
Vector<RetainPtr<id>> AccessibilityObject::modelElementChildren()
{
    RefPtr model = dynamicDowncast<HTMLModelElement>(node());
    if (!model)
        return { };

    return model->accessibilityChildren();
}
#endif

// Finds a RenderListItem parent given a node.
static RenderListItem* renderListItemContainer(Node* node)
{
    for (; node; node = node->parentNode()) {
        if (auto* listItem = dynamicDowncast<RenderListItem>(node->renderer()))
            return listItem;
    }
    return nullptr;
}

// Returns the text representing a list marker taking into account the position of the text in the line of text.
static StringView lineStartListMarkerText(const RenderListItem* listItem, const VisiblePosition& startVisiblePosition, std::optional<StringView> markerText = std::nullopt)
{
    if (!listItem)
        return { };

    if (!markerText)
        markerText = listItem->markerTextWithSuffix();
    if (markerText->isEmpty())
        return { };

    // Only include the list marker if the range includes the line start (where the marker would be), and is in the same line as the marker.
    if (!isStartOfLine(startVisiblePosition) || !inSameLine(startVisiblePosition, firstPositionInNode(listItem->element())))
        return { };
    return *markerText;
}

StringView AccessibilityObject::listMarkerTextForNodeAndPosition(Node* node, Position&& startPosition)
{
    auto* listItem = renderListItemContainer(node);
    if (!listItem)
        return { };
    // Creating a VisiblePosition and determining its relationship to a line of text can be expensive.
    // Thus perform that determination only if we have some text to return.
    auto markerText = listItem->markerTextWithSuffix();
    if (markerText.isEmpty())
        return { };
    return lineStartListMarkerText(listItem, startPosition, markerText);
}

String AccessibilityObject::textContentPrefixFromListMarker() const
{
    // The code below creates a VisiblePosition, which is very expensive. Only do this if there's
    // any chance we're actually associated with a list marker.
    if (!renderListItemContainer(node()))
        return { };

    // Get the attributed string for range (0, 1) and then delete the last character,
    // in order to extract the list marker that was added as a prefix to the text content.
    std::optional<SimpleRange> firstCharacterRange = rangeForCharacterRange({ 0, 1 });
    if (firstCharacterRange) {
        String firstCharacterText = AXTextMarkerRange { firstCharacterRange }.toString();
        if (firstCharacterText.length() > 1)
            return firstCharacterText.left(firstCharacterText.length() - 1);
    }
    return { };
}

bool AccessibilityObject::shouldCacheStringValue() const
{
    if (!isStaticText()) {
        // Quick non-virtual exit path (this only checks m_role — renderer() is virtual).
        return true;
    }

    CheckedPtr renderer = this->renderer();
    if (!renderer || !is<RenderText>(*renderer))
        return true;
    // Only consider RenderTexts for now.

    if (renderer->isAnonymous()) {
        CheckedPtr parent = renderer ? renderer->parent() : nullptr;
        if (is<PseudoElement>(parent ? parent->element() : nullptr)) {
            // RenderTexts descending from pseudo-elements (e.g. ::before) can have alt text that
            // we don't currently handle via text runs, and thus we must cache the string value.
            return true;
        }
    }

    if (CheckedPtr containingBlock = renderer->containingBlock()) {
        // Check for ::first-letter, which would require some special handling to serve off the main-thread
        // that we don't have right now.
        if (containingBlock->style().hasPseudoStyle(PseudoId::FirstLetter))
            return true;
        if (containingBlock->isAnonymous()) {
            containingBlock = containingBlock->containingBlock();
            return containingBlock && containingBlock->style().hasPseudoStyle(PseudoId::FirstLetter);
        }
    }
    // Getting to the end means we can avoid caching string value.
    return false;
}

String AccessibilityObject::stringForVisiblePositionRange(const VisiblePositionRange& visiblePositionRange)
{
    auto range = makeSimpleRange(visiblePositionRange);
    if (!range)
        return { };

    StringBuilder builder;
    TextIterator it = textIteratorIgnoringFullSizeKana(*range);
    for (; !it.atEnd(); it.advance()) {
        // non-zero length means textual node, zero length means replaced node (AKA "attachments" in AX)
        if (it.text().length()) {
            // Add a textual representation for list marker text.
            builder.append(lineStartListMarkerText(renderListItemContainer(it.node()), visiblePositionRange.start));
            it.appendTextToStringBuilder(builder);
        } else {
            // locate the node and starting offset for this replaced range
            if (replacedNodeNeedsCharacter(*it.node()))
                builder.append(objectReplacementCharacter);
        }
    }
    return builder.toString();
}

VisiblePosition AccessibilityObject::nextLineEndPosition(const VisiblePosition& startPosition) const
{
    if (startPosition.isNull())
        return { };

    // Move to the next position to ensure we move off a line end.
    auto nextPosition = startPosition.next();
    if (nextPosition.isNull())
        return { };

    auto lineEndPosition = endOfLine(nextPosition);
    // As long as the position hasn't reached the end of the document, keep searching for a valid line
    // end position. Skip past null positions, as there are cases like when the position is next to a
    // floating object that'll return null for end of line. Also, in certain scenarios, like when one
    // position is editable and the other isn't (e.g. in mixed-contenteditable-visible-character-range-hang.html),
    // we may end up back at the same position we started at. This is never valid, so keep moving forward
    // trying to find the next line end.
    while ((lineEndPosition.isNull() || lineEndPosition == startPosition) && nextPosition.isNotNull()) {
        nextPosition = nextPosition.next();
        lineEndPosition = endOfLine(nextPosition);
    }
    return lineEndPosition;
}

std::optional<VisiblePosition> AccessibilityObject::previousLineStartPositionInternal(const VisiblePosition& visiblePosition) const
{
    if (visiblePosition.isNull())
        return std::nullopt;

    // Make sure we move off of a line start.
    auto previousVisiblePosition = visiblePosition.previous();
    if (previousVisiblePosition.isNull())
        return std::nullopt;

    auto startPosition = startOfLine(previousVisiblePosition);
    // As long as the position hasn't reached the beginning of the document, keep searching for a valid line start position.
    // This avoids returning a null position when we shouldn't, like when a position is next to a floating object.
    if (startPosition.isNull()) {
        while (startPosition.isNull() && previousVisiblePosition.isNotNull()) {
            previousVisiblePosition = previousVisiblePosition.previous();
            startPosition = startOfLine(previousVisiblePosition);
        }
    } else
        startPosition = updateAXLineStartForVisiblePosition(startPosition);

    return startPosition;
}

bool AccessibilityObject::hasRowGroupTag() const
{
    auto elementName = this->elementName();
    return elementName == ElementName::HTML_thead || elementName == ElementName::HTML_tbody || elementName == ElementName::HTML_tfoot;
}

bool AccessibilityObject::isVisited() const
{
    if (!isLink()) {
        // Note that this isLink() check is necessary in addition to the RenderStyle::isLink() check below, as multiple
        // renderers can share the same style, e.g. RenderTexts within a link take their parent's (the link) style.
        return false;
    }

    auto* style = this->style();
    if (!style || !style->isLink())
        return false;
    return style->insideLink() == InsideLink::InsideVisited;
}

// If you call node->hasEditableStyle() since that will return true if an ancestor is editable.
// This only returns true if this is the element that actually has the contentEditable attribute set.
bool AccessibilityObject::hasContentEditableAttributeSet() const
{
    RefPtr element = this->element();
    return element && contentEditableAttributeIsEnabled(*element);
}

bool AccessibilityObject::dependsOnTextUnderElement() const
{
    switch (role()) {
    case AccessibilityRole::PopUpButton:
        // Native popup buttons should not use their descendant's text as a title. That value is retrieved through stringValue().
        if (hasElementName(ElementName::HTML_select))
            break;
        [[fallthrough]];
    case AccessibilityRole::Summary:
    case AccessibilityRole::Button:
    case AccessibilityRole::ToggleButton:
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::ListBoxOption:
#if !PLATFORM(COCOA)
    // macOS does not expect native <li> elements to expose label information, it only expects leaf node elements to do that.
    case AccessibilityRole::ListItem:
#endif
    case AccessibilityRole::MenuItem:
    case AccessibilityRole::MenuItemCheckbox:
    case AccessibilityRole::MenuItemRadio:
    case AccessibilityRole::RadioButton:
    case AccessibilityRole::Switch:
    case AccessibilityRole::Tab:
        return true;
    default:
        break;
    }

    // If it's focusable but it's not content editable or a known control type, then it will appear to
    // the user as a single atomic object, so we should use its text as the default title.
    if (isHeading() || isLink())
        return true;

    return isOutput();
}

bool AccessibilityObject::supportsReadOnly() const
{
    auto role = this->role();
    return role == AccessibilityRole::Checkbox
        || role == AccessibilityRole::ComboBox
        || role == AccessibilityRole::Grid
        || role == AccessibilityRole::GridCell
        || role == AccessibilityRole::ListBox
        || role == AccessibilityRole::MenuItemCheckbox
        || role == AccessibilityRole::MenuItemRadio
        || role == AccessibilityRole::RadioGroup
        || role == AccessibilityRole::SearchField
        || role == AccessibilityRole::Slider
        || role == AccessibilityRole::SpinButton
        || role == AccessibilityRole::Switch
        || role == AccessibilityRole::TextField
        || role == AccessibilityRole::TreeGrid
        || isColumnHeader()
        || isRowHeader()
        || isSecureField();
}

String AccessibilityObject::readOnlyValue() const
{
    if (!hasAttribute(aria_readonlyAttr))
        return ariaRoleAttribute() != AccessibilityRole::Unknown && supportsReadOnly() ? "false"_s : String();

    return getAttribute(aria_readonlyAttr).string().convertToASCIILowercase();
}

bool AccessibilityObject::supportsCheckedState() const
{
    auto role = this->role();
    return isCheckboxOrRadio()
    || role == AccessibilityRole::MenuItemCheckbox
    || role == AccessibilityRole::MenuItemRadio
    || role == AccessibilityRole::Switch
    || isToggleButton();
}

bool AccessibilityObject::supportsAutoComplete() const
{
    return (isComboBox() || isARIATextControl()) && hasAttribute(aria_autocompleteAttr);
}

String AccessibilityObject::explicitAutoCompleteValue() const
{
    const AtomString& autoComplete = getAttribute(aria_autocompleteAttr);
    if (equalLettersIgnoringASCIICase(autoComplete, "inline"_s)
        || equalLettersIgnoringASCIICase(autoComplete, "list"_s)
        || equalLettersIgnoringASCIICase(autoComplete, "both"_s))
        return autoComplete;

    return { };
}

bool AccessibilityObject::contentEditableAttributeIsEnabled(Element& element)
{
    const AtomString& contentEditableValue = element.attributeWithoutSynchronization(contenteditableAttr);
    if (contentEditableValue.isNull())
        return false;

    if (auto* htmlElement = dynamicDowncast<HTMLElement>(&element)) {
        if (htmlElement->isTextControlInnerTextElement())
            return false;
    }

    // All of "true", "plaintext-only", (both case-insensitive) and the empty string count as true for accessibility.
    // This needs to be consistent with contentEditableType(const AtomString&) from HTMLElement.cpp.
    return contentEditableValue.isEmpty() || equalLettersIgnoringASCIICase(contentEditableValue, "true"_s) || equalLettersIgnoringASCIICase(contentEditableValue, "plaintext-only"_s);
}

int AccessibilityObject::lineForPosition(const VisiblePosition& visiblePos) const
{
    if (visiblePos.isNull() || !node())
        return -1;

    // If the position is not in the same editable region as this AX object, return -1.
    RefPtr containerNode = visiblePos.deepEquivalent().containerNode();
    if (!containerNode->isShadowIncludingInclusiveAncestorOf(node()) && !node()->isShadowIncludingInclusiveAncestorOf(containerNode.get()))
        return -1;

    int lineCount = -1;
    VisiblePosition currentVisiblePos = visiblePos;
    VisiblePosition savedVisiblePos;

    // move up until we get to the top
    // FIXME: This only takes us to the top of the rootEditableElement, not the top of the
    // top document.
    do {
        savedVisiblePos = currentVisiblePos;
        currentVisiblePos = previousLinePosition(currentVisiblePos, 0, HasEditableAXRole);
        ++lineCount;
    } while (currentVisiblePos.isNotNull() && !(inSameLine(currentVisiblePos, savedVisiblePos)));

    return lineCount;
}

// NOTE: Consider providing this utility method as AX API
CharacterRange AccessibilityObject::plainTextRangeForVisiblePositionRange(const VisiblePositionRange& positionRange) const
{
    int index1 = index(positionRange.start);
    int index2 = index(positionRange.end);
    if (index1 < 0 || index2 < 0 || index1 > index2)
        return { };

    return CharacterRange(index1, index2 - index1);
}

// The composed character range in the text associated with this accessibility object that
// is specified by the given screen coordinates. This parameterized attribute returns the
// complete range of characters (including surrogate pairs of multi-byte glyphs) at the given
// screen coordinates.
// NOTE: This varies from AppKit when the point is below the last line. AppKit returns an
// an error in that case. We return textControl->text().length(), 1. Does this matter?
CharacterRange AccessibilityObject::characterRangeForPoint(const IntPoint& point) const
{
    int i = index(visiblePositionForPoint(point));
    if (i < 0)
        return { };
    return { static_cast<uint64_t>(i), 1 };
}

// Given a character index, the range of text associated with this accessibility object
// over which the style in effect at that character index applies.
CharacterRange AccessibilityObject::doAXStyleRangeForIndex(unsigned index) const
{
    VisiblePositionRange range = styleRangeForPosition(visiblePositionForIndex(index, false));
    return plainTextRangeForVisiblePositionRange(range);
}

// Given an indexed character, the line number of the text associated with this accessibility
// object that contains the character.
unsigned AccessibilityObject::doAXLineForIndex(unsigned index)
{
    return lineForPosition(visiblePositionForIndex(index, false));
}

void AccessibilityObject::updateBackingStore()
{
    if (!axObjectCache())
        return;
    
    // Updating the layout may delete this object.
    RefPtr<AccessibilityObject> protectedThis(this);
    if (RefPtr document = this->document()) {
        if (!Accessibility::inRenderTreeOrStyleUpdate(*document))
            document->updateLayoutIgnorePendingStylesheets();
    }

    if (auto* cache = axObjectCache())
        cache->performDeferredCacheUpdate(ForceLayout::Yes);

    updateChildrenIfNecessary();
}

const AccessibilityScrollView* AccessibilityObject::ancestorAccessibilityScrollView(bool includeSelf) const
{
    return downcast<AccessibilityScrollView>(Accessibility::findAncestor<AccessibilityObject>(*this, includeSelf, [] (const auto& object) {
        return is<AccessibilityScrollView>(object);
    }));
}

#if PLATFORM(COCOA)
RetainPtr<RemoteAXObjectRef> AccessibilityObject::remoteParent() const
{
    RefPtr document = this->document();
    RefPtr frame = document ? document->frame() : nullptr;
    return frame ? frame->loader().client().accessibilityRemoteObject() : nullptr;
}
#endif

IntPoint AccessibilityObject::remoteFrameOffset() const
{
#if PLATFORM(COCOA)
    RefPtr document = this->document();
    RefPtr frame = document ? document->frame() : nullptr;
    return frame ? frame->loader().client().accessibilityRemoteFrameOffset() : IntPoint();
#else
    return IntPoint();
#endif
}

Document* AccessibilityObject::document() const
{
    RefPtr frameView = documentFrameView();
    if (!frameView)
        return nullptr;

    return frameView->frame().document();
}

RefPtr<Document> AccessibilityObject::protectedDocument() const
{
    return document();
}

Page* AccessibilityObject::page() const
{
    RefPtr document = this->document();
    if (!document)
        return nullptr;
    return document->page();
}

LocalFrameView* AccessibilityObject::documentFrameView() const 
{ 
    RefPtr<const AccessibilityObject> object = this;
    while (object) {
        // Ascend until we find an ancestor with a valid renderer or node, from which we can
        // actually get a frameview.
        if (auto* axRenderObject = dynamicDowncast<AccessibilityRenderObject>(*object)) {
            if (axRenderObject->renderer() || axRenderObject->node()) {
                object = axRenderObject;
                break;
            }
        } else if (auto* axNodeObject = dynamicDowncast<AccessibilityNodeObject>(*object); axNodeObject && axNodeObject->node()) {
            object = axNodeObject;
            break;
        }
        object = object->parentObject();
    }
    return object ? object->documentFrameView() : nullptr;
}

const AccessibilityObject::AccessibilityChildrenVector& AccessibilityObject::children(bool updateChildrenIfNeeded)
{
    if (updateChildrenIfNeeded)
        updateChildrenIfNecessary();

    return m_children;
}

void AccessibilityObject::updateChildrenIfNecessary()
{
    if (!childrenInitialized()) {
        // Enable the cache in case we end up adding a lot of children, we don't want to recompute axIsIgnored each time.
        AXAttributeCacheEnabler enableCache(axObjectCache());
        addChildren();
    }
}
    
void AccessibilityObject::clearChildren()
{
    // Some objects have weak pointers to their parents and those associations need to be detached.
    for (const auto& child : m_children)
        child->detachFromParent();
    
    m_children.clear();
    m_childrenInitialized = false;
}

AccessibilityObject* AccessibilityObject::anchorElementForNode(Node& node)
{
    CheckedPtr renderer = node.renderer();
    if (!renderer)
        return nullptr;

    WeakPtr cache = renderer->document().axObjectCache();
    RefPtr axObject = cache ? cache->getOrCreate(renderer.get()) : nullptr;
    RefPtr anchor = axObject ? axObject->anchorElement() : nullptr;
    return anchor ? cache->getOrCreate(anchor->renderer()) : nullptr;
}

AccessibilityObject* AccessibilityObject::headingElementForNode(Node* node)
{
    if (!node)
        return nullptr;

    RenderObject* renderObject = node->renderer();
    if (!renderObject)
        return nullptr;

    RefPtr axObject = renderObject->document().axObjectCache()->getOrCreate(*renderObject);

    return Accessibility::findAncestor<AccessibilityObject>(*axObject, true, [] (const AccessibilityObject& object) {
        return object.role() == AccessibilityRole::Heading;
    });
}

AXCoreObject::AccessibilityChildrenVector AccessibilityObject::disclosedRows()
{
    AccessibilityChildrenVector result;

    for (const auto& obj : unignoredChildren()) {
        // Add tree items as the rows.
        if (obj->role() == AccessibilityRole::TreeItem)
            result.append(obj);
        // If it's not a tree item, then descend into the group to find more tree items.
        else 
            result.appendVector(obj->ariaTreeRows());
    }

    return result;
}

String AccessibilityObject::localizedActionVerb() const
{
#if !PLATFORM(IOS_FAMILY)
    // FIXME: Need to add verbs for select elements.
    static NeverDestroyed<const String> buttonAction(AXButtonActionVerb());
    static NeverDestroyed<const String> textFieldAction(AXTextFieldActionVerb());
    static NeverDestroyed<const String> radioButtonAction(AXRadioButtonActionVerb());
    static NeverDestroyed<const String> checkedCheckboxAction(AXCheckedCheckboxActionVerb());
    static NeverDestroyed<const String> uncheckedCheckboxAction(AXUncheckedCheckboxActionVerb());
    static NeverDestroyed<const String> linkAction(AXLinkActionVerb());
    static NeverDestroyed<const String> menuListAction(AXMenuListActionVerb());
    static NeverDestroyed<const String> menuListPopupAction(AXMenuListPopupActionVerb());
    static NeverDestroyed<const String> listItemAction(AXListItemActionVerb());

    switch (role()) {
    case AccessibilityRole::Button:
    case AccessibilityRole::ToggleButton:
        return buttonAction;
    case AccessibilityRole::TextField:
    case AccessibilityRole::TextArea:
        return textFieldAction;
    case AccessibilityRole::RadioButton:
        return radioButtonAction;
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::Switch:
        return isChecked() ? checkedCheckboxAction : uncheckedCheckboxAction;
    case AccessibilityRole::Link:
        return linkAction;
    case AccessibilityRole::PopUpButton:
        return menuListAction;
    case AccessibilityRole::MenuListPopup:
        return menuListPopupAction;
    case AccessibilityRole::ListItem:
        return listItemAction;
    default:
        return nullAtom();
    }
#else
    return nullAtom();
#endif
}

String AccessibilityObject::actionVerb() const
{
#if !PLATFORM(IOS_FAMILY)
    // FIXME: Need to add verbs for select elements.
    switch (role()) {
    case AccessibilityRole::Button:
    case AccessibilityRole::ToggleButton:
        return "press"_s;
    case AccessibilityRole::TextField:
    case AccessibilityRole::TextArea:
        return "activate"_s;
    case AccessibilityRole::RadioButton:
        return "select"_s;
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::Switch:
        return isChecked() ? "uncheck"_s : "check"_s;
    case AccessibilityRole::Link:
        return "jump"_s;
    case AccessibilityRole::PopUpButton:
    case AccessibilityRole::MenuListPopup:
    case AccessibilityRole::ListItem:
        return "select"_s;
    default:
        break;
    }
#endif
    return { };
}

bool AccessibilityObject::ariaIsMultiline() const
{
    return equalLettersIgnoringASCIICase(getAttribute(aria_multilineAttr), "true"_s);
}

String AccessibilityObject::explicitInvalidStatus() const
{
    static NeverDestroyed<String> grammarValue = "grammar"_s;
    static NeverDestroyed<String> falseValue = "false"_s;
    static NeverDestroyed<String> spellingValue = "spelling"_s;
    static NeverDestroyed<String> trueValue = "true"_s;
    static NeverDestroyed<String> undefinedValue = "undefined"_s;

    // aria-invalid can return false (default), grammar, spelling, or true.
    auto ariaInvalid = getAttributeTrimmed(aria_invalidAttr);

    if (ariaInvalid.isEmpty()) {
        auto* htmlElement = dynamicDowncast<HTMLElement>(this->node());
        if (RefPtr validatedFormListedElement = htmlElement ? htmlElement->asValidatedFormListedElement() : nullptr) {
            // "willValidate" is true if the element is able to be validated.
            if (validatedFormListedElement->willValidate() && !validatedFormListedElement->isValidFormControlElement())
                return trueValue;
        }
        return { };
    }
    
    // If "false", "undefined" [sic, string value], empty, or missing, return "false".
    if (ariaInvalid == falseValue || ariaInvalid == undefinedValue)
        return falseValue;
    // Besides true/false/undefined, the only tokens defined by WAI-ARIA 1.0...
    // ...for @aria-invalid are "grammar" and "spelling".
    if (ariaInvalid == grammarValue)
        return grammarValue;
    if (ariaInvalid == spellingValue)
        return spellingValue;
    // Any other non empty string should be treated as "true".
    return trueValue;
}

bool AccessibilityObject::supportsCurrent() const
{
    return hasAttribute(aria_currentAttr);
}
 
AccessibilityCurrentState AccessibilityObject::currentState() const
{
    // aria-current can return false (default), true, page, step, location, date or time.
    auto currentStateValue = getAttributeTrimmed(aria_currentAttr);

    // If "false", empty, or missing, return false state.
    if (currentStateValue.isEmpty() || currentStateValue == "false"_s)
        return AccessibilityCurrentState::False;
    
    if (currentStateValue == "page"_s)
        return AccessibilityCurrentState::Page;
    if (currentStateValue == "step"_s)
        return AccessibilityCurrentState::Step;
    if (currentStateValue == "location"_s)
        return AccessibilityCurrentState::Location;
    if (currentStateValue == "date"_s)
        return AccessibilityCurrentState::Date;
    if (currentStateValue == "time"_s)
        return AccessibilityCurrentState::Time;
    
    // Any value not included in the list of allowed values should be treated as "true".
    return AccessibilityCurrentState::True;
}

bool AccessibilityObject::isModalDescendant(Node& modalNode) const
{
    RefPtr node = this->node();
    // ARIA 1.1 aria-modal, indicates whether an element is modal when displayed.
    // For the decendants of the modal object, they should also be considered as aria-modal=true.
    // Determine descendancy by iterating the composed tree which inherently accounts for shadow roots and slots.
    for (RefPtr ancestor = node.get(); ancestor; ancestor = ancestor->parentInComposedTree()) {
        if (ancestor.get() == &modalNode)
            return true;
    }
    return false;
}

bool AccessibilityObject::isModalNode() const
{
    if (AXObjectCache* cache = axObjectCache())
        return node() && cache->modalNode() == node();

    return false;
}


static RenderObject* nearestRendererFromNode(Node& node)
{
    CheckedPtr renderer = node.renderer();
    for (RefPtr ancestor = &node; ancestor && !renderer; ancestor = composedParentIgnoringDocumentFragments(*ancestor))
        renderer = ancestor->renderer();

    return renderer.get();
}

static int zIndexFromRenderer(RenderObject* renderer)
{
    for (CheckedPtr layer = renderer->enclosingLayer(); layer; layer = layer->parent()) {
        if (int zIndex = layer->zIndex())
            return zIndex;
    }

    return 0;
}

bool AccessibilityObject::ignoredFromModalPresence() const
{
    // We shouldn't ignore the top node.
    if (!node() || !node()->parentNode())
        return false;
    
    AXObjectCache* cache = axObjectCache();
    if (!cache)
        return false;
    
    // modalNode is the current displayed modal dialog.
    RefPtr modalNode = cache->modalNode();
    if (!modalNode)
        return false;
    
    // We only want to ignore the objects within the same frame as the modal dialog.
    if (modalNode->document().frame() != this->frame())
        return false;
    
    // Some objects might be outside of a modal, but are linked to elements inside of it. Don't ignore those.
    for (RefPtr ancestor = this; ancestor; ancestor = ancestor->parentObject()) {
        for (auto& controller : ancestor->controllers()) {
            if (downcast<AccessibilityObject>(controller)->isModalDescendant(*modalNode))
                return false;
        }

        for (auto& activeDescendant : ancestor->activeDescendantOfObjects()) {
            if (downcast<AccessibilityObject>(activeDescendant)->isModalDescendant(*modalNode))
                return false;
        }
    }

    // If this element has a higher z-index than the active modal, also don't ignore it.
    if (CheckedPtr renderer = this->rendererOrNearestAncestor()) {
        if (CheckedPtr modalRenderer = nearestRendererFromNode(*modalNode)) {
            int thisZIndex = zIndexFromRenderer(renderer.get());
            int modalZIndex = zIndexFromRenderer(modalRenderer.get());
            if (thisZIndex > modalZIndex)
                return false;
        }
    }

    return !isModalDescendant(*modalNode);
}

bool AccessibilityObject::hasElementName(ElementName name) const
{
    return elementName() == name;
}

bool AccessibilityObject::hasAttribute(const QualifiedName& attribute) const
{
    RefPtr element = this->element();
    if (!element)
        return false;

    if (element->hasAttributeWithoutSynchronization(attribute))
        return true;

    if (auto* defaultARIA = element->customElementDefaultARIAIfExists()) {
        // We do not want to use CustomElementDefaultARIA::hasAttribute here, as it returns true
        // even if the author has set the attribute to null (e.g. this.internals.ariaValueNow = null),
        // which should be treated the same as removing the attribute.
        return !defaultARIA->valueForAttribute(*element, attribute).isNull();
    }

    return false;
}

const AtomString& AccessibilityObject::getAttribute(const QualifiedName& attribute) const
{
    RefPtr element = this->element();
    return element ? element->attributeWithDefaultARIA(attribute) : nullAtom();
}

String AccessibilityObject::getAttributeTrimmed(const QualifiedName& attribute) const
{
    const auto& rawValue = getAttribute(attribute);
    if (rawValue.isEmpty())
        return { };
    auto value = rawValue.string();
    return value.trim(isASCIIWhitespace).simplifyWhiteSpace(isASCIIWhitespace);
}

String AccessibilityObject::nameAttribute() const
{
    return getAttribute(nameAttr);
}

int AccessibilityObject::integralAttribute(const QualifiedName& attributeName) const
{
    return parseHTMLInteger(getAttribute(attributeName)).value_or(0);
}

bool AccessibilityObject::replaceTextInRange(const String& replacementString, const CharacterRange& range)
{
    // If this is being called on the web area, redirect it to be on the body, which will have a renderer associated with it.
    if (RefPtr document = dynamicDowncast<Document>(node())) {
        if (RefPtr bodyObject = axObjectCache()->getOrCreate(document->body()))
            return bodyObject->replaceTextInRange(replacementString, range);
        return false;
    }

    // FIXME: This checks node() is an Element, but below we assume that means renderer()->node() is an element.
    if (!renderer() || !is<Element>(node()))
        return false;

    Ref element = downcast<Element>(*renderer()->node());

    // We should use the editor's insertText to mimic typing into the field.
    // Also only do this when the field is in editing mode.
    Ref frame = renderer()->frame();
    if (element->shouldUseInputMethod()) {
        frame->selection().setSelectedRange(rangeForCharacterRange(range), Affinity::Downstream, FrameSelection::ShouldCloseTyping::Yes);
        frame->editor().replaceSelectionWithText(replacementString, Editor::SelectReplacement::No, Editor::SmartReplace::No);
        return true;
    }
    
    if (RefPtr input = dynamicDowncast<HTMLInputElement>(element)) {
        input->setRangeText(replacementString, range.location, range.length, emptyString());
        return true;
    }
    if (RefPtr textarea = dynamicDowncast<HTMLTextAreaElement>(element)) {
        textarea->setRangeText(replacementString, range.location, range.length, emptyString());
        return true;
    }

    return false;
}

bool AccessibilityObject::insertText(const String& text)
{
    AXTRACE(makeString("AccessibilityObject::insertText text = "_s, text));

    if (!renderer())
        return false;

    RefPtr element = dynamicDowncast<Element>(node());
    if (!element)
        return false;

    // Only try to insert text if the field is in editing mode (excluding secure fields, which we do still want to try to insert into).
    if (!isSecureField() && !element->shouldUseInputMethod())
        return false;

    // Use Editor::insertText to mimic typing into the field.
    Ref editor = renderer()->frame().editor();
    return editor->insertText(text, nullptr);
}

using ARIARoleMap = HashMap<String, AccessibilityRole, ASCIICaseInsensitiveHash>;
using ARIAReverseRoleMap = HashMap<AccessibilityRole, String, DefaultHash<int>, WTF::UnsignedWithZeroKeyHashTraits<int>>;

static ARIARoleMap* gAriaRoleMap = nullptr;
static ARIAReverseRoleMap* gAriaReverseRoleMap = nullptr;

struct RoleEntry {
    String ariaRole;
    AccessibilityRole webcoreRole;
};

static void initializeRoleMap()
{
    if (gAriaRoleMap)
        return;
    ASSERT(!gAriaReverseRoleMap);

    const std::array roles {
        RoleEntry { "alert"_s, AccessibilityRole::ApplicationAlert },
        RoleEntry { "alertdialog"_s, AccessibilityRole::ApplicationAlertDialog },
        RoleEntry { "application"_s, AccessibilityRole::WebApplication },
        RoleEntry { "article"_s, AccessibilityRole::DocumentArticle },
        RoleEntry { "banner"_s, AccessibilityRole::LandmarkBanner },
        RoleEntry { "blockquote"_s, AccessibilityRole::Blockquote },
        RoleEntry { "button"_s, AccessibilityRole::Button },
        RoleEntry { "caption"_s, AccessibilityRole::Caption },
        RoleEntry { "code"_s, AccessibilityRole::Code },
        RoleEntry { "checkbox"_s, AccessibilityRole::Checkbox },
        RoleEntry { "complementary"_s, AccessibilityRole::LandmarkComplementary },
        RoleEntry { "contentinfo"_s, AccessibilityRole::LandmarkContentInfo },
        RoleEntry { "deletion"_s, AccessibilityRole::Deletion },
        RoleEntry { "dialog"_s, AccessibilityRole::ApplicationDialog },
        RoleEntry { "directory"_s, AccessibilityRole::Directory },
        // The 'doc-*' roles are defined the ARIA DPUB mobile: https://www.w3.org/TR/dpub-aam-1.0
        // Editor's draft is currently at https://w3c.github.io/dpub-aam
        RoleEntry { "doc-abstract"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-acknowledgments"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-afterword"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-appendix"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-backlink"_s, AccessibilityRole::Link },
        RoleEntry { "doc-biblioentry"_s, AccessibilityRole::ListItem },
        RoleEntry { "doc-bibliography"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-biblioref"_s, AccessibilityRole::Link },
        RoleEntry { "doc-chapter"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-colophon"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-conclusion"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-cover"_s, AccessibilityRole::Image },
        RoleEntry { "doc-credit"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-credits"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-dedication"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-endnote"_s, AccessibilityRole::ListItem },
        RoleEntry { "doc-endnotes"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-epigraph"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-epilogue"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-errata"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-example"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-footnote"_s, AccessibilityRole::Footnote },
        RoleEntry { "doc-foreword"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-glossary"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-glossref"_s, AccessibilityRole::Link },
        RoleEntry { "doc-index"_s, AccessibilityRole::LandmarkNavigation },
        RoleEntry { "doc-introduction"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-noteref"_s, AccessibilityRole::Link },
        RoleEntry { "doc-notice"_s, AccessibilityRole::DocumentNote },
        RoleEntry { "doc-pagebreak"_s, AccessibilityRole::Splitter },
        RoleEntry { "doc-pagelist"_s, AccessibilityRole::LandmarkNavigation },
        RoleEntry { "doc-part"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-preface"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-prologue"_s, AccessibilityRole::LandmarkDocRegion },
        RoleEntry { "doc-pullquote"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-qna"_s, AccessibilityRole::TextGroup },
        RoleEntry { "doc-subtitle"_s, AccessibilityRole::Heading },
        RoleEntry { "doc-tip"_s, AccessibilityRole::DocumentNote },
        RoleEntry { "doc-toc"_s, AccessibilityRole::LandmarkNavigation },
        RoleEntry { "emphasis"_s, AccessibilityRole::Emphasis },
        RoleEntry { "figure"_s, AccessibilityRole::Figure },
        RoleEntry { "generic"_s, AccessibilityRole::Generic },
        // The mappings for 'graphics-*' roles are defined in this spec: https://w3c.github.io/graphics-aam/
        RoleEntry { "graphics-document"_s, AccessibilityRole::GraphicsDocument },
        RoleEntry { "graphics-object"_s, AccessibilityRole::GraphicsObject },
        RoleEntry { "graphics-symbol"_s, AccessibilityRole::GraphicsSymbol },
        RoleEntry { "grid"_s, AccessibilityRole::Grid },
        RoleEntry { "gridcell"_s, AccessibilityRole::GridCell },
        RoleEntry { "table"_s, AccessibilityRole::Table },
        RoleEntry { "cell"_s, AccessibilityRole::Cell },
        RoleEntry { "columnheader"_s, AccessibilityRole::ColumnHeader },
        RoleEntry { "combobox"_s, AccessibilityRole::ComboBox },
        RoleEntry { "definition"_s, AccessibilityRole::Definition },
        RoleEntry { "document"_s, AccessibilityRole::Document },
        RoleEntry { "feed"_s, AccessibilityRole::Feed },
        RoleEntry { "form"_s, AccessibilityRole::Form },
        RoleEntry { "rowheader"_s, AccessibilityRole::RowHeader },
        RoleEntry { "group"_s, AccessibilityRole::Group },
        RoleEntry { "heading"_s, AccessibilityRole::Heading },
        // The "image" role is synonymous with the "img" role. https://w3c.github.io/aria/#image
        RoleEntry { "image"_s, AccessibilityRole::Image },
        RoleEntry { "img"_s, AccessibilityRole::Image },
        RoleEntry { "insertion"_s, AccessibilityRole::Insertion },
        RoleEntry { "link"_s, AccessibilityRole::Link },
        RoleEntry { "list"_s, AccessibilityRole::List },
        RoleEntry { "listitem"_s, AccessibilityRole::ListItem },
        RoleEntry { "listbox"_s, AccessibilityRole::ListBox },
        RoleEntry { "log"_s, AccessibilityRole::ApplicationLog },
        RoleEntry { "main"_s, AccessibilityRole::LandmarkMain },
        RoleEntry { "marquee"_s, AccessibilityRole::ApplicationMarquee },
        RoleEntry { "math"_s, AccessibilityRole::DocumentMath },
        RoleEntry { "mark"_s, AccessibilityRole::Mark },
        RoleEntry { "menu"_s, AccessibilityRole::Menu },
        RoleEntry { "menubar"_s, AccessibilityRole::MenuBar },
        RoleEntry { "menuitem"_s, AccessibilityRole::MenuItem },
        RoleEntry { "menuitemcheckbox"_s, AccessibilityRole::MenuItemCheckbox },
        RoleEntry { "menuitemradio"_s, AccessibilityRole::MenuItemRadio },
        RoleEntry { "meter"_s, AccessibilityRole::Meter },
        RoleEntry { "none"_s, AccessibilityRole::Presentational },
        RoleEntry { "note"_s, AccessibilityRole::DocumentNote },
        RoleEntry { "navigation"_s, AccessibilityRole::LandmarkNavigation },
        RoleEntry { "option"_s, AccessibilityRole::ListBoxOption },
        RoleEntry { "paragraph"_s, AccessibilityRole::Paragraph },
        RoleEntry { "presentation"_s, AccessibilityRole::Presentational },
        RoleEntry { "progressbar"_s, AccessibilityRole::ProgressIndicator },
        RoleEntry { "radio"_s, AccessibilityRole::RadioButton },
        RoleEntry { "radiogroup"_s, AccessibilityRole::RadioGroup },
        RoleEntry { "region"_s, AccessibilityRole::LandmarkRegion },
        RoleEntry { "row"_s, AccessibilityRole::Row },
        RoleEntry { "rowgroup"_s, AccessibilityRole::RowGroup },
        RoleEntry { "scrollbar"_s, AccessibilityRole::ScrollBar },
        RoleEntry { "search"_s, AccessibilityRole::LandmarkSearch },
        RoleEntry { "searchbox"_s, AccessibilityRole::SearchField },
        RoleEntry { "sectionfooter"_s, AccessibilityRole::SectionFooter },
        RoleEntry { "sectionheader"_s, AccessibilityRole::SectionHeader },
        RoleEntry { "separator"_s, AccessibilityRole::Splitter },
        RoleEntry { "slider"_s, AccessibilityRole::Slider },
        RoleEntry { "spinbutton"_s, AccessibilityRole::SpinButton },
        RoleEntry { "status"_s, AccessibilityRole::ApplicationStatus },
        RoleEntry { "subscript"_s, AccessibilityRole::Subscript },
        RoleEntry { "suggestion"_s, AccessibilityRole::Suggestion },
        RoleEntry { "superscript"_s, AccessibilityRole::Superscript },
        RoleEntry { "strong"_s, AccessibilityRole::Strong },
        RoleEntry { "switch"_s, AccessibilityRole::Switch },
        RoleEntry { "tab"_s, AccessibilityRole::Tab },
        RoleEntry { "tablist"_s, AccessibilityRole::TabList },
        RoleEntry { "tabpanel"_s, AccessibilityRole::TabPanel },
        RoleEntry { "text"_s, AccessibilityRole::StaticText },
        RoleEntry { "textbox"_s, AccessibilityRole::TextField },
        RoleEntry { "term"_s, AccessibilityRole::Term },
        RoleEntry { "time"_s, AccessibilityRole::Time },
        RoleEntry { "timer"_s, AccessibilityRole::ApplicationTimer },
        RoleEntry { "toolbar"_s, AccessibilityRole::Toolbar },
        RoleEntry { "tooltip"_s, AccessibilityRole::UserInterfaceTooltip },
        RoleEntry { "tree"_s, AccessibilityRole::Tree },
        RoleEntry { "treegrid"_s, AccessibilityRole::TreeGrid },
        RoleEntry { "treeitem"_s, AccessibilityRole::TreeItem }
    };

    gAriaRoleMap = new ARIARoleMap;
    gAriaReverseRoleMap = new ARIAReverseRoleMap;
    size_t roleLength = std::size(roles);
    for (size_t i = 0; i < roleLength; ++i) {
        gAriaRoleMap->set(roles[i].ariaRole, roles[i].webcoreRole);
        gAriaReverseRoleMap->set(enumToUnderlyingType(roles[i].webcoreRole), roles[i].ariaRole);
    }

    // Create specific synonyms for the computedRole which is used in WPT tests and the accessibility inspector.
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::DateTime), "textbox"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::TextArea), "textbox"_s);

    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::DescriptionListDetail), "definition"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::DescriptionListTerm), "term"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::Details), "group"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::Image), "image"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::ListBoxOption), "option"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::MenuListOption), "option"_s);
    gAriaReverseRoleMap->set(enumToUnderlyingType(AccessibilityRole::Presentational), "none"_s);
}

static ARIARoleMap& ariaRoleMap()
{
    initializeRoleMap();
    return *gAriaRoleMap;
}

static ARIAReverseRoleMap& reverseAriaRoleMap()
{
    initializeRoleMap();
    return *gAriaReverseRoleMap;
}

AccessibilityRole AccessibilityObject::ariaRoleToWebCoreRole(const String& value)
{
    return ariaRoleToWebCoreRole(value, [] (const AccessibilityRole&) {
        return false;
    });
}

AccessibilityRole AccessibilityObject::ariaRoleToWebCoreRole(const String& value, NOESCAPE const Function<bool(const AccessibilityRole&)>& skipRole)
{
    if (value.isNull() || value.isEmpty())
        return AccessibilityRole::Unknown;
    auto simplifiedValue = value.simplifyWhiteSpace(isASCIIWhitespace);
    for (auto roleName : StringView(simplifiedValue).split(' ')) {
        AccessibilityRole role = ariaRoleMap().get<ASCIICaseInsensitiveStringViewHashTranslator>(roleName);
        if (skipRole(role))
            continue;

        if (enumToUnderlyingType(role))
            return role;
    }
    return AccessibilityRole::Unknown;
}

String AccessibilityObject::computedRoleString() const
{
    // FIXME: Need a few special cases that aren't in the RoleMap: option, etc. http://webkit.org/b/128296
    auto role = this->role();

    if (role == AccessibilityRole::Image && isIgnored())
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Presentational));

    // We do compute a role string for block elements with author-provided roles.
    if (ariaRoleAttribute() == AccessibilityRole::TextGroup
        || role == AccessibilityRole::Footnote
        || role == AccessibilityRole::GraphicsObject)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Group));

    // We do not compute a role string for generic block elements with user-agent assigned roles.
    if (role == AccessibilityRole::TextGroup)
        return emptyString();

    if (role == AccessibilityRole::GraphicsDocument)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Document));

    if (role == AccessibilityRole::GraphicsSymbol)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Image));

    if (role == AccessibilityRole::HorizontalRule)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Splitter));

    if (role == AccessibilityRole::PopUpButton || role == AccessibilityRole::ToggleButton)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::Button));

    if (role == AccessibilityRole::LandmarkDocRegion)
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::LandmarkRegion));

    if (isColumnHeader())
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::ColumnHeader));

    if (isRowHeader())
        return reverseAriaRoleMap().get(enumToUnderlyingType(AccessibilityRole::RowHeader));

    return reverseAriaRoleMap().get(enumToUnderlyingType(role));
}

void AccessibilityObject::updateRole()
{
    auto previousRole = m_role;
    m_role = determineAccessibilityRole();
    if (previousRole != m_role) {
        if (auto* cache = axObjectCache())
            cache->handleRoleChanged(*this, previousRole);
    }
}

SRGBA<uint8_t> AccessibilityObject::colorValue() const
{
    return Color::black;
}

#if !PLATFORM(MAC)
String AccessibilityObject::subrolePlatformString() const
{
    return String();
}
#endif

String AccessibilityObject::embeddedImageDescription() const
{
    CheckedPtr renderImage = dynamicDowncast<RenderImage>(renderer());
    if (!renderImage)
        return { };

    return renderImage->accessibilityDescription();
}

String AccessibilityObject::datetimeAttributeValue() const
{
    return getAttribute(datetimeAttr);
}
    
String AccessibilityObject::linkRelValue() const
{
    return getAttribute(relAttr);
}

bool AccessibilityObject::isLoaded() const
{
    RefPtr document = this->document();
    return document && !document->parser();
}

bool AccessibilityObject::isInlineText() const
{
    return is<RenderInline>(renderer());
}

bool AccessibilityObject::supportsKeyShortcuts() const
{
    return hasAttribute(aria_keyshortcutsAttr);
}

String AccessibilityObject::keyShortcuts() const
{
    return getAttribute(aria_keyshortcutsAttr);
}

Element* AccessibilityObject::element() const
{
    return dynamicDowncast<Element>(node());
}

RenderObject* AccessibilityObject::rendererOrNearestAncestor() const
{
    RefPtr node = this->node();
    return node ? nearestRendererFromNode(*node) : nullptr;
}

const RenderStyle* AccessibilityObject::style() const
{
    if (auto* renderer = this->renderer()) {
        if (auto* renderText = dynamicDowncast<RenderText>(*renderer)) {
            // Trying to get the style from a RenderText that has no parent (e.g. because it hasn't been
            // set yet, or it was destroyed as part of an in-progress render-tree update) will cause a
            // crash because RenderTexts get their style from their parent.
            return renderText->parent() ? &renderText->style() : nullptr;
        }
        return &renderer->style();
    }

    RefPtr element = this->element();
    if (!element)
        return nullptr;
    // We cannot resolve style (as computedStyle() does) if we are downstream of an existing render tree
    // update. Otherwise, a RELEASE_ASSERT preventing re-entrancy will be hit inside RenderTreeBuilder.
    return RenderTreeBuilder::current() ? element->existingComputedStyle() : element->computedStyle();
}

bool AccessibilityObject::isValueAutofillAvailable() const
{
    if (!isNativeTextControl())
        return false;

    RefPtr input = dynamicDowncast<HTMLInputElement>(node());
    return input && (input->autofillAvailable() || input->autofillButtonType() != AutoFillButtonType::None);
}

AutoFillButtonType AccessibilityObject::valueAutofillButtonType() const
{
    if (!isValueAutofillAvailable())
        return AutoFillButtonType::None;
    
    return downcast<HTMLInputElement>(*this->node()).autofillButtonType();
}

bool AccessibilityObject::isSelected() const
{
    if (!renderer() && !node())
        return false;

    if (equalLettersIgnoringASCIICase(getAttribute(aria_selectedAttr), "true"_s))
        return true;

    if (isTabItem() && isTabItemSelected())
        return true;

    // Menu items are considered selectable by assistive technologies
    if (isMenuItem()) {
        if (isFocused())
            return true;
        WeakPtr parent = parentObjectUnignored();
        return parent && parent->activeDescendant() == this;
    }

    return false;
}

bool AccessibilityObject::isTabItemSelected() const
{
    if (!isTabItem() || (!renderer() && !node()))
        return false;

    WeakPtr node = this->node();
    if (!node || !node->isElementNode())
        return false;

    // The ARIA spec says a tab item can also be selected if it is aria-labeled by a tabpanel
    // that has keyboard focus inside of it, or if a tabpanel in its aria-controls list has KB
    // focus inside of it.
    RefPtr focusedElement = focusedUIElement();
    if (!focusedElement)
        return false;

    auto* cache = axObjectCache();
    if (!cache)
        return false;

    auto elements = elementsFromAttribute(aria_controlsAttr);
    for (auto& element : elements) {
        RefPtr tabPanel = cache->getOrCreate(element.ptr());

        // A tab item should only control tab panels.
        if (!tabPanel || tabPanel->role() != AccessibilityRole::TabPanel)
            continue;

        RefPtr checkFocusElement = focusedElement;
        // Check if the focused element is a descendant of the element controlled by the tab item.
        while (checkFocusElement) {
            if (tabPanel == checkFocusElement)
                return true;
            checkFocusElement = checkFocusElement->parentObject();
        }
    }
    return false;
}

unsigned AccessibilityObject::textLength() const
{
    ASSERT(isTextControl());
    return text().length();
}

std::optional<String> AccessibilityObject::textContent() const
{
    if (!hasTextContent())
        return std::nullopt;

    std::optional<SimpleRange> range;
    if (isTextControl())
        range = rangeForCharacterRange({ 0, text().length() });
    else
        range = simpleRange();
    if (range)
        return AXTextMarkerRange { range }.toString();
    return std::nullopt;
}

const String AccessibilityObject::placeholderValue() const
{
    const AtomString& placeholder = getAttribute(placeholderAttr);
    if (!placeholder.isEmpty())
        return placeholder;
    
    const AtomString& ariaPlaceholder = getAttribute(aria_placeholderAttr);
    if (!ariaPlaceholder.isEmpty())
        return ariaPlaceholder;
    
    return nullAtom();
}
    
bool AccessibilityObject::supportsARIAAttributes() const
{
    // This returns whether the element supports any global ARIA attributes.
    return supportsLiveRegion()
        || supportsDragging()
        || supportsDropping()
        || supportsARIAOwns()
        || hasAttribute(aria_atomicAttr)
        || hasAttribute(aria_busyAttr)
        || hasAttribute(aria_controlsAttr)
        || hasAttribute(aria_currentAttr)
        || hasAttribute(aria_describedbyAttr)
        || hasAttribute(aria_detailsAttr)
        || hasAttribute(aria_disabledAttr)
        || hasAttribute(aria_errormessageAttr)
        || hasAttribute(aria_flowtoAttr)
        || hasAttribute(aria_haspopupAttr)
        || hasAttribute(aria_invalidAttr)
        || hasAttribute(aria_labelAttr)
        || hasAttribute(aria_labelledbyAttr)
        || hasAttribute(aria_relevantAttr);
}
    
AccessibilityObject* AccessibilityObject::elementAccessibilityHitTest(const IntPoint& point) const
{
    // Send the hit test back into the sub-frame if necessary.
    if (isAttachment()) {
        Widget* widget = widgetForAttachmentView();
        // Normalize the point for the widget's bounds.
        if (widget && widget->isLocalFrameView()) {
            if (CheckedPtr cache = axObjectCache())
                return cache->getOrCreate(*widget)->accessibilityHitTest(IntPoint(point - widget->frameRect().location()));
        }

        if (widget && widget->isRemoteFrameView()) {
            if (CheckedPtr cache = axObjectCache()) {
                if (RefPtr remoteHostWidget = cache->getOrCreate(*widget)) {
                    remoteHostWidget->updateChildrenIfNecessary();
                    RefPtr scrollView = dynamicDowncast<AccessibilityScrollView>(*remoteHostWidget);
                    return scrollView ? scrollView->remoteFrame().get() : nullptr;
                }
            }
        }
    }
    
    // Check if there are any mock elements that need to be handled.
    for (const auto& child : const_cast<AccessibilityObject*>(this)->unignoredChildren(/* updateChildrenIfNeeded */ false)) {
        if (RefPtr mockChild = dynamicDowncast<AccessibilityMockObject>(child.get()); mockChild && mockChild->elementRect().contains(point))
            return mockChild->elementAccessibilityHitTest(point);
    }

    return const_cast<AccessibilityObject*>(this);
}
    
CommandType AccessibilityObject::commandType() const
{
    return CommandType::Invalid;
}

AccessibilityObject* AccessibilityObject::focusedUIElement() const
{
    RefPtr page = this->page();
    auto* axObjectCache = this->axObjectCache();
    return page && axObjectCache ? axObjectCache->focusedObjectForPage(page.get()) : nullptr;
}

void AccessibilityObject::setSelectedRows(AccessibilityChildrenVector&& selectedRows)
{
    // Setting selected only makes sense in trees and tables (and tree-tables).
    auto role = this->role();
    if (role != AccessibilityRole::Tree && role != AccessibilityRole::TreeGrid && role != AccessibilityRole::Table && role != AccessibilityRole::Grid)
        return;

    bool isMulti = isMultiSelectable();
    for (const auto& selectedRow : selectedRows) {
        // FIXME: At the time of writing, setSelected is only implemented for AccessibilityListBoxOption and AccessibilityMenuListOption which are unlikely to be "rows", so this function probably isn't doing anything useful.
        selectedRow->setSelected(true);
        if (isMulti)
            break;
    }
}

void AccessibilityObject::setFocused(bool focus)
{
    if (focus) {
        // Ensure that the view is focused and active, otherwise, any attempt to set focus to an object inside it will fail.
        RefPtr frame = document() ? document()->frame() : nullptr;
        if (frame && frame->selection().isFocusedAndActive())
            return; // Nothing to do, already focused and active.

        RefPtr page = document() ? document()->page() : nullptr;
        if (!page)
            return;

        page->chrome().client().focus();
        // Reset the page pointer in case ChromeClient::focus() caused a side effect that invalidated our old one.
        page = document() ? document()->page() : nullptr;
        if (!page)
            return;

#if PLATFORM(IOS_FAMILY)
        // Mark the page as focused so the focus ring can be drawn immediately. The page is also marked
        // as focused as part assistiveTechnologyMakeFirstResponder, but that requires some back-and-forth
        // IPC between the web and UI processes, during which we can miss the drawing of the focus ring for the
        // first focused element. Making the page focused is a requirement for making the page selection focused.
        // This is iOS only until there's a demonstrated need for this preemptive focus on other platforms.
        if (!page->focusController().isFocused())
            page->focusController().setFocused(true);

        // Reset the page pointer in case FocusController::setFocused(true) caused a side effect that invalidated our old one.
        page = document() ? document()->page() : nullptr;
        if (!page)
            return;
#endif

#if PLATFORM(COCOA)
        RefPtr frameView = documentFrameView();
        if (!frameView)
            return;

        // Legacy WebKit1 case.
        if (frameView->platformWidget())
            page->chrome().client().makeFirstResponder((NSResponder *)frameView->platformWidget());
#endif
#if PLATFORM(MAC)
        else
            page->chrome().client().assistiveTechnologyMakeFirstResponder();
        // WebChromeClient::assistiveTechnologyMakeFirstResponder (the WebKit2 codepath) is intentionally
        // not called on iOS because stealing first-respondership causes issues such as:
        //   1. VoiceOver Speak Screen focus erroneously jumping to the top of the page when encountering an embedded WKWebView
        //   2. Third-party apps relying on WebKit to not steal first-respondership (https://bugs.webkit.org/show_bug.cgi?id=249976)
#endif
    }
}

AccessibilitySortDirection AccessibilityObject::sortDirection() const
{
    // Only row and column headers are allowed to have aria-sort.
    // https://w3c.github.io/aria/#aria-sort
    if (!isColumnHeader() && !isRowHeader())
        return AccessibilitySortDirection::Invalid;

    auto& sortAttribute = getAttribute(aria_sortAttr);
    if (sortAttribute.isNull())
        return AccessibilitySortDirection::None;

    if (equalLettersIgnoringASCIICase(sortAttribute, "ascending"_s))
        return AccessibilitySortDirection::Ascending;
    if (equalLettersIgnoringASCIICase(sortAttribute, "descending"_s))
        return AccessibilitySortDirection::Descending;
    if (equalLettersIgnoringASCIICase(sortAttribute, "other"_s))
        return AccessibilitySortDirection::Other;

    return AccessibilitySortDirection::None;
}

bool AccessibilityObject::supportsHasPopup() const
{
    return hasAttribute(aria_haspopupAttr) || isComboBox();
}

String AccessibilityObject::explicitPopupValue() const
{
    auto& hasPopup = getAttribute(aria_haspopupAttr);
    if (hasPopup.isEmpty()) {
        // In ARIA 1.1, the implicit value for datalists became "listbox."
        if (hasDatalist())
            return "listbox"_s;
        return { };
    }

    for (auto& value : { "menu"_s, "listbox"_s, "tree"_s, "grid"_s, "dialog"_s }) {
        // FIXME: Should fix ambiguity so we don't have to write "characters", but also don't create/destroy a String when passing an ASCIILiteral to equalIgnoringASCIICase.
        if (equalIgnoringASCIICase(hasPopup, value))
            return value;
    }

    // aria-haspopup specification states that true must be treated as menu.
    if (equalLettersIgnoringASCIICase(hasPopup, "true"_s))
        return "menu"_s;
    return { };
}

bool AccessibilityObject::hasDatalist() const
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(element());
    return input && input->hasDataList();
}

bool AccessibilityObject::supportsSetSize() const
{
    return hasAttribute(aria_setsizeAttr);
}

bool AccessibilityObject::supportsPosInSet() const
{
    return hasAttribute(aria_posinsetAttr);
}

int AccessibilityObject::setSize() const
{
    // https://github.com/w3c/aria/pull/2341
    // When aria-setsize isn't a positive integer (greater than or equal to 1), its value should be indeterminate, i.e., -1.
    int setSize = integralAttribute(aria_setsizeAttr);
    return setSize >= 1 ? setSize : -1;
}

int AccessibilityObject::posInSet() const
{
    // https://github.com/w3c/aria/pull/2341
    // When aria-posinset isn't a positive integer (greater than or equal to 1), its value should be 1.
    return std::max(1, integralAttribute(aria_posinsetAttr));
}

String AccessibilityObject::identifierAttribute() const
{
    return getAttribute(idAttr);
}

Vector<String> AccessibilityObject::classList() const
{
    RefPtr element = this->element();
    if (!element)
        return { };

    Ref domClassList = element->classList();
    Vector<String> classList;
    unsigned length = domClassList->length();
    classList.reserveInitialCapacity(length);
    for (unsigned k = 0; k < length; k++)
        classList.append(domClassList->item(k).string());
    return classList;
}

String AccessibilityObject::extendedDescription() const
{
    auto describedBy = ariaDescribedByAttribute();
    if (!describedBy.isEmpty())
        return describedBy;

    return getAttribute(HTMLNames::aria_descriptionAttr);
}

bool AccessibilityObject::supportsPressed() const
{
    const AtomString& expanded = getAttribute(aria_pressedAttr);
    return equalLettersIgnoringASCIICase(expanded, "true"_s) || equalLettersIgnoringASCIICase(expanded, "false"_s);
}

bool AccessibilityObject::supportsExpanded() const
{
    // commandfor attribute takes precedence over popovertarget attribute.
    if (RefPtr targetElement = commandForElement()) {
        // If the target element is a popover then check command is popover related.
        if (targetElement->popoverState() != PopoverState::None) {
            switch (commandType()) {
            // Expose an expanded state if the command is valid for a popover.
            case CommandType::ShowPopover:
            case CommandType::HidePopover:
            case CommandType::TogglePopover:
                return true;
            case CommandType::Invalid:
            case CommandType::Custom:
            case CommandType::ShowModal:
            case CommandType::Close:
            case CommandType::RequestClose:
                break;
            default:
                ASSERT_NOT_REACHED();
                break;
            }
        }
    } else if (popoverTargetElement())
        return true;

    if (is<HTMLDetailsElement>(node()))
        return true;

    auto hasValidAriaExpandedValue = [this] () -> bool {
        // Undefined values should not result in this attribute being exposed to ATs according to ARIA.
        const AtomString& expanded = getAttribute(aria_expandedAttr);
        return equalLettersIgnoringASCIICase(expanded, "true"_s) || equalLettersIgnoringASCIICase(expanded, "false"_s);
    };

    if (isColumnHeader() || isRowHeader())
        return hasValidAriaExpandedValue();

    switch (role()) {
    case AccessibilityRole::Details:
        return true;
    case AccessibilityRole::Button:
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::ComboBox:
    case AccessibilityRole::GridCell:
    case AccessibilityRole::Link:
    case AccessibilityRole::MenuItem:
    case AccessibilityRole::MenuItemCheckbox:
    case AccessibilityRole::MenuItemRadio:
    case AccessibilityRole::Row:
    case AccessibilityRole::Switch:
    case AccessibilityRole::Tab:
    case AccessibilityRole::TreeItem:
    case AccessibilityRole::WebApplication:
        return hasValidAriaExpandedValue();
    default:
        return false;
    }
}

double AccessibilityObject::loadingProgress() const
{
    if (isLoaded())
        return 1.0;

    RefPtr page = this->page();
    if (!page)
        return 0.0;

    return page->progress().estimatedProgress();
}

bool AccessibilityObject::isExpanded() const
{
    if (RefPtr details = dynamicDowncast<HTMLDetailsElement>(node()))
        return details->hasAttribute(openAttr);
    
    // Summary element should use its details parent's expanded status.
    if (isSummary()) {
        if (RefPtr parent = Accessibility::findAncestor<AccessibilityObject>(*this, false, [] (const AccessibilityObject& object) {
            return is<HTMLDetailsElement>(object.node());
        }))
            return parent->isExpanded();
    }

    if (supportsExpanded()) {
        if (RefPtr commandForElement = this->commandForElement())
            return commandForElement->isPopoverShowing();
        if (RefPtr popoverTargetElement = this->popoverTargetElement())
            return popoverTargetElement->isPopoverShowing();
        return equalLettersIgnoringASCIICase(getAttribute(aria_expandedAttr), "true"_s);
    }

    return false;  
}

bool AccessibilityObject::supportsChecked() const
{
    switch (role()) {
    case AccessibilityRole::Checkbox:
    case AccessibilityRole::MenuItemCheckbox:
    case AccessibilityRole::MenuItemRadio:
    case AccessibilityRole::RadioButton:
    case AccessibilityRole::Switch:
        return true;
    default:
        return false;
    }
}

bool AccessibilityObject::supportsRowCountChange() const
{
    switch (role()) {
    case AccessibilityRole::Tree:
    case AccessibilityRole::TreeGrid:
    case AccessibilityRole::Grid:
    case AccessibilityRole::Table:
        return true;
    default:
        return false;
    }
}

AccessibilityButtonState AccessibilityObject::checkboxOrRadioValue() const
{
    // If this is a real checkbox or radio button, AccessibilityNodeObject will handle.
    // If it's an ARIA checkbox, radio, or switch the aria-checked attribute should be used.
    // If it's a toggle button, the aria-pressed attribute is consulted.

    if (isToggleButton()) {
        const AtomString& ariaPressed = getAttribute(aria_pressedAttr);
        if (equalLettersIgnoringASCIICase(ariaPressed, "true"_s))
            return AccessibilityButtonState::On;
        if (equalLettersIgnoringASCIICase(ariaPressed, "mixed"_s))
            return AccessibilityButtonState::Mixed;
        return AccessibilityButtonState::Off;
    }
    
    const AtomString& result = getAttribute(aria_checkedAttr);
    if (equalLettersIgnoringASCIICase(result, "true"_s))
        return AccessibilityButtonState::On;
    if (equalLettersIgnoringASCIICase(result, "mixed"_s)) {
        // ARIA says that radio, menuitemradio, and switch elements must NOT expose button state mixed.
        AccessibilityRole ariaRole = ariaRoleAttribute();
        if (ariaRole == AccessibilityRole::RadioButton || ariaRole == AccessibilityRole::MenuItemRadio || ariaRole == AccessibilityRole::Switch)
            return AccessibilityButtonState::Off;
        return AccessibilityButtonState::Mixed;
    }
    
    return AccessibilityButtonState::Off;
}

HashMap<String, AXEditingStyleValueVariant> AccessibilityObject::resolvedEditingStyles() const
{
    RefPtr document = this->document();
    if (!document)
        return { };
    
    auto selectionStyle = EditingStyle::styleAtSelectionStart(document->selection().selection());
    if (!selectionStyle)
        return { };

    HashMap<String, AXEditingStyleValueVariant> styles;
    styles.add("bold"_s, selectionStyle->hasStyle(CSSPropertyFontWeight, "bold"_s));
    styles.add("italic"_s, selectionStyle->hasStyle(CSSPropertyFontStyle, "italic"_s));
    styles.add("underline"_s, selectionStyle->hasStyle(CSSPropertyWebkitTextDecorationsInEffect, "underline"_s));
    styles.add("fontsize"_s, selectionStyle->legacyFontSize(*document));
    return styles;
}

// This is a 1-dimensional scroll offset helper function that's applied
// separately in the horizontal and vertical directions, because the
// logic is the same. The goal is to compute the best scroll offset
// in order to make an object visible within a viewport.
//
// If the object is already fully visible, returns the same scroll
// offset.
//
// In case the whole object cannot fit, you can specify a
// subfocus - a smaller region within the object that should
// be prioritized. If the whole object can fit, the subfocus is
// ignored.
//
// If possible, the object and subfocus are centered within the
// viewport.
//
// Example 1: the object is already visible, so nothing happens.
//   +----------Viewport---------+
//                 +---Object---+
//                 +--SubFocus--+
//
// Example 2: the object is not fully visible, so it's centered
// within the viewport.
//   Before:
//   +----------Viewport---------+
//                         +---Object---+
//                         +--SubFocus--+
//
//   After:
//                 +----------Viewport---------+
//                         +---Object---+
//                         +--SubFocus--+
//
// Example 3: the object is larger than the viewport, so the
// viewport moves to show as much of the object as possible,
// while also trying to center the subfocus.
//   Before:
//   +----------Viewport---------+
//     +---------------Object--------------+
//                         +-SubFocus-+
//
//   After:
//             +----------Viewport---------+
//     +---------------Object--------------+
//                         +-SubFocus-+
//
// When constraints cannot be fully satisfied, the min
// (left/top) position takes precedence over the max (right/bottom).
//
// Note that the return value represents the ideal new scroll offset.
// This may be out of range - the calling function should clip this
// to the available range.
static int computeBestScrollOffset(int currentScrollOffset, int subfocusMin, int subfocusMax, int objectMin, int objectMax, int viewportMin, int viewportMax)
{
    int viewportSize = viewportMax - viewportMin;
    
    // If the object size is larger than the viewport size, consider
    // only a portion that's as large as the viewport, centering on
    // the subfocus as much as possible.
    if (objectMax - objectMin > viewportSize) {
        // Since it's impossible to fit the whole object in the
        // viewport, exit now if the subfocus is already within the viewport.
        if (subfocusMin - currentScrollOffset >= viewportMin && subfocusMax - currentScrollOffset <= viewportMax)
            return currentScrollOffset;
        
        // Subfocus must be within focus.
        subfocusMin = std::max(subfocusMin, objectMin);
        subfocusMax = std::min(subfocusMax, objectMax);
        
        // Subfocus must be no larger than the viewport size; favor top/left.
        if (subfocusMax - subfocusMin > viewportSize)
            subfocusMax = subfocusMin + viewportSize;
        
        // Compute the size of an object centered on the subfocus, the size of the viewport.
        int centeredObjectMin = (subfocusMin + subfocusMax - viewportSize) / 2;
        int centeredObjectMax = centeredObjectMin + viewportSize;

        objectMin = std::max(objectMin, centeredObjectMin);
        objectMax = std::min(objectMax, centeredObjectMax);
    }

    // Exit now if the focus is already within the viewport.
    if (objectMin - currentScrollOffset >= viewportMin
        && objectMax - currentScrollOffset <= viewportMax)
        return currentScrollOffset;
    
    // Center the object in the viewport.
    return (objectMin + objectMax - viewportMin - viewportMax) / 2;
}

bool AccessibilityObject::isOnScreen() const
{
    // To figure out if the element is onscreen, we start by building of a stack starting with the
    // element, and then include every scrollable parent in the hierarchy.
    Vector<RefPtr<const AccessibilityObject>> objects;

    objects.append(this);
    for (RefPtr ancestor = parentObject(); ancestor; ancestor = ancestor->parentObject()) {
        if (ancestor->getScrollableAreaIfScrollable())
            objects.append(ancestor);
    }

    // Now, go back through that chain and make sure each inner object is within the
    // visible bounds of the outer object.
    size_t levels = objects.size() - 1;
    for (size_t i = levels; i >= 1; i--) {
        RefPtr outer = objects[i];
        RefPtr inner = objects[i - 1];
        // FIXME: unclear if we need LegacyIOSDocumentVisibleRect.
        const IntRect outerRect = i < levels ? snappedIntRect(outer->boundingBoxRect()) : outer->getScrollableAreaIfScrollable()->visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect);

        IntRect innerRect = snappedIntRect(inner->boundingBoxRect());
        if (RefPtr scrollView = !outer->isRoot() ? outer->scrollView() : nullptr)
            innerRect = scrollView->contentsToRootView(innerRect);

        if (!outerRect.intersects(innerRect))
            return false;
    }
    return true;
}

void AccessibilityObject::scrollToMakeVisible() const
{
    scrollToMakeVisible({ SelectionRevealMode::Reveal, ScrollAlignment::alignCenterIfNeeded, ScrollAlignment::alignCenterIfNeeded, ShouldAllowCrossOriginScrolling::Yes });
}

void AccessibilityObject::scrollToMakeVisible(const ScrollRectToVisibleOptions& options) const
{
    if (isScrollView() && parentObject())
        parentObject()->scrollToMakeVisible();

    if (auto* renderer = this->renderer())
        LocalFrameView::scrollRectToVisible(boundingBoxRect(), *renderer, false, options);
}

void AccessibilityObject::scrollToMakeVisibleWithSubFocus(IntRect&& subfocus) const
{
    // Search up the parent chain until we find the first one that's scrollable.
    AccessibilityObject* scrollParent = parentObject();
    ScrollableArea* scrollableArea;
    for (scrollableArea = nullptr;
         scrollParent && !(scrollableArea = scrollParent->getScrollableAreaIfScrollable());
         scrollParent = scrollParent->parentObject()) { }
    if (!scrollableArea)
        return;

    LayoutRect objectRect = boundingBoxRect();
    IntPoint scrollPosition = scrollableArea->scrollPosition();
    // FIXME: unclear if we need LegacyIOSDocumentVisibleRect.
    IntRect scrollVisibleRect = scrollableArea->visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect);

    if (!scrollParent->isScrollView()) {
        objectRect.moveBy(scrollPosition);
        objectRect.moveBy(-snappedIntRect(scrollParent->elementRect()).location());
    }
    
    int desiredX = computeBestScrollOffset(
        scrollPosition.x(),
        objectRect.x() + subfocus.x(), objectRect.x() + subfocus.maxX(),
        objectRect.x(), objectRect.maxX(),
        0, scrollVisibleRect.width());
    int desiredY = computeBestScrollOffset(
        scrollPosition.y(),
        objectRect.y() + subfocus.y(), objectRect.y() + subfocus.maxY(),
        objectRect.y(), objectRect.maxY(),
        0, scrollVisibleRect.height());

    scrollParent->scrollTo(IntPoint(desiredX, desiredY));

    // Convert the subfocus into the coordinates of the scroll parent.
    IntRect newElementRect = snappedIntRect(elementRect());
    IntRect scrollParentRect = snappedIntRect(scrollParent->elementRect());
    subfocus.move(newElementRect.x(), newElementRect.y());
    subfocus.move(-scrollParentRect.x(), -scrollParentRect.y());

    // Recursively make sure the scroll parent itself is visible.
    if (scrollParent->parentObject())
        scrollParent->scrollToMakeVisibleWithSubFocus(WTFMove(subfocus));
}

FloatRect AccessibilityObject::unobscuredContentRect() const
{
    RefPtr document = this->document();
    if (!document || !document->view())
        return { };
    return FloatRect(snappedIntRect(document->view()->unobscuredContentRect()));
}

void AccessibilityObject::scrollToGlobalPoint(IntPoint&& point) const
{
    // Search up the parent chain and create a vector of all scrollable parent objects
    // and ending with this object itself.
    Vector<const AccessibilityObject*> objects;

    objects.append(this);
    for (AccessibilityObject* parentObject = this->parentObject(); parentObject; parentObject = parentObject->parentObject()) {
        if (parentObject->getScrollableAreaIfScrollable())
            objects.append(parentObject);
    }

    objects.reverse();

    // Start with the outermost scrollable (the main window) and try to scroll the
    // next innermost object to the given point.
    int offsetX = 0, offsetY = 0;
    size_t levels = objects.size() - 1;
    for (size_t i = 0; i < levels; i++) {
        RefPtr outer = objects[i];
        RefPtr inner = objects[i + 1];

        ScrollableArea* scrollableArea = outer->getScrollableAreaIfScrollable();

        LayoutRect innerRect = inner->isScrollView() ? inner->parentObject()->boundingBoxRect() : inner->boundingBoxRect();
        LayoutRect objectRect = innerRect;
        IntPoint scrollPosition = scrollableArea->scrollPosition();

        // Convert the object rect into local coordinates.
        objectRect.move(offsetX, offsetY);
        if (!outer->isScrollView())
            objectRect.move(scrollPosition.x(), scrollPosition.y());

        int desiredX = computeBestScrollOffset(
            0,
            objectRect.x(), objectRect.maxX(),
            objectRect.x(), objectRect.maxX(),
            point.x(), point.x());
        int desiredY = computeBestScrollOffset(
            0,
            objectRect.y(), objectRect.maxY(),
            objectRect.y(), objectRect.maxY(),
            point.y(), point.y());
        outer->scrollTo(IntPoint(desiredX, desiredY));

        if (outer->isScrollView() && !inner->isScrollView()) {
            // If outer object we just scrolled is a scroll view (main window or iframe) but the
            // inner object is not, keep track of the coordinate transformation to apply to
            // future nested calculations.
            scrollPosition = scrollableArea->scrollPosition();
            offsetX -= (scrollPosition.x() + point.x());
            offsetY -= (scrollPosition.y() + point.y());
            point.move(scrollPosition.x() - innerRect.x(),
                       scrollPosition.y() - innerRect.y());
        } else if (inner->isScrollView()) {
            // Otherwise, if the inner object is a scroll view, reset the coordinate transformation.
            offsetX = 0;
            offsetY = 0;
        }
    }
}
    
void AccessibilityObject::scrollAreaAndAncestor(std::pair<ScrollableArea*, AccessibilityObject*>& scrollers) const
{
    // Search up the parent chain until we find the first one that's scrollable.
    scrollers.first = nullptr;
    for (scrollers.second = parentObject(); scrollers.second; scrollers.second = scrollers.second->parentObject()) {
        if ((scrollers.first = scrollers.second->getScrollableAreaIfScrollable()))
            break;
    }
}
    
ScrollableArea* AccessibilityObject::scrollableAreaAncestor() const
{
    std::pair<ScrollableArea*, AccessibilityObject*> scrollers;
    scrollAreaAndAncestor(scrollers);
    return scrollers.first;
}
    
IntPoint AccessibilityObject::scrollPosition() const
{
    if (auto scroller = scrollableAreaAncestor())
        return scroller->scrollPosition();

    return IntPoint();
}

IntRect AccessibilityObject::scrollVisibleContentRect() const
{
    if (auto scroller = scrollableAreaAncestor())
        return scroller->visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect);
    
    return IntRect();
}

IntSize AccessibilityObject::scrollContentsSize() const
{
    if (auto scroller = scrollableAreaAncestor())
        return scroller->contentsSize();

    return IntSize();
}
    
bool AccessibilityObject::scrollByPage(ScrollByPageDirection direction) const
{
    std::pair<ScrollableArea*, AccessibilityObject*> scrollers;
    scrollAreaAndAncestor(scrollers);
    ScrollableArea* scrollableArea = scrollers.first;
    RefPtr scrollParent = scrollers.second;
    
    if (!scrollableArea)
        return false;
    
    IntPoint scrollPosition = scrollableArea->scrollPosition();
    IntPoint newScrollPosition = scrollPosition;
    IntSize scrollSize = scrollableArea->contentsSize();
    IntRect scrollVisibleRect = scrollableArea->visibleContentRect(ScrollableArea::LegacyIOSDocumentVisibleRect);
    switch (direction) {
    case ScrollByPageDirection::Right: {
        int scrollAmount = scrollVisibleRect.size().width();
        int newX = scrollPosition.x() - scrollAmount;
        newScrollPosition.setX(std::max(newX, 0));
        break;
    }
    case ScrollByPageDirection::Left: {
        int scrollAmount = scrollVisibleRect.size().width();
        int newX = scrollAmount + scrollPosition.x();
        int maxX = scrollSize.width() - scrollAmount;
        newScrollPosition.setX(std::min(newX, maxX));
        break;
    }
    case ScrollByPageDirection::Up: {
        int scrollAmount = scrollVisibleRect.size().height();
        int newY = scrollPosition.y() - scrollAmount;
        newScrollPosition.setY(std::max(newY, 0));
        break;
    }
    case ScrollByPageDirection::Down: {
        int scrollAmount = scrollVisibleRect.size().height();
        int newY = scrollAmount + scrollPosition.y();
        int maxY = scrollSize.height() - scrollAmount;
        newScrollPosition.setY(std::min(newY, maxY));
        break;
    }
    }
    
    if (newScrollPosition != scrollPosition) {
        scrollParent->scrollTo(newScrollPosition);
        protectedDocument()->updateLayoutIgnorePendingStylesheets();
        return true;
    }
    
    return false;
}

void AccessibilityObject::setLastKnownIsIgnoredValue(bool isIgnored)
{
    m_lastKnownIsIgnoredValue = isIgnored ? AccessibilityObjectInclusion::IgnoreObject : AccessibilityObjectInclusion::IncludeObject;
}

bool AccessibilityObject::ignoredFromPresentationalRole() const
{
    return role() == AccessibilityRole::Presentational || inheritsPresentationalRole();
}

bool AccessibilityObject::includeIgnoredInCoreTree() const
{
#if ENABLE(INCLUDE_IGNORED_IN_CORE_AX_TREE)
    RefPtr document = this->document();
    return document ? document->settings().includeIgnoredInCoreAXTree() : false;
#else
    return false;
#endif // ENABLE(INCLUDE_IGNORED_IN_CORE_AX_TREE)
}

bool AccessibilityObject::pressedIsPresent() const
{
    return !getAttribute(aria_pressedAttr).isEmpty();
}

TextIteratorBehaviors AccessibilityObject::textIteratorBehaviorForTextRange() const
{
    TextIteratorBehaviors behaviors { TextIteratorBehavior::IgnoresStyleVisibility, TextIteratorBehavior::IgnoresFullSizeKana };

#if USE(ATSPI)
    // We need to emit replaced elements for ATSPI, and present
    // them with the 'object replacement character' (0xFFFC).
    behaviors.add(TextIteratorBehavior::EmitsObjectReplacementCharacters);
#endif
    
    return behaviors;
}

TextIterator AccessibilityObject::textIteratorIgnoringFullSizeKana(const SimpleRange& range)
{
    return TextIterator(range, { TextIteratorBehavior::IgnoresFullSizeKana });
}

AccessibilityRole AccessibilityObject::buttonRoleType() const
{
    // If aria-pressed is present, then it should be exposed as a toggle button.
    // https://www.w3.org/TR/wai-aria#aria-pressed
    if (pressedIsPresent())
        return AccessibilityRole::ToggleButton;
    if (selfOrAncestorLinkHasPopup())
        return AccessibilityRole::PopUpButton;
    // We don't contemplate AccessibilityRole::RadioButton, as it depends on the input type.

    return AccessibilityRole::Button;
}

std::optional<InputType::Type> AccessibilityObject::inputType() const
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(node());
    RefPtr inputType = input ? input->inputType() : nullptr;
    return inputType ? std::optional(inputType->type()) : std::nullopt;
}

bool AccessibilityObject::isIgnoredByDefault() const
{
    return defaultObjectInclusion() == AccessibilityObjectInclusion::IgnoreObject;
}

bool AccessibilityObject::isARIAHidden() const
{
    if (isFocused())
        return false;

    RefPtr node = this->node();
    RefPtr element = dynamicDowncast<Element>(node);
    AtomString tag = element ? element->localName() : nullAtom();
    // https://github.com/w3c/aria/pull/1880
    // To prevent authors from hiding all content from assistive technology users, do not respect
    // aria-hidden on html, body, or document-root svg elements.
    if (tag == bodyTag || tag == htmlTag || (tag == SVGNames::svgTag && !element->parentNode()))
        return false;

    if (RefPtr assignedSlot = node ? node->assignedSlot() : nullptr) {
        if (equalLettersIgnoringASCIICase(assignedSlot->attributeWithDefaultARIA(aria_hiddenAttr), "true"_s))
            return true;
    }
    return element && equalLettersIgnoringASCIICase(element->attributeWithDefaultARIA(aria_hiddenAttr), "true"_s);
}

// ARIA component of hidden definition.
// https://www.w3.org/TR/wai-aria/#dfn-hidden
bool AccessibilityObject::isAXHidden() const
{
    if (isFocused())
        return false;

    return Accessibility::findAncestor<AccessibilityObject>(*this, true, [] (const auto& object) {
        return object.isARIAHidden();
    }) != nullptr;
}

bool AccessibilityObject::isRenderHidden() const
{
    return WebCore::isRenderHidden(style());
}

bool AccessibilityObject::isShowingValidationMessage() const
{
    if (RefPtr element = this->element()) {
        if (RefPtr listedElement = element->asValidatedFormListedElement())
            return listedElement->isShowingValidationMessage();
    }
    return false;
}

String AccessibilityObject::validationMessage() const
{
    if (RefPtr element = this->element()) {
        if (RefPtr listedElement = element->asValidatedFormListedElement())
            return listedElement->validationMessage();
    }
    return String();
}

AccessibilityObjectInclusion AccessibilityObject::defaultObjectInclusion() const
{
    if (const auto* style = this->style()) {
        if (style->effectiveInert())
            return AccessibilityObjectInclusion::IgnoreObject;
        if (isVisibilityHidden(*style))
            return AccessibilityObjectInclusion::IgnoreObject;
    }

    bool useParentData = !m_isIgnoredFromParentData.isNull();
    if (useParentData && (m_isIgnoredFromParentData.isAXHidden || m_isIgnoredFromParentData.isPresentationalChildOfAriaRole))
        return AccessibilityObjectInclusion::IgnoreObject;

    if (isARIAHidden() || isWithinHiddenWebArea())
        return AccessibilityObjectInclusion::IgnoreObject;

    bool ignoreARIAHidden = isFocused();
    if (Accessibility::findAncestor<AccessibilityObject>(*this, false, [&] (const auto& object) {
        const auto* style = object.style();
        if (style && style->display() == DisplayType::None) {
            // We don't want to use AccessibilityObject::isRenderHidden(), as that also checks and returns true
            // for visibility:hidden, which would be wrong if |this| has a visibility:visible ancestor before
            // this visibility:hidden ancestor (visibility:visible cancels out visibility:hidden).
            //
            // We check the isVisibilityHidden at the top of this method, so that covers us as far as visibility goes.
            return true;
        }

        return (!ignoreARIAHidden && object.isARIAHidden()) || object.ariaRoleHasPresentationalChildren() || !object.canHaveChildren();
    }))
        return AccessibilityObjectInclusion::IgnoreObject;

    // Include <dialog> elements and elements with role="dialog".
    if (role() == AccessibilityRole::ApplicationDialog)
        return AccessibilityObjectInclusion::IncludeObject;

    return accessibilityPlatformIncludesObject();
}

bool AccessibilityObject::isWithinHiddenWebArea() const
{
    RefPtr webArea = this->containingWebArea();
    CheckedPtr renderView = webArea ? dynamicDowncast<RenderView>(webArea->renderer()) : nullptr;
    CheckedPtr frameRenderer = renderView ? renderView->frameView().frame().ownerRenderer() : nullptr;
    while (frameRenderer) {
        const auto& style = frameRenderer->style();
        if (isVisibilityHidden(style) || style.effectiveInert())
            return true;

        renderView = frameRenderer->document().renderView();
        frameRenderer = renderView ? renderView->frameView().frame().ownerRenderer() : nullptr;
    }
    return false;

}
    
bool AccessibilityObject::isIgnored() const
{
    AXComputedObjectAttributeCache* attributeCache = nullptr;
    auto* axObjectCache = this->axObjectCache();
    if (axObjectCache)
        attributeCache = axObjectCache->computedObjectAttributeCache();
    
    if (attributeCache) {
        AccessibilityObjectInclusion ignored = attributeCache->getIgnored(objectID());
        switch (ignored) {
        case AccessibilityObjectInclusion::IgnoreObject:
            return true;
        case AccessibilityObjectInclusion::IncludeObject:
            return false;
        case AccessibilityObjectInclusion::DefaultBehavior:
            break;
        }
    }

    bool ignored = isIgnoredWithoutCache(axObjectCache);

    // Refetch the attribute cache in case it was enabled as part of computing isIgnored.
    if (axObjectCache && (attributeCache = axObjectCache->computedObjectAttributeCache()))
        attributeCache->setIgnored(objectID(), ignored ? AccessibilityObjectInclusion::IgnoreObject : AccessibilityObjectInclusion::IncludeObject);

    return ignored;
}

bool AccessibilityObject::isIgnoredWithoutCache(AXObjectCache* cache) const
{
    // If we are in the midst of retrieving the current modal node, we only need to consider whether the object
    // is inherently ignored via computeIsIgnored. Also, calling ignoredFromModalPresence
    // in this state would cause infinite recursion.
    bool ignored = cache && cache->isRetrievingCurrentModalNode() ? false : ignoredFromModalPresence();
    if (!ignored)
        ignored = computeIsIgnored();

    auto previousLastKnownIsIgnoredValue = m_lastKnownIsIgnoredValue;
    const_cast<AccessibilityObject*>(this)->setLastKnownIsIgnoredValue(ignored);

    if (cache) {
        bool becameUnignored = previousLastKnownIsIgnoredValue == AccessibilityObjectInclusion::IgnoreObject && !ignored;
        bool becameIgnored = !becameUnignored && previousLastKnownIsIgnoredValue == AccessibilityObjectInclusion::IncludeObject && ignored;

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
        if (becameIgnored)
            cache->objectBecameIgnored(*this);
        else if (becameUnignored)
            cache->objectBecameUnignored(*this);
#endif // ENABLE(ACCESSIBILITY_ISOLATED_TREE)

        if (becameUnignored || becameIgnored) {
            // FIXME: We should not have to submit a children-changed when ENABLE(INCLUDE_IGNORED_IN_CORE_AX_TREE), but that causes a few failing
            // tests. We should fix that or remove this comment before enabling ENABLE(INCLUDE_IGNORED_IN_CORE_AX_TREE) by default for any port.
            cache->childrenChanged(parentObject());
        }
    }
    return ignored;
}

Vector<Ref<Element>> AccessibilityObject::elementsFromAttribute(const QualifiedName& attribute) const
{
    RefPtr element = dynamicDowncast<Element>(node());
    if (!element)
        return { };

    if (auto elementsFromAttribute = element->elementsArrayForAttributeInternal(attribute))
        return elementsFromAttribute.value();

    if (auto* defaultARIA = element->customElementDefaultARIAIfExists())
        return defaultARIA->elementsForAttribute(*element, attribute);

    return { };
}

#if PLATFORM(COCOA)
bool AccessibilityObject::preventKeyboardDOMEventDispatch() const
{
    RefPtr frame = this->frame();
    return frame && frame->settings().preventKeyboardDOMEventDispatch();
}

void AccessibilityObject::setPreventKeyboardDOMEventDispatch(bool on)
{
    RefPtr frame = this->frame();
    if (!frame)
        return;
    frame->settings().setPreventKeyboardDOMEventDispatch(on);
}
#endif

AccessibilityObject* AccessibilityObject::radioGroupAncestor() const
{
    return Accessibility::findAncestor<AccessibilityObject>(*this, false, [] (const AccessibilityObject& object) {
        return object.isRadioGroup();
    });
}

ElementName AccessibilityObject::elementName() const
{
    RefPtr element = this->element();
    return element ? element->elementName() : ElementName::Unknown;
}

bool AccessibilityObject::isStyleFormatGroup() const
{
    if (isCode())
        return true;

    auto elementName = this->elementName();
    return elementName == ElementName::HTML_kbd || elementName == ElementName::HTML_code
    || elementName == ElementName::HTML_pre || elementName == ElementName::HTML_samp
    || elementName == ElementName::HTML_var || elementName == ElementName::HTML_cite
    || elementName == ElementName::HTML_ins || elementName == ElementName::HTML_del
    || elementName == ElementName::HTML_sup || elementName == ElementName::HTML_sub;
}

bool AccessibilityObject::isFigureElement() const
{
    return elementName() == ElementName::HTML_figure;
}

bool AccessibilityObject::isKeyboardFocusable() const
{
    RefPtr element = this->element();
    return element && element->isFocusable();
}

bool AccessibilityObject::isOutput() const
{
    return elementName() == ElementName::HTML_output;
}
    
bool AccessibilityObject::isContainedBySecureField() const
{
    Node* node = this->node();
    if (!node)
        return false;
    
    if (ariaRoleAttribute() != AccessibilityRole::Unknown)
        return false;

    RefPtr input = dynamicDowncast<HTMLInputElement>(node->shadowHost());
    return input && input->isSecureField();
}

AXCoreObject::AccessibilityChildrenVector AccessibilityObject::relatedObjects(AXRelation relation) const
{
    auto* cache = axObjectCache();
    if (!cache)
        return { };

    auto relatedObjectIDs = cache->relatedObjectIDsFor(*this, relation);
    if (!relatedObjectIDs)
        return { };
    return cache->objectsForIDs(*relatedObjectIDs);
}

bool AccessibilityObject::shouldFocusActiveDescendant() const
{
    switch (ariaRoleAttribute()) {
    case AccessibilityRole::Group:
    case AccessibilityRole::ListBox:
    case AccessibilityRole::Menu:
    case AccessibilityRole::MenuBar:
    case AccessibilityRole::RadioGroup:
    case AccessibilityRole::Row:
    case AccessibilityRole::PopUpButton:
    case AccessibilityRole::Meter:
    case AccessibilityRole::ProgressIndicator:
    case AccessibilityRole::Toolbar:
    case AccessibilityRole::Tree:
    case AccessibilityRole::Grid:
    /* FIXME: replace these with actual roles when they are added to AccessibilityRole
    composite
    alert
    alertdialog
    status
    timer
    */
        return true;
    default:
        return false;
    }
}

bool AccessibilityObject::ariaRoleHasPresentationalChildren() const
{
    switch (ariaRoleAttribute()) {
    case AccessibilityRole::Button:
    case AccessibilityRole::Slider:
    case AccessibilityRole::Image:
    case AccessibilityRole::ProgressIndicator:
    case AccessibilityRole::SpinButton:
        return true;
    default:
        return false;
    }
}

void AccessibilityObject::setIsIgnoredFromParentDataForChild(AccessibilityObject& child)
{
    AccessibilityIsIgnoredFromParentData result = AccessibilityIsIgnoredFromParentData(this);
    if (!m_isIgnoredFromParentData.isNull()) {
        result.isAXHidden = (m_isIgnoredFromParentData.isAXHidden || child.isARIAHidden()) && !child.isFocused();
        result.isPresentationalChildOfAriaRole = m_isIgnoredFromParentData.isPresentationalChildOfAriaRole || ariaRoleHasPresentationalChildren();
        result.isDescendantOfBarrenParent = m_isIgnoredFromParentData.isDescendantOfBarrenParent || !canHaveChildren();
    } else {
        if (child.isARIAHidden())
            result.isAXHidden = true;

        bool ignoreARIAHidden = child.isFocused();
        for (auto* object = child.parentObject(); object; object = object->parentObject()) {
            if (!result.isAXHidden && !ignoreARIAHidden && object->isARIAHidden())
                result.isAXHidden = true;

            if (!result.isPresentationalChildOfAriaRole && object->ariaRoleHasPresentationalChildren())
                result.isPresentationalChildOfAriaRole = true;

            if (!result.isDescendantOfBarrenParent && !object->canHaveChildren())
                result.isDescendantOfBarrenParent = true;
        }
    }

    child.setIsIgnoredFromParentData(result);
}

String AccessibilityObject::innerHTML() const
{
    RefPtr element = this->element();
    return element ? element->innerHTML() : String();
}

String AccessibilityObject::outerHTML() const
{
    RefPtr element = this->element();
    return element ? element->outerHTML() : String();
}

bool AccessibilityObject::ignoredByRowAncestor() const
{
    RefPtr ancestor = Accessibility::findAncestor<AccessibilityObject>(*this, false, [] (const AccessibilityObject& ancestor) {
        // If an object has a table cell ancestor (before a table row), that is a cell's contents, so don't ignore it.
        // Similarly, if an object has a table ancestor (before a row), that could be a row, row group or other container, so don't ignore it.
        return ancestor.isTableCell() || ancestor.isTableRow() || ancestor.isTable();
    });

    return ancestor && ancestor->isTableRow();
}

AccessibilityObject* AccessibilityObject::containingWebArea() const
{
    CheckedPtr frameView = documentFrameView();
    CheckedPtr cache = axObjectCache();
    RefPtr root = cache ? dynamicDowncast<AccessibilityScrollView>(cache->getOrCreate(frameView.get())) : nullptr;
    return root ? root->webAreaObject() : nullptr;
}

} // namespace WebCore
