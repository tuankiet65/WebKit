/*
 * Copyright (C) 2006, 2007, 2013 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nuanti Ltd.
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
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "FocusController.h"

#include "AXObjectCache.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "ContainerNodeInlines.h"
#include "Editing.h"
#include "Editor.h"
#include "EditorClient.h"
#include "Element.h"
#include "ElementRareData.h"
#include "ElementTraversal.h"
#include "Event.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "FocusOptions.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "HTMLAreaElement.h"
#include "HTMLImageElement.h"
#include "HTMLInputElement.h"
#include "HTMLPlugInElement.h"
#include "HTMLSlotElement.h"
#include "HTMLTextAreaElement.h"
#include "HitTestResult.h"
#include "KeyboardEvent.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "Page.h"
#include "PopoverData.h"
#include "RemoteFrame.h"
#include "RemoteFrameClient.h"
#include "ScrollAnimator.h"
#include "SelectionRestorationMode.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "SpatialNavigation.h"
#include "UserGestureIndicator.h"
#include "Widget.h"
#include <limits>
#include <wtf/Ref.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(FocusController);

using namespace HTMLNames;

static HTMLElement* invokerForOpenPopover(const Node* candidatePopover)
{
    RefPtr popover = dynamicDowncast<HTMLElement>(candidatePopover);
    if (popover && popover->isPopoverShowing())
        return popover->popoverData()->invoker();
    return nullptr;
}

static Element* openPopoverForInvoker(const Node* candidateInvoker)
{
    RefPtr invoker = dynamicDowncast<HTMLElement>(candidateInvoker);
    if (!invoker)
        return nullptr;
    RefPtr popover = invoker->invokedPopover();
    if (popover && popover->isPopoverShowing() && popover->popoverData()->invoker() == invoker)
        return popover.get();
    return nullptr;
}

static inline bool hasCustomFocusLogic(const Element& element)
{
    RefPtr htmlElement = dynamicDowncast<HTMLElement>(element);
    return htmlElement && htmlElement->hasCustomFocusLogic();
}

static inline bool isFocusScopeOwner(const Element& element)
{
    if (element.shadowRoot() && !hasCustomFocusLogic(element))
        return true;
    if (is<HTMLSlotElement>(element)) {
        ShadowRoot* root = element.containingShadowRoot();
        if (!root || !root->host() || !hasCustomFocusLogic(*root->host()))
            return true;
    }
    if (invokerForOpenPopover(&element))
        return true;
    return false;
}

static void clearSelectionIfNeeded(LocalFrame* oldFocusedFrame, LocalFrame* newFocusedFrame, Node* newFocusedNode)
{
    if (!oldFocusedFrame)
        return;

    if (newFocusedFrame && oldFocusedFrame->document() != newFocusedFrame->document())
        return;

    const auto& selection = oldFocusedFrame->selection().selection();
    if (selection.isNone())
        return;

    bool caretBrowsing = oldFocusedFrame->settings().caretBrowsingEnabled();
    if (caretBrowsing)
        return;

    if (newFocusedNode) {
        Node* selectionStartNode = selection.start().deprecatedNode();
        if (newFocusedNode->contains(selectionStartNode) || selectionStartNode->shadowHost() == newFocusedNode)
            return;
    }

    if (RefPtr mousePressNode = newFocusedFrame ? newFocusedFrame->eventHandler().mousePressNode() : nullptr) {
        if (!mousePressNode->canStartSelection()) {
            // Don't clear the selection for contentEditable elements, but do clear it for input and textarea. See bug 38696.
            auto* root = selection.rootEditableElement();
            if (!root)
                return;
            auto* host = root->shadowHost();
            // FIXME: Seems likely we can just do the check on "host" here instead of "rootOrHost".
            auto* rootOrHost = host ? host : root;
            if (!is<HTMLInputElement>(*rootOrHost) && !is<HTMLTextAreaElement>(*rootOrHost))
                return;
        }
    }

    oldFocusedFrame->selection().clear();
}

class FocusNavigationScope {
public:
    Element* owner() const;
    WEBCORE_EXPORT static FocusNavigationScope scopeOf(Node&);
    static FocusNavigationScope scopeOwnedByScopeOwner(Element&);
    static FocusNavigationScope scopeOwnedByIFrame(HTMLFrameOwnerElement&);

    Node* firstNodeInScope() const;
    Node* lastNodeInScope() const;
    Node* nextInScope(const Node*) const;
    Node* previousInScope(const Node*) const;
    Node* lastChildInScope(const Node&) const;

private:
    enum class SlotKind : uint8_t { Assigned, Fallback };

    Node* firstChildInScope(const Node&) const;

    Node* parentInScope(const Node&) const;

    Node* nextSiblingInScope(const Node&) const;
    Node* previousSiblingInScope(const Node&) const;

    explicit FocusNavigationScope(TreeScope&);
    explicit FocusNavigationScope(HTMLSlotElement&, SlotKind);
    explicit FocusNavigationScope(Element&);

    RefPtr<ContainerNode> m_treeScopeRootNode;
    RefPtr<HTMLSlotElement> m_slotElement;
    SlotKind m_slotKind { SlotKind::Assigned };
};

// FIXME: Focus navigation should work with shadow trees that have slots.
Node* FocusNavigationScope::firstChildInScope(const Node& node) const
{
    if (RefPtr element = dynamicDowncast<Element>(node); element && isFocusScopeOwner(*element))
        return nullptr;
    auto* first = node.firstChild();
    while (invokerForOpenPopover(first))
        first = first->nextSibling();
    return first;
}

Node* FocusNavigationScope::lastChildInScope(const Node& node) const
{
    if (RefPtr element = dynamicDowncast<Element>(node); element && isFocusScopeOwner(*element))
        return nullptr;
    auto* last = node.lastChild();
    while (invokerForOpenPopover(last))
        last = last->previousSibling();
    return last;
}

Node* FocusNavigationScope::parentInScope(const Node& node) const
{
    if (m_treeScopeRootNode == &node)
        return nullptr;

    if (m_slotElement) [[unlikely]] {
        if (m_slotKind == SlotKind::Assigned) {
            if (m_slotElement == node.assignedSlot())
                return nullptr;
        } else {
            ASSERT(m_slotKind == SlotKind::Fallback);
            auto* parentNode = node.parentNode();
            if (parentNode == m_slotElement)
                return nullptr;
        }
    }

    return node.parentNode();
}

Node* FocusNavigationScope::nextSiblingInScope(const Node& node) const
{
    if (m_slotElement && m_slotElement == node.assignedSlot()) [[unlikely]] {
        for (Node* current = node.nextSibling(); current; current = current->nextSibling()) {
            if (current->assignedSlot() == m_slotElement)
                return current;
        }
        return nullptr;
    }
    if (m_treeScopeRootNode == &node)
        return nullptr;
    auto* next = node.nextSibling();
    while (invokerForOpenPopover(next))
        next = next->nextSibling();
    return next;
}

Node* FocusNavigationScope::previousSiblingInScope(const Node& node) const
{
    if (m_slotElement && m_slotElement == node.assignedSlot()) [[unlikely]] {
        for (Node* current = node.previousSibling(); current; current = current->previousSibling()) {
            if (current->assignedSlot() == m_slotElement)
                return current;
        }
        return nullptr;
    }
    if (m_treeScopeRootNode == &node)
        return nullptr;
    auto* previous = node.previousSibling();
    while (invokerForOpenPopover(previous))
        previous = previous->previousSibling();
    return previous;
}

Node* FocusNavigationScope::firstNodeInScope() const
{
    if (m_slotElement) [[unlikely]] {
        auto* assignedNodes = m_slotElement->assignedNodes();
        if (m_slotKind == SlotKind::Assigned) {
            ASSERT(assignedNodes);
            return assignedNodes->first().get();
        }
        ASSERT(m_slotKind == SlotKind::Fallback);
        return m_slotElement->firstChild();
    }
    // Popovers with invokers delegate focus.
    if (invokerForOpenPopover(m_treeScopeRootNode.get()))
        return m_treeScopeRootNode->firstChild();
    ASSERT(m_treeScopeRootNode);
    return m_treeScopeRootNode.get();
}

Node* FocusNavigationScope::lastNodeInScope() const
{
    if (m_slotElement) [[unlikely]] {
        auto* assignedNodes = m_slotElement->assignedNodes();
        if (m_slotKind == SlotKind::Assigned) {
            ASSERT(assignedNodes);
            return assignedNodes->last().get();
        }
        ASSERT(m_slotKind == SlotKind::Fallback);
        return m_slotElement->lastChild();
    }
    // Popovers with invokers delegate focus.
    if (invokerForOpenPopover(m_treeScopeRootNode.get()))
        return m_treeScopeRootNode->lastChild();
    ASSERT(m_treeScopeRootNode);
    return m_treeScopeRootNode.get();
}

Node* FocusNavigationScope::nextInScope(const Node* node) const
{
    ASSERT(node);
    if (Node* next = firstChildInScope(*node))
        return next;
    if (Node* next = nextSiblingInScope(*node))
        return next;
    const Node* current = node;
    while (current && !nextSiblingInScope(*current))
        current = parentInScope(*current);
    return current ? nextSiblingInScope(*current) : nullptr;
}

Node* FocusNavigationScope::previousInScope(const Node* node) const
{
    ASSERT(node);
    if (node == firstNodeInScope())
        return nullptr;
    if (Node* current = previousSiblingInScope(*node)) {
        while (Node* child = lastChildInScope(*current))
            current = child;
        return current;
    }
    return parentInScope(*node);
}

FocusNavigationScope::FocusNavigationScope(TreeScope& treeScope)
    : m_treeScopeRootNode(&treeScope.rootNode())
{
}

FocusNavigationScope::FocusNavigationScope(HTMLSlotElement& slotElement, SlotKind slotKind)
    : m_slotElement(&slotElement)
    , m_slotKind(slotKind)
{
}

FocusNavigationScope::FocusNavigationScope(Element& element)
    : m_treeScopeRootNode(&element)
{
}

Element* FocusNavigationScope::owner() const
{
    if (m_slotElement)
        return m_slotElement.get();

    ASSERT(m_treeScopeRootNode);
    if (RefPtr shadowRoot = dynamicDowncast<ShadowRoot>(*m_treeScopeRootNode))
        return shadowRoot->host();
    if (invokerForOpenPopover(m_treeScopeRootNode.get()))
        return downcast<Element>(m_treeScopeRootNode.get());
    if (auto* frame = m_treeScopeRootNode->document().frame())
        return frame->ownerElement();
    return nullptr;
}

FocusNavigationScope FocusNavigationScope::scopeOf(Node& startingNode)
{
    ASSERT(startingNode.isInTreeScope());
    RefPtr<Node> root;
    RefPtr<Node> parentNode;
    for (RefPtr<Node> currentNode = startingNode; currentNode; currentNode = parentNode) {
        root = currentNode;
        if (HTMLSlotElement* slot = currentNode->assignedSlot()) {
            if (isFocusScopeOwner(*slot))
                return FocusNavigationScope(*slot, SlotKind::Assigned);
        }
        if (RefPtr shadowRoot = dynamicDowncast<ShadowRoot>(currentNode))
            return FocusNavigationScope(*shadowRoot);
        if (invokerForOpenPopover(currentNode.get()))
            return FocusNavigationScope(downcast<Element>(*currentNode));
        parentNode = currentNode->parentNode();
        // The scope of a fallback content of a HTMLSlotElement is the slot element
        // but the scope of a HTMLSlotElement is its parent scope.
        if (RefPtr slot = dynamicDowncast<HTMLSlotElement>(parentNode); slot && !slot->assignedNodes())
            return FocusNavigationScope(*slot, SlotKind::Fallback);
    }
    ASSERT(root);
    return FocusNavigationScope(root->treeScope());
}

FocusNavigationScope FocusNavigationScope::scopeOwnedByScopeOwner(Element& element)
{
    ASSERT(element.shadowRoot() || is<HTMLSlotElement>(element) || invokerForOpenPopover(&element));
    if (RefPtr slot = dynamicDowncast<HTMLSlotElement>(element))
        return FocusNavigationScope(*slot, slot->assignedNodes() ? SlotKind::Assigned : SlotKind::Fallback);
    if (element.shadowRoot())
        return FocusNavigationScope(*element.shadowRoot());
    return FocusNavigationScope(element);
}

FocusNavigationScope FocusNavigationScope::scopeOwnedByIFrame(HTMLFrameOwnerElement& frame)
{
    ASSERT(is<LocalFrame>(frame.contentFrame()));
    ASSERT(downcast<LocalFrame>(frame.contentFrame())->document());
    return FocusNavigationScope(*downcast<LocalFrame>(frame.contentFrame())->document());
}

static inline void dispatchEventsOnWindowAndFocusedElement(Document* document, bool focused)
{
    // If we have a focused node we should dispatch blur on it before we blur the window.
    // If we have a focused node we should dispatch focus on it after we focus the window.
    // https://bugs.webkit.org/show_bug.cgi?id=27105

    // Do not fire events while modal dialogs are up.  See https://bugs.webkit.org/show_bug.cgi?id=33962
    if (Page* page = document->page()) {
        if (page->defersLoading())
            return;
    }

    if (!focused && document->focusedElement())
        document->focusedElement()->dispatchBlurEvent(nullptr);
    document->dispatchWindowEvent(Event::create(focused ? eventNames().focusEvent : eventNames().blurEvent, Event::CanBubble::No, Event::IsCancelable::No));
    if (focused && document->focusedElement())
        document->focusedElement()->dispatchFocusEvent(nullptr, { });
}

static inline bool isFocusableElementOrScopeOwner(Element& element, const FocusEventData& focusEventData)
{
    return element.isKeyboardFocusable(focusEventData) || isFocusScopeOwner(element);
}

static inline bool isNonFocusableScopeOwner(Element& element, const FocusEventData& focusEventData)
{
    return !element.isKeyboardFocusable(focusEventData) && isFocusScopeOwner(element);
}

static inline bool isFocusableScopeOwner(Element& element, const FocusEventData& focusEventData)
{
    return element.isKeyboardFocusable(focusEventData) && isFocusScopeOwner(element);
}

static inline int shadowAdjustedTabIndex(Element& element, const FocusEventData& focusEventData)
{
    if (isNonFocusableScopeOwner(element, focusEventData)) {
        if (!element.tabIndexSetExplicitly())
            return 0; // Treat a shadow host without tabindex if it has tabindex=0 even though HTMLElement::tabIndex returns -1 on such an element.
    }
    return element.shouldBeIgnoredInSequentialFocusNavigation() ? -1 : element.tabIndexSetExplicitly().value_or(0);
}

FocusController::FocusController(Page& page, OptionSet<ActivityState> activityState)
    : m_page(page)
    , m_isChangingFocusedFrame(false)
    , m_activityState(activityState)
    , m_focusRepaintTimer(*this, &FocusController::focusRepaintTimerFired)
{
}

void FocusController::setFocusedFrame(Frame* frame, BroadcastFocusedFrame broadcast)
{
    ASSERT(!frame || frame->page() == m_page.ptr());
    if (m_focusedFrame == frame || m_isChangingFocusedFrame)
        return;

    m_isChangingFocusedFrame = true;

    RefPtr oldFrame { focusedLocalFrame() };
    RefPtr newFrame { dynamicDowncast<LocalFrame>(frame) };

    m_focusedFrame = frame;

    // Now that the frame is updated, fire events and update the selection focused states of both frames.
    if (auto* oldFrameView = oldFrame ? oldFrame->view() : nullptr) {
        oldFrameView->stopKeyboardScrollAnimation();
        oldFrame->selection().setFocused(false);
        oldFrame->document()->dispatchWindowEvent(Event::create(eventNames().blurEvent, Event::CanBubble::No, Event::IsCancelable::No));
        Frame* frame = oldFrame.get();
        do {
            if (auto* localFrame = dynamicDowncast<LocalFrame>(frame))
                localFrame->document()->updateServiceWorkerClientData();
            frame = frame->tree().parent();
        } while (frame);
    }

#if PLATFORM(IOS_FAMILY)
    if (oldFrame)
        oldFrame->eventHandler().cancelSelectionAutoscroll();
#endif

    if (newFrame && newFrame->view() && isFocused()) {
        newFrame->selection().setFocused(true);
        newFrame->document()->dispatchWindowEvent(Event::create(eventNames().focusEvent, Event::CanBubble::No, Event::IsCancelable::No));
        Frame* frame = newFrame.get();
        do {
            if (auto* localFrame = dynamicDowncast<LocalFrame>(frame))
                localFrame->document()->updateServiceWorkerClientData();
            frame = frame->tree().parent();
        } while (frame);
    }

    if (broadcast == BroadcastFocusedFrame::Yes)
        protectedPage()->chrome().focusedFrameChanged(frame);

    m_isChangingFocusedFrame = false;
}

LocalFrame* FocusController::focusedOrMainFrame() const
{
    if (auto* frame = focusedLocalFrame())
        return frame;
    if (RefPtr localMainFrame = m_page->localMainFrame())
        return localMainFrame.get();
    ASSERT(m_page->settings().siteIsolationEnabled());
    return nullptr;
}

void FocusController::setFocused(bool focused)
{
    protectedPage()->setActivityState(focused ? m_activityState | ActivityState::IsFocused : m_activityState - ActivityState::IsFocused);
}

void FocusController::setFocusedInternal(bool focused)
{
    if (!isFocused()) {
        if (RefPtr focusedOrMainFrame = this->focusedOrMainFrame())
            focusedOrMainFrame->eventHandler().stopAutoscrollTimer();
    }

    if (!focusedFrame())
        setFocusedFrame(m_page->protectedMainFrame().ptr());

    RefPtr focusedFrame = focusedLocalFrame();
    if (focusedFrame && focusedFrame->view()) {
        focusedFrame->checkedSelection()->setFocused(focused);
        dispatchEventsOnWindowAndFocusedElement(focusedFrame->protectedDocument().get(), focused);
    }
}

FocusableElementSearchResult FocusController::findAndFocusElementStartingWithLocalFrame(FocusDirection direction, const FocusEventData& focusEventData, LocalFrame& frame)
{
    RefPtr document = frame.document();
    if (!document)
        return { nullptr };

    // We are advancing focus in this frame's process in response to a keypress in a different frame's process.
    // We therefore assume we have an active user gesture, which is necessary for element-finding and focus-advancing to work.
    UserGestureIndicator gestureIndicator(IsProcessingUserGesture::Yes, document.get());

    return findAndFocusElementInDocumentOrderStartingWithFrame(frame, document->documentElement(), nullptr, direction, focusEventData, InitialFocus::No, ContinuingRemoteSearch::Yes);
}

FocusableElementSearchResult FocusController::findFocusableElementDescendingIntoSubframes(FocusDirection direction, Element* startingElement, const FocusEventData& focusEventData)
{
    // The node we found might be a HTMLFrameOwnerElement, so descend down the tree until we find either:
    // 1) a focusable node, or
    // 2) the deepest-nested HTMLFrameOwnerElement.
    RefPtr element = startingElement;
    while (RefPtr owner = dynamicDowncast<HTMLFrameOwnerElement>(element)) {
        if (RefPtr remoteFrame = dynamicDowncast<RemoteFrame>(owner->contentFrame())) {
            remoteFrame->client().findFocusableElementDescendingIntoRemoteFrame(direction, focusEventData, [](FoundElementInRemoteFrame found) {
                // FIXME: Implement sibling frame search by continuing here.
                UNUSED_PARAM(found);
            });

            return { nullptr, ContinuedSearchInRemoteFrame::Yes };
        }

        auto* localContentFrame = dynamicDowncast<LocalFrame>(owner->contentFrame());
        if (!localContentFrame || !localContentFrame->document())
            break;
        localContentFrame->protectedDocument()->updateLayoutIgnorePendingStylesheets();
        auto findResult = findFocusableElementWithinScope(direction, FocusNavigationScope::scopeOwnedByIFrame(*owner), nullptr, focusEventData);
        if (!findResult.element)
            break;
        ASSERT(element != findResult.element);
        element = findResult.element;
    }
    return { element };
}

bool FocusController::setInitialFocus(FocusDirection direction, KeyboardEvent* providedEvent)
{
    bool didAdvanceFocus = advanceFocus(direction, providedEvent, true);

    // If focus is being set initially, accessibility needs to be informed that system focus has moved 
    // into the web area again, even if focus did not change within WebCore. PostNotification is called instead
    // of handleFocusedUIElementChanged, because this will send the notification even if the element is the same.
    RefPtr focusedOrMainFrame = this->focusedOrMainFrame();
    if (CheckedPtr cache = focusedOrMainFrame ? focusedOrMainFrame->document()->existingAXObjectCache() : nullptr)
        cache->postNotification(focusedOrMainFrame->document(), AXNotification::FocusedUIElementChanged);

    return didAdvanceFocus;
}

bool FocusController::advanceFocus(FocusDirection direction, KeyboardEvent* event, bool initialFocus)
{
    FocusEventData focusEventData = event ? event->focusEventData() : FocusEventData { };

    switch (direction) {
    case FocusDirection::Forward:
    case FocusDirection::Backward:
        return advanceFocusInDocumentOrder(direction, focusEventData, initialFocus ? InitialFocus::Yes : InitialFocus::No);
    case FocusDirection::Left:
    case FocusDirection::Right:
    case FocusDirection::Up:
    case FocusDirection::Down:
        return advanceFocusDirectionally(direction, focusEventData);
    default:
        ASSERT_NOT_REACHED();
    }

    return false;
}

bool FocusController::relinquishFocusToChrome(FocusDirection direction)
{
    RefPtr frame = focusedOrMainFrame();
    if (!frame)
        return false;

    RefPtr document = frame->document();
    if (!document)
        return false;

    Ref page = m_page.get();
    if (!page->chrome().canTakeFocus(direction) || page->isControlledByAutomation())
        return false;

    clearSelectionIfNeeded(frame.get(), nullptr, nullptr);
    document->setFocusedElement(nullptr);
    setFocusedFrame(nullptr);
    page->chrome().takeFocus(direction);
    return true;
}

bool FocusController::advanceFocusInDocumentOrder(FocusDirection direction, const FocusEventData& focusEventData, InitialFocus initialFocus)
{
    RefPtr frame = focusedOrMainFrame();
    if (!frame)
        return false;

    RefPtr document = frame->document();
    if (!document)
        return false;

    RefPtr startingNode = document->focusNavigationStartingNode(direction);
    auto findResult = findAndFocusElementInDocumentOrderStartingWithFrame(*frame, startingNode, startingNode, direction, focusEventData, initialFocus, ContinuingRemoteSearch::No);

    return findResult.element;
}

FocusableElementSearchResult FocusController::findAndFocusElementInDocumentOrderStartingWithFrame(Ref<LocalFrame> frame, RefPtr<Node> scopeNode, RefPtr<Node> startingNode, FocusDirection direction, const FocusEventData& focusEventData, InitialFocus initialFocus, ContinuingRemoteSearch continuingRemoteSearch)
{
    RefPtr document = frame->document();
    RELEASE_ASSERT(document);

    // FIXME: Not quite correct when it comes to focus transitions leaving/entering the WebView itself
    bool caretBrowsing = frame->settings().caretBrowsingEnabled();

    if (caretBrowsing && !scopeNode)
        scopeNode = frame->selection().selection().start().deprecatedNode();

    if (continuingRemoteSearch == ContinuingRemoteSearch::No)
        document->updateLayoutIgnorePendingStylesheets();

    auto findResult = findFocusableElementAcrossFocusScope(direction, FocusNavigationScope::scopeOf(scopeNode ? *scopeNode : *document), startingNode.get(), focusEventData);
    if (findResult.continuedSearchInRemoteFrame == ContinuedSearchInRemoteFrame::Yes) {
        // In currently supported cases (e.g. descendant-frame-only search), the following steps occurs in the remote frame's WebContent process
        // FIXME: Make sure they happen in all cases (e.g. searching sibling frames)
        return findResult;
    }

    if (!findResult.element) {
        if (continuingRemoteSearch == ContinuingRemoteSearch::Yes)
            return findResult;

        // We didn't find a node to focus, so we should try to pass focus to Chrome.
        if (initialFocus == InitialFocus::No) {
            if (relinquishFocusToChrome(direction))
                return findResult;
        }

        // Chrome doesn't want focus, so we should wrap focus.
        RefPtr localTopDocument = m_page->localTopDocument();
        if (!localTopDocument)
            return findResult;
        findResult = findFocusableElementAcrossFocusScope(direction, FocusNavigationScope::scopeOf(*localTopDocument), nullptr, focusEventData);

        if (!findResult.element)
            return findResult;
    }
    RefPtr element = findResult.element;
    ASSERT(element);

    if (element == document->focusedElement()) {
        // Focus wrapped around to the same element.
        return findResult;
    }

    if (RefPtr owner = dynamicDowncast<HTMLFrameOwnerElement>(*element); owner && (!is<HTMLPlugInElement>(*element) || !element->isKeyboardFocusable(focusEventData))) {
        // We focus frames rather than frame owners.
        // FIXME: We should not focus frames that have no scrollbars, as focusing them isn't useful to the user.
        if (!owner->contentFrame())
            return findResult;

        document->setFocusedElement(nullptr);
        setFocusedFrame(owner->protectedContentFrame().get());
        return findResult;
    }
    
    // FIXME: It would be nice to just be able to call setFocusedElement(node) here, but we can't do
    // that because some elements (e.g. HTMLInputElement and HTMLTextAreaElement) do extra work in
    // their focus() methods.

    Document& newDocument = element->document();

    if (&newDocument != document) {
        // Focus is going away from this document, so clear the focused node.
        document->setFocusedElement(nullptr);
    }

    setFocusedFrame(newDocument.protectedFrame().get());

    if (caretBrowsing) {
        VisibleSelection newSelection(firstPositionInOrBeforeNode(element.get()), Affinity::Downstream);
        if (frame->selection().shouldChangeSelection(newSelection)) {
            AXTextStateChangeIntent intent(AXTextStateChangeTypeSelectionMove, AXTextSelection { AXTextSelectionDirectionDiscontiguous, AXTextSelectionGranularityUnknown, true });
            frame->selection().setSelection(newSelection, FrameSelection::defaultSetSelectionOptions(UserTriggered::Yes), intent);
        }
    }

    element->focus({ SelectionRestorationMode::SelectAll, direction, { }, { }, FocusVisibility::Visible });
    return findResult;
}

FocusableElementSearchResult FocusController::findFocusableElementAcrossFocusScope(FocusDirection direction, const FocusNavigationScope& scope, Node* currentNode, const FocusEventData& focusEventData)
{
    ASSERT(!is<Element>(currentNode) || !isNonFocusableScopeOwner(downcast<Element>(*currentNode), focusEventData));

    if (RefPtr currentElement = dynamicDowncast<Element>(currentNode); currentElement && direction == FocusDirection::Forward) {
        if (isFocusableScopeOwner(*currentElement, focusEventData)) {
            auto candidateInInnerScope = findFocusableElementWithinScope(direction, FocusNavigationScope::scopeOwnedByScopeOwner(*currentElement), nullptr, focusEventData);
            if (candidateInInnerScope.element)
                return candidateInInnerScope;
        } else if (auto* popover = openPopoverForInvoker(currentNode)) {
            auto candidateInInnerScope = findFocusableElementWithinScope(direction, FocusNavigationScope::scopeOwnedByScopeOwner(*popover), nullptr, focusEventData);
            if (candidateInInnerScope.element)
                return candidateInInnerScope;
        }
    }

    auto candidateInCurrentScope = findFocusableElementWithinScope(direction, scope, currentNode, focusEventData);
    if (candidateInCurrentScope.element) {
        if (direction == FocusDirection::Backward) {
            // Skip through invokers if they have popovers with focusable contents, and navigate through those contents instead.
            while (RefPtr popover = openPopoverForInvoker(candidateInCurrentScope.element.get())) {
                auto candidate = findFocusableElementWithinScope(direction, FocusNavigationScope::scopeOwnedByScopeOwner(*popover), nullptr, focusEventData);
                if (candidate.element)
                    candidateInCurrentScope = candidate;
                else
                    break;
            }
        }
        return candidateInCurrentScope;
    }

    // If there's no focusable node to advance to, move up the focus scopes until we find one.
    RefPtr owner = scope.owner();
    while (owner) {
        if (direction == FocusDirection::Backward && isFocusableScopeOwner(*owner, focusEventData))
            return findFocusableElementDescendingIntoSubframes(direction, owner.get(), focusEventData);

        // If we're getting out of a popover backwards, focus the invoker itself instead of the node preceding it, if possible.
        RefPtr invoker = invokerForOpenPopover(owner.get());
        if (invoker && direction == FocusDirection::Backward && invoker->isKeyboardFocusable(focusEventData))
            return { invoker.get() };

        auto outerScope = FocusNavigationScope::scopeOf(invoker ? *invoker : *owner);
        auto candidateInOuterScope = findFocusableElementWithinScope(direction, outerScope, invoker ? invoker.get() : owner.get(), focusEventData);
        if (candidateInOuterScope.element)
            return candidateInOuterScope;
        owner = outerScope.owner();
    }
    return candidateInCurrentScope;
}

FocusableElementSearchResult FocusController::findFocusableElementWithinScope(FocusDirection direction, const FocusNavigationScope& scope, Node* start, const FocusEventData& focusEventData)
{
    // Starting node is exclusive.
    auto candidate = direction == FocusDirection::Forward
        ? nextFocusableElementWithinScope(scope, start, focusEventData)
        : previousFocusableElementWithinScope(scope, start, focusEventData);
    return findFocusableElementDescendingIntoSubframes(direction, candidate.element.get(), focusEventData);
}

FocusableElementSearchResult FocusController::nextFocusableElementWithinScope(const FocusNavigationScope& scope, Node* start, const FocusEventData& focusEventData)
{
    auto* found = nextFocusableElementOrScopeOwner(scope, start, focusEventData);
    if (!found)
        return { nullptr };
    if (isNonFocusableScopeOwner(*found, focusEventData)) {
        auto foundInInnerFocusScope = nextFocusableElementWithinScope(FocusNavigationScope::scopeOwnedByScopeOwner(*found), 0, focusEventData);
        if (foundInInnerFocusScope.element)
            return foundInInnerFocusScope;
        return nextFocusableElementWithinScope(scope, found, focusEventData);
    }
    return { found };
}

FocusableElementSearchResult FocusController::previousFocusableElementWithinScope(const FocusNavigationScope& scope, Node* start, const FocusEventData& focusEventData)
{
    RefPtr found = previousFocusableElementOrScopeOwner(scope, start, focusEventData);
    if (!found)
        return { nullptr };
    if (isFocusableScopeOwner(*found, focusEventData)) {
        // Search an inner focusable element in the shadow tree from the end.
        auto foundInInnerFocusScope = previousFocusableElementWithinScope(FocusNavigationScope::scopeOwnedByScopeOwner(*found), 0, focusEventData);
        if (foundInInnerFocusScope.element)
            return foundInInnerFocusScope;
        return { found };
    }
    if (isNonFocusableScopeOwner(*found, focusEventData)) {
        auto foundInInnerFocusScope = previousFocusableElementWithinScope(FocusNavigationScope::scopeOwnedByScopeOwner(*found), 0, focusEventData);
        if (foundInInnerFocusScope.element)
            return foundInInnerFocusScope;
        return previousFocusableElementWithinScope(scope, found.get(), focusEventData);
    }
    return { found };
}

Element* FocusController::findFocusableElementOrScopeOwner(FocusDirection direction, const FocusNavigationScope& scope, Node* node, const FocusEventData& focusEventData)
{
    return (direction == FocusDirection::Forward)
        ? nextFocusableElementOrScopeOwner(scope, node, focusEventData)
        : previousFocusableElementOrScopeOwner(scope, node, focusEventData);
}

Element* FocusController::findElementWithExactTabIndex(const FocusNavigationScope& scope, Node* start, int tabIndex, const FocusEventData& focusEventData, FocusDirection direction)
{
    // Search is inclusive of start
    for (Node* node = start; node; node = direction == FocusDirection::Forward ? scope.nextInScope(node) : scope.previousInScope(node)) {
        auto* element = dynamicDowncast<Element>(*node);
        if (!element)
            continue;
        if (isFocusableElementOrScopeOwner(*element, focusEventData) && shadowAdjustedTabIndex(*element, focusEventData) == tabIndex)
            return element;
    }
    return nullptr;
}

static Element* nextElementWithGreaterTabIndex(const FocusNavigationScope& scope, int tabIndex, const FocusEventData& focusEventData)
{
    // Search is inclusive of start
    int winningTabIndex = std::numeric_limits<int>::max();
    Element* winner = nullptr;
    for (Node* node = scope.firstNodeInScope(); node; node = scope.nextInScope(node)) {
        auto* candidate = dynamicDowncast<Element>(*node);
        if (!candidate)
            continue;
        int candidateTabIndex = shadowAdjustedTabIndex(*candidate, focusEventData);
        if (isFocusableElementOrScopeOwner(*candidate, focusEventData) && candidateTabIndex > tabIndex && (!winner || candidateTabIndex < winningTabIndex)) {
            winner = candidate;
            winningTabIndex = candidateTabIndex;
        }
    }

    return winner;
}

static Element* previousElementWithLowerTabIndex(const FocusNavigationScope& scope, Node* start, int tabIndex, const FocusEventData& focusEventData)
{
    // Search is inclusive of start
    int winningTabIndex = 0;
    Element* winner = nullptr;
    for (Node* node = start; node; node = scope.previousInScope(node)) {
        auto* element = dynamicDowncast<Element>(*node);
        if (!element)
            continue;
        int currentTabIndex = shadowAdjustedTabIndex(*element, focusEventData);
        if (isFocusableElementOrScopeOwner(*element, focusEventData) && currentTabIndex < tabIndex && currentTabIndex > winningTabIndex) {
            winner = element;
            winningTabIndex = currentTabIndex;
        }
    }
    return winner;
}

FocusableElementSearchResult FocusController::nextFocusableElement(Node& start)
{
    // FIXME: This can return a non-focusable shadow host.
    // FIXME: This can't give the correct answer that takes modifier keys into account since it doesn't pass event data.
    return findFocusableElementAcrossFocusScope(FocusDirection::Forward, FocusNavigationScope::scopeOf(start), &start, { });
}

FocusableElementSearchResult FocusController::previousFocusableElement(Node& start)
{
    // FIXME: This can return a non-focusable shadow host.
    // FIXME: This can't give the correct answer that takes modifier keys into account since it doesn't pass event data.
    return findFocusableElementAcrossFocusScope(FocusDirection::Backward, FocusNavigationScope::scopeOf(start), &start, { });
}

Element* FocusController::nextFocusableElementOrScopeOwner(const FocusNavigationScope& scope, Node* start, const FocusEventData& focusEventData)
{
    int startTabIndex = 0;
    if (RefPtr element = dynamicDowncast<Element>(start))
        startTabIndex = shadowAdjustedTabIndex(*element, focusEventData);

    if (start) {
        // If a node is excluded from the normal tabbing cycle, the next focusable node is determined by tree order
        if (startTabIndex < 0) {
            for (Node* node = scope.nextInScope(start); node; node = scope.nextInScope(node)) {
                auto* element = dynamicDowncast<Element>(*node);
                if (!element)
                    continue;
                if (isFocusableElementOrScopeOwner(*element, focusEventData) && shadowAdjustedTabIndex(*element, focusEventData) >= 0)
                    return element;
            }
        }

        // First try to find a node with the same tabindex as start that comes after start in the scope.
        if (auto* winner = findElementWithExactTabIndex(scope, RefPtr { scope.nextInScope(start) }.get(), startTabIndex, focusEventData, FocusDirection::Forward))
            return winner;

        if (!startTabIndex)
            return nullptr; // We've reached the last node in the document with a tabindex of 0. This is the end of the tabbing order.
    }

    // Look for the first Element in the scope that:
    // 1) has the lowest tabindex that is higher than start's tabindex (or 0, if start is null), and
    // 2) comes first in the scope, if there's a tie.
    if (Element* winner = nextElementWithGreaterTabIndex(scope, startTabIndex, focusEventData))
        return winner;

    // There are no nodes with a tabindex greater than start's tabindex,
    // so find the first node with a tabindex of 0.
    return findElementWithExactTabIndex(scope, scope.firstNodeInScope(), 0, focusEventData, FocusDirection::Forward);
}

Element* FocusController::previousFocusableElementOrScopeOwner(const FocusNavigationScope& scope, Node* start, const FocusEventData& focusEventData)
{
    Node* last = nullptr;
    for (Node* node = scope.lastNodeInScope(); node; node = scope.lastChildInScope(*node))
        last = node;
    ASSERT(last);

    // First try to find the last node in the scope that comes before start and has the same tabindex as start.
    // If start is null, find the last node in the scope with a tabindex of 0.
    Node* startingNode;
    int startingTabIndex = 0;
    if (start) {
        startingNode = scope.previousInScope(start);
        if (RefPtr element = dynamicDowncast<Element>(*start))
            startingTabIndex = shadowAdjustedTabIndex(*element, focusEventData);
    } else
        startingNode = last;

    // However, if a node is excluded from the normal tabbing cycle, the previous focusable node is determined by tree order
    if (startingTabIndex < 0) {
        for (Node* node = startingNode; node; node = scope.previousInScope(node)) {
            auto* element = dynamicDowncast<Element>(*node);
            if (!element)
                continue;
            if (isFocusableElementOrScopeOwner(*element, focusEventData) && shadowAdjustedTabIndex(*element, focusEventData) >= 0)
                return element;
        }
    }

    if (auto* winner = findElementWithExactTabIndex(scope, startingNode, startingTabIndex, focusEventData, FocusDirection::Backward))
        return winner;

    // There are no nodes before start with the same tabindex as start, so look for a node that:
    // 1) has the highest non-zero tabindex (that is less than start's tabindex), and
    // 2) comes last in the scope, if there's a tie.
    startingTabIndex = (start && startingTabIndex) ? startingTabIndex : std::numeric_limits<int>::max();
    return previousElementWithLowerTabIndex(scope, last, startingTabIndex, focusEventData);
}

static bool relinquishesEditingFocus(Element& element)
{
    ASSERT(element.hasEditableStyle());

    auto root = element.rootEditableElement();
    auto frame = element.document().frame();
    if (!frame || !root)
        return false;

    return frame->editor().shouldEndEditing(makeRangeSelectingNodeContents(*root));
}

static bool shouldClearSelectionWhenChangingFocusedElement(const Page& page, RefPtr<Element> oldFocusedElement, RefPtr<Element> newFocusedElement)
{
#if PLATFORM(IOS_FAMILY) && ENABLE(DRAG_SUPPORT)
    if (newFocusedElement || !oldFocusedElement)
        return true;

    // FIXME: These additional checks should not be necessary. We should consider generally keeping the selection whenever the
    // focused element is blurred, with no new element taking focus.
    if (!oldFocusedElement->isRootEditableElement() && !is<HTMLInputElement>(oldFocusedElement) && !is<HTMLTextAreaElement>(oldFocusedElement))
        return true;

    RefPtr localMainFrame = dynamicDowncast<LocalFrame>(page.mainFrame());
    if (!localMainFrame) {
        LOG(SiteIsolation, "shouldClearSelectionWhenChangingFocusedElement - Encountered a non-local main frame which is not yet supported.");
        return false;
    }

    for (auto* ancestor = localMainFrame->eventHandler().draggedElement(); ancestor; ancestor = ancestor->parentOrShadowHostElement()) {
        if (ancestor == oldFocusedElement)
            return false;
    }
#else
    UNUSED_PARAM(page);
    UNUSED_PARAM(oldFocusedElement);
    UNUSED_PARAM(newFocusedElement);
#endif
    return true;
}

bool FocusController::setFocusedElement(Element* element, LocalFrame& newFocusedFrame, const FocusOptions& options)
{
    Ref protectedNewFocusedFrame { newFocusedFrame };
    RefPtr oldFocusedFrame { focusedLocalFrame() };
    RefPtr oldDocument = oldFocusedFrame ? oldFocusedFrame->document() : nullptr;
    
    RefPtr oldFocusedElement = oldDocument ? oldDocument->focusedElement() : nullptr;
    Ref page = m_page.get();
    if (oldFocusedElement == element) {
        if (element)
            page->chrome().client().elementDidRefocus(*element, options);
        return true;
    }

    // FIXME: Might want to disable this check for caretBrowsing
    if (oldFocusedElement && oldFocusedElement->isRootEditableElement() && !relinquishesEditingFocus(*oldFocusedElement))
        return false;

    if (shouldClearSelectionWhenChangingFocusedElement(page, WTFMove(oldFocusedElement), element))
        clearSelectionIfNeeded(oldFocusedFrame.get(), &newFocusedFrame, element);

    if (!element) {
        if (oldDocument)
            oldDocument->setFocusedElement(nullptr);
        page->editorClient().setInputMethodState(nullptr);
        return true;
    }

    Ref newDocument(element->document());

    if (newDocument->focusedElement() == element) {
        page->editorClient().setInputMethodState(element);
        return true;
    }
    
    if (oldDocument && oldDocument != newDocument.ptr())
        oldDocument->setFocusedElement(nullptr);

    if (!newFocusedFrame.page()) {
        setFocusedFrame(nullptr);
        return false;
    }
    setFocusedFrame(&newFocusedFrame);

    bool successfullyFocused = newDocument->setFocusedElement(element, options);
    if (!successfullyFocused)
        return false;

    if (newDocument->focusedElement() == element)
        page->editorClient().setInputMethodState(element);

    m_focusSetTime = MonotonicTime::now();
    m_focusRepaintTimer.stop();

    return true;
}

void FocusController::setActivityState(OptionSet<ActivityState> activityState)
{
    auto changed = m_activityState ^ activityState;
    m_activityState = activityState;

    if (changed & ActivityState::IsFocused)
        setFocusedInternal(activityState.contains(ActivityState::IsFocused));
    if (changed & ActivityState::WindowIsActive) {
        setActiveInternal(activityState.contains(ActivityState::WindowIsActive));
        if (changed & ActivityState::IsVisible)
            setIsVisibleAndActiveInternal(activityState.contains(ActivityState::WindowIsActive));
    }
}

Ref<Page> FocusController::protectedPage() const
{
    return m_page.get();
}

void FocusController::setActive(bool active)
{
    protectedPage()->setActivityState(active ? m_activityState | ActivityState::WindowIsActive : m_activityState - ActivityState::WindowIsActive);
}

void FocusController::setActiveInternal(bool active)
{
    RefPtr localMainFrame = m_page->localMainFrame();
    if (!localMainFrame)
        return;
    if (RefPtr view = localMainFrame->view()) {
        if (!view->platformWidget()) {
            view->updateLayoutAndStyleIfNeededRecursive();
            view->updateControlTints();
        }
    }

    if (RefPtr focusedOrMainFrame = this->focusedOrMainFrame())
        focusedOrMainFrame->selection().pageActivationChanged();
    
    auto* focusedFrame = focusedLocalFrame();
    if (focusedFrame && isFocused())
        dispatchEventsOnWindowAndFocusedElement(focusedFrame->protectedDocument().get(), active);
}

static void contentAreaDidShowOrHide(ScrollableArea* scrollableArea, bool didShow)
{
    if (didShow)
        scrollableArea->contentAreaDidShow();
    else
        scrollableArea->contentAreaDidHide();
}

void FocusController::setIsVisibleAndActiveInternal(bool contentIsVisible)
{
    Ref page = m_page.get();
    RefPtr view = page->mainFrame().virtualView();
    if (!view)
        return;

    contentAreaDidShowOrHide(view.get(), contentIsVisible);

    for (RefPtr frame = page->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(frame);
        if (!localFrame)
            continue;
        RefPtr frameView = localFrame->view();
        if (!frameView)
            continue;

        auto scrollableAreas = frameView->scrollableAreas();
        if (!scrollableAreas)
            continue;

        for (CheckedRef area : *scrollableAreas) {
            ASSERT(area->scrollbarsCanBeActive() || page->shouldSuppressScrollbarAnimations());
            contentAreaDidShowOrHide(area.ptr(), contentIsVisible);
        }
    }
}

static void updateFocusCandidateIfNeeded(FocusDirection direction, const FocusCandidate& current, FocusCandidate& candidate, FocusCandidate& closest)
{
    ASSERT(candidate.visibleNode->renderer());

    // Ignore iframes that don't have a src attribute
    if (frameOwnerElement(candidate) && (!frameOwnerElement(candidate)->contentFrame() || candidate.rect.isEmpty()))
        return;

    // Ignore off screen child nodes of containers that do not scroll (overflow:hidden)
    if (candidate.isOffscreen && !canBeScrolledIntoView(direction, candidate))
        return;

    distanceDataForNode(direction, current, candidate);
    if (candidate.distance == maxDistance())
        return;

    if (candidate.isOffscreenAfterScrolling && candidate.alignment < RectsAlignment::Full)
        return;

    if (closest.isNull()) {
        closest = candidate;
        return;
    }

    LayoutRect intersectionRect = intersection(candidate.rect, closest.rect);
    if (!intersectionRect.isEmpty() && !areElementsOnSameLine(closest, candidate)) {
        // If 2 nodes are intersecting, do hit test to find which node in on top.
        auto center = flooredIntPoint(intersectionRect.center()); // FIXME: Would roundedIntPoint be better?
        constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::Active, HitTestRequest::Type::IgnoreClipping, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::AllowChildFrameContent };
        auto* localMainFrame = dynamicDowncast<LocalFrame>(candidate.visibleNode->document().page()->mainFrame());
        if (!localMainFrame) {
            LOG(SiteIsolation, "updateFocusCandidateIfNeeded - Encountered a non-local main frame which is not yet supported.");
            return;
        }
        HitTestResult result = localMainFrame->eventHandler().hitTestResultAtPoint(center, hitType);
        if (candidate.visibleNode->contains(result.innerNode())) {
            closest = candidate;
            return;
        }
        if (closest.visibleNode->contains(result.innerNode()))
            return;
    }

    if (candidate.alignment == closest.alignment) {
        if (candidate.distance < closest.distance)
            closest = candidate;
        return;
    }

    if (candidate.alignment > closest.alignment)
        closest = candidate;
}

void FocusController::findFocusCandidateInContainer(const ContainerNode& container, const LayoutRect& startingRect, FocusDirection direction, const FocusEventData& focusEventData, FocusCandidate& closest)
{
    auto* focusedNode = (focusedLocalFrame() && focusedLocalFrame()->document()) ? focusedLocalFrame()->document()->focusedElement() : nullptr;

    Element* element = ElementTraversal::firstWithin(container);
    FocusCandidate current;
    current.rect = startingRect;
    current.focusableNode = focusedNode;
    current.visibleNode = focusedNode;

    unsigned candidateCount = 0;
    for (; element; element = (is<HTMLFrameOwnerElement>(*element) || canScrollInDirection(*element, direction)) ? ElementTraversal::nextSkippingChildren(*element, &container) : ElementTraversal::next(*element, &container)) {
        if (element == focusedNode)
            continue;

        if (!element->isKeyboardFocusable(focusEventData) && !is<HTMLFrameOwnerElement>(*element) && !canScrollInDirection(*element, direction))
            continue;

        FocusCandidate candidate = FocusCandidate(element, direction);
        if (candidate.isNull())
            continue;

        if (!isValidCandidate(direction, current, candidate))
            continue;

        candidateCount++;
        candidate.enclosingScrollableBox = container;
        updateFocusCandidateIfNeeded(direction, current, candidate, closest);
    }

    // The variable 'candidateCount' keeps track of the number of nodes traversed in a given container.
    // If we have more than one container in a page then the total number of nodes traversed is equal to the sum of nodes traversed in each container.
    auto* focusedFrame = focusedLocalFrame();
    if (focusedFrame && focusedFrame->document()) {
        candidateCount += focusedFrame->document()->page()->lastSpatialNavigationCandidateCount();
        focusedFrame->document()->page()->setLastSpatialNavigationCandidateCount(candidateCount);
    }
}

bool FocusController::advanceFocusDirectionallyInContainer(const ContainerNode& container, const LayoutRect& startingRect, FocusDirection direction, const FocusEventData& focusEventData)
{
    LayoutRect newStartingRect = startingRect;

    if (startingRect.isEmpty())
        newStartingRect = virtualRectForDirection(direction, nodeRectInAbsoluteCoordinates(container));

    // Find the closest node within current container in the direction of the navigation.
    FocusCandidate focusCandidate;
    findFocusCandidateInContainer(container, newStartingRect, direction, focusEventData, focusCandidate);

    if (focusCandidate.isNull()) {
        // Nothing to focus, scroll if possible.
        // NOTE: If no scrolling is performed (i.e. scrollInDirection returns false), the
        // spatial navigation algorithm will skip this container.
        return scrollInDirection(container, direction);
    }

    if (HTMLFrameOwnerElement* frameElement = frameOwnerElement(focusCandidate)) {
        // If we have an iframe without the src attribute, it will not have a contentFrame().
        // We ASSERT here to make sure that
        // updateFocusCandidateIfNeeded() will never consider such an iframe as a candidate.
        ASSERT(is<LocalFrame>(frameElement->contentFrame()));

        if (focusCandidate.isOffscreenAfterScrolling) {
            scrollInDirection(focusCandidate.visibleNode->protectedDocument(), direction);
            return true;
        }
        // Navigate into a new frame.
        LayoutRect rect;
        RefPtr focusedOrMainFrame = this->focusedOrMainFrame();
        RefPtr focusedElement = focusedOrMainFrame ? focusedOrMainFrame->document()->focusedElement() : nullptr;
        if (focusedElement && !hasOffscreenRect(*focusedElement))
            rect = nodeRectInAbsoluteCoordinates(*focusedElement, true /* ignore border */);
        dynamicDowncast<LocalFrame>(frameElement->contentFrame())->protectedDocument()->updateLayoutIgnorePendingStylesheets();
        if (!advanceFocusDirectionallyInContainer(*dynamicDowncast<LocalFrame>(frameElement->contentFrame())->document(), rect, direction, focusEventData)) {
            // The new frame had nothing interesting, need to find another candidate.
            RefPtr visibleNode = focusCandidate.visibleNode.get();
            return advanceFocusDirectionallyInContainer(container, nodeRectInAbsoluteCoordinates(*visibleNode, true), direction, focusEventData);
        }
        return true;
    }

    if (RefPtr visibleNode = focusCandidate.visibleNode.get(); visibleNode && canScrollInDirection(*visibleNode, direction)) {
        if (focusCandidate.isOffscreenAfterScrolling) {
            scrollInDirection(*visibleNode, direction);
            return true;
        }
        // Navigate into a new scrollable container.
        LayoutRect startingRect;
        RefPtr focusedOrMainFrame = this->focusedOrMainFrame();
        RefPtr focusedElement = focusedOrMainFrame ? focusedOrMainFrame->document()->focusedElement() : nullptr;
        if (focusedElement && !hasOffscreenRect(*focusedElement))
            startingRect = nodeRectInAbsoluteCoordinates(*focusedElement, true);
        return advanceFocusDirectionallyInContainer(*visibleNode, startingRect, direction, focusEventData);
    }
    if (focusCandidate.isOffscreenAfterScrolling) {
        RefPtr container = focusCandidate.enclosingScrollableBox.get();
        scrollInDirection(*container, direction);
        return true;
    }

    // We found a new focus node, navigate to it.
    RefPtr element = focusCandidate.focusableNode.get();
    element->focus({ SelectionRestorationMode::SelectAll, direction });
    return true;
}

bool FocusController::advanceFocusDirectionally(FocusDirection direction, const FocusEventData& focusEventData)
{
    RefPtr focusedOrMainFrame = this->focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return false;

    RefPtr focusedDocument = focusedOrMainFrame->document();
    if (!focusedDocument)
        return false;

    focusedDocument->updateLayoutIgnorePendingStylesheets();

    // Figure out the starting rect.
    RefPtr<ContainerNode> container = focusedDocument.get();
    LayoutRect startingRect;
    if (auto* focusedElement = focusedDocument->focusedElement()) {
        if (!hasOffscreenRect(*focusedElement)) {
            container = scrollableEnclosingBoxOrParentFrameForNodeInDirection(direction, *focusedElement);
            startingRect = nodeRectInAbsoluteCoordinates(*focusedElement, true /* ignore border */);
        } else if (auto* area = dynamicDowncast<HTMLAreaElement>(*focusedElement); area && area->imageElement()) {
            container = scrollableEnclosingBoxOrParentFrameForNodeInDirection(direction, *area->imageElement());
            startingRect = virtualRectForAreaElementAndDirection(area, direction);
        }
    }

    ASSERT(container);
    auto* focusedFrame = focusedLocalFrame();
    if (focusedFrame && focusedFrame->document())
        focusedDocument->page()->setLastSpatialNavigationCandidateCount(0);

    bool consumed = false;
    do {
        consumed = advanceFocusDirectionallyInContainer(*container, startingRect, direction, focusEventData);
        focusedDocument->updateLayoutIgnorePendingStylesheets();
        startingRect = nodeRectInAbsoluteCoordinates(*container, true /* ignore border */);
        container = scrollableEnclosingBoxOrParentFrameForNodeInDirection(direction, *container);
    } while (!consumed && container);

    return consumed;
}

void FocusController::setFocusedElementNeedsRepaint()
{
    m_focusRepaintTimer.startOneShot(33_ms);
}

void FocusController::focusRepaintTimerFired()
{
    RefPtr focusedOrMainFrame = this->focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return;

    RefPtr focusedDocument = focusedOrMainFrame->document();
    if (!focusedDocument)
        return;

    RefPtr focusedElement = focusedDocument->focusedElement();
    if (!focusedElement)
        return;

    if (focusedElement->renderer())
        focusedElement->renderer()->repaint();
}

Seconds FocusController::timeSinceFocusWasSet() const
{
    return MonotonicTime::now() - m_focusSetTime;
}

} // namespace WebCore
