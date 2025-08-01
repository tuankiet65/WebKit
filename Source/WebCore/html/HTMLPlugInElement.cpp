/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Stefan Schimanski (1Stein@gmx.de)
 * Copyright (C) 2004-2019 Apple Inc. All rights reserved.
 * Copyright (C) 2016 Google Inc. All rights reserved.
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
#include "HTMLPlugInElement.h"

#include "BridgeJSC.h"
#include "CSSPropertyNames.h"
#include "ContainerNodeInlines.h"
#include "Document.h"
#include "Event.h"
#include "EventHandler.h"
#include "FrameLoader.h"
#include "FrameTree.h"
#include "HTMLNames.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "Logging.h"
#include "MIMETypeRegistry.h"
#include "NodeName.h"
#include "Page.h"
#include "PluginData.h"
#include "PluginReplacement.h"
#include "PluginViewBase.h"
#include "RenderEmbeddedObject.h"
#include "RenderLayer.h"
#include "RenderStyleInlines.h"
#include "RenderView.h"
#include "RenderWidget.h"
#include "ScriptController.h"
#include "Settings.h"
#include "ShadowRoot.h"
#include "SubframeLoader.h"
#include "VoidCallback.h"
#include "Widget.h"
#include <wtf/TZoneMallocInlines.h>

#if PLATFORM(COCOA)
#include "YouTubePluginReplacement.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLPlugInElement);

using namespace HTMLNames;

HTMLPlugInElement::HTMLPlugInElement(const QualifiedName& tagName, Document& document, OptionSet<TypeFlag> typeFlags)
    : HTMLFrameOwnerElement(tagName, document, typeFlags | TypeFlag::HasCustomStyleResolveCallbacks)
    , m_swapRendererTimer(*this, &HTMLPlugInElement::swapRendererTimerFired)
{
}

HTMLPlugInElement::~HTMLPlugInElement()
{
    ASSERT(!m_instance); // cleared in detach()
    ASSERT(!m_pendingPDFTestCallback);
}

bool HTMLPlugInElement::willRespondToMouseClickEventsWithEditability(Editability) const
{
    if (isDisabledFormControl())
        return false;
    auto renderer = this->renderer();
    return renderer && renderer->isRenderWidget();
}

void HTMLPlugInElement::willDetachRenderers()
{
    m_instance = nullptr;

    if (m_isCapturingMouseEvents) {
        if (RefPtr frame = document().frame())
            frame->eventHandler().setCapturingMouseEventsElement(nullptr);
        m_isCapturingMouseEvents = false;
    }
}

void HTMLPlugInElement::resetInstance()
{
    m_instance = nullptr;
}

JSC::Bindings::Instance* HTMLPlugInElement::bindingsInstance()
{
    RefPtr frame = document().frame();
    if (!frame)
        return nullptr;

    // If the host dynamically turns off JavaScript (or Java) we will still return
    // the cached allocated Bindings::Instance.  Not supporting this edge-case is OK.

    if (!m_instance) {
        if (RefPtr widget = pluginWidget())
            m_instance = frame->script().createScriptInstanceForWidget(widget.get());
    }
    return m_instance.get();
}

PluginViewBase* HTMLPlugInElement::pluginWidget(PluginLoadingPolicy loadPolicy) const
{
    RenderWidget* renderWidget = loadPolicy == PluginLoadingPolicy::Load ? renderWidgetLoadingPlugin() : this->renderWidget();
    if (!renderWidget)
        return nullptr;

    return dynamicDowncast<PluginViewBase>(renderWidget->widget());
}

RenderWidget* HTMLPlugInElement::renderWidgetLoadingPlugin() const
{
    RefPtr view = document().view();
    if (!view || (!view->inUpdateEmbeddedObjects() && !view->layoutContext().isInLayout() && !view->isPainting())) {
        // Needs to load the plugin immediatedly because this function is called
        // when JavaScript code accesses the plugin.
        // FIXME: <rdar://16893708> Check if dispatching events here is safe.
        document().updateLayout({ LayoutOptions::IgnorePendingStylesheets, LayoutOptions::RunPostLayoutTasksSynchronously });
    }
    return renderWidget(); // This will return nullptr if the renderer is not a RenderWidget.
}

bool HTMLPlugInElement::hasPresentationalHintsForAttribute(const QualifiedName& name) const
{
    switch (name.nodeName()) {
    case AttributeNames::widthAttr:
    case AttributeNames::heightAttr:
    case AttributeNames::vspaceAttr:
    case AttributeNames::hspaceAttr:
    case AttributeNames::alignAttr:
        return true;
    default:
        break;
    }
    return HTMLFrameOwnerElement::hasPresentationalHintsForAttribute(name);
}

void HTMLPlugInElement::collectPresentationalHintsForAttribute(const QualifiedName& name, const AtomString& value, MutableStyleProperties& style)
{
    switch (name.nodeName()) {
    case AttributeNames::widthAttr:
        addHTMLLengthToStyle(style, CSSPropertyWidth, value);
        break;
    case AttributeNames::heightAttr:
        addHTMLLengthToStyle(style, CSSPropertyHeight, value);
        break;
    case AttributeNames::vspaceAttr:
        addHTMLLengthToStyle(style, CSSPropertyMarginTop, value);
        addHTMLLengthToStyle(style, CSSPropertyMarginBottom, value);
        break;
    case AttributeNames::hspaceAttr:
        addHTMLLengthToStyle(style, CSSPropertyMarginLeft, value);
        addHTMLLengthToStyle(style, CSSPropertyMarginRight, value);
        break;
    case AttributeNames::alignAttr:
        applyAlignmentAttributeToStyle(value, style);
        break;
    default:
        HTMLFrameOwnerElement::collectPresentationalHintsForAttribute(name, value, style);
        break;
    }
}

Node::InsertedIntoAncestorResult HTMLPlugInElement::insertedIntoAncestor(InsertionType insertionType, ContainerNode& parentOfInsertedTree)
{
    auto result = HTMLFrameOwnerElement::insertedIntoAncestor(insertionType, parentOfInsertedTree);
    if (insertionType.connectedToDocument)
        document().didConnectPluginElement();
    return result;
}

void HTMLPlugInElement::removedFromAncestor(RemovalType removalType, ContainerNode& oldParentOfRemovedTree)
{
    HTMLFrameOwnerElement::removedFromAncestor(removalType, oldParentOfRemovedTree);
    if (removalType.disconnectedFromDocument)
        document().didDisconnectPluginElement();
}

void HTMLPlugInElement::defaultEventHandler(Event& event)
{
    // Firefox seems to use a fake event listener to dispatch events to plug-in (tested with mouse events only).
    // This is observable via different order of events - in Firefox, event listeners specified in HTML attributes fires first, then an event
    // gets dispatched to plug-in, and only then other event listeners fire. Hopefully, this difference does not matter in practice.

    // FIXME: Mouse down and scroll events are passed down to plug-in via custom code in EventHandler; these code paths should be united.

    auto* renderer = dynamicDowncast<RenderWidget>(this->renderer());
    if (!renderer)
        return;

    if (CheckedPtr renderEmbedded = dynamicDowncast<RenderEmbeddedObject>(*renderer); renderEmbedded && renderEmbedded->isPluginUnavailable())
        renderEmbedded->handleUnavailablePluginIndicatorEvent(&event);

    if (RefPtr widget = renderer->widget())
        widget->handleEvent(event);
    if (event.defaultHandled())
        return;

    HTMLFrameOwnerElement::defaultEventHandler(event);
}

bool HTMLPlugInElement::isKeyboardFocusable(const FocusEventData& focusEventData) const
{
    if (HTMLFrameOwnerElement::isKeyboardFocusable(focusEventData))
        return true;
    return false;
}

bool HTMLPlugInElement::isPluginElement() const
{
    return true;
}

bool HTMLPlugInElement::supportsFocus() const
{
    if (HTMLFrameOwnerElement::supportsFocus())
        return true;

    if (useFallbackContent())
        return false;

    auto* renderer = dynamicDowncast<RenderEmbeddedObject>(this->renderer());
    return renderer && !renderer->isPluginUnavailable();
}

RenderPtr<RenderElement> HTMLPlugInElement::createElementRenderer(RenderStyle&& style, const RenderTreePosition& insertionPosition)
{
    if (m_pluginReplacement && m_pluginReplacement->willCreateRenderer()) {
        RenderPtr<RenderElement> renderer = m_pluginReplacement->createElementRenderer(*this, WTFMove(style), insertionPosition);
        if (renderer)
            renderer->markIsYouTubeReplacement();
        return renderer;
    }

    return createRenderer<RenderEmbeddedObject>(*this, WTFMove(style));
}

bool HTMLPlugInElement::isReplaced(const RenderStyle&) const
{
    return !m_pluginReplacement || !m_pluginReplacement->willCreateRenderer();
}

void HTMLPlugInElement::swapRendererTimerFired()
{
    ASSERT(displayState() == PreparingPluginReplacement);
    if (userAgentShadowRoot())
        return;
    
    // Create a shadow root, which will trigger the code to add a snapshot container
    // and reattach, thus making a new Renderer.
    ensureUserAgentShadowRoot();
}

void HTMLPlugInElement::setDisplayState(DisplayState state)
{
    if (state == m_displayState)
        return;

    m_displayState = state;

    m_swapRendererTimer.stop();
    if (displayState() == PreparingPluginReplacement)
        m_swapRendererTimer.startOneShot(0_s);
}

void HTMLPlugInElement::didAddUserAgentShadowRoot(ShadowRoot& root)
{
    if (!m_pluginReplacement || !document().page() || displayState() != PreparingPluginReplacement)
        return;
    
    m_pluginReplacement->installReplacement(root);

    setDisplayState(DisplayingPluginReplacement);
    invalidateStyleAndRenderersForSubtree();
}

#if PLATFORM(COCOA)
static void registrar(const ReplacementPlugin&);
#endif

static Vector<ReplacementPlugin*>& registeredPluginReplacements()
{
    static NeverDestroyed<Vector<ReplacementPlugin*>> registeredReplacements;
    static bool enginesQueried = false;
    
    if (enginesQueried)
        return registeredReplacements;
    enginesQueried = true;

#if PLATFORM(COCOA)
    YouTubePluginReplacement::registerPluginReplacement(registrar);
#endif
    
    return registeredReplacements;
}

#if PLATFORM(COCOA)
static void registrar(const ReplacementPlugin& replacement)
{
    registeredPluginReplacements().append(new ReplacementPlugin(replacement));
}
#endif

static ReplacementPlugin* pluginReplacementForType(const URL& url, const String& mimeType)
{
    Vector<ReplacementPlugin*>& replacements = registeredPluginReplacements();
    if (replacements.isEmpty())
        return nullptr;

    StringView extension;
    auto lastPathComponent = url.lastPathComponent();
    size_t dotOffset = lastPathComponent.reverseFind('.');
    if (dotOffset != notFound)
        extension = lastPathComponent.substring(dotOffset + 1);

    String type = mimeType;
    if (type.isEmpty() && url.protocolIsData())
        type = mimeTypeFromDataURL(url.string());
    
    if (type.isEmpty() && !extension.isEmpty()) {
        for (auto* replacement : replacements) {
            if (replacement->supportsFileExtension(extension) && replacement->supportsURL(url))
                return replacement;
        }
    }
    
    if (type.isEmpty()) {
        if (extension.isEmpty())
            return nullptr;
        type = MIMETypeRegistry::mediaMIMETypeForExtension(extension);
    }

    if (type.isEmpty())
        return nullptr;

    for (auto* replacement : replacements) {
        if (replacement->supportsType(type) && replacement->supportsURL(url))
            return replacement;
    }

    return nullptr;
}

bool HTMLPlugInElement::requestObject(const String& relativeURL, const String& mimeType, const Vector<AtomString>& paramNames, const Vector<AtomString>& paramValues)
{
    if (m_pluginReplacement)
        return true;

    URL completedURL;
    if (!relativeURL.isEmpty())
        completedURL = document().completeURL(relativeURL);

    ReplacementPlugin* replacement = pluginReplacementForType(completedURL, mimeType);
    if (!replacement)
        return false;

    LOG(Plugins, "%p - Found plug-in replacement for %s.", this, completedURL.string().utf8().data());

    m_pluginReplacement = replacement->create(*this, paramNames, paramValues);
    setDisplayState(PreparingPluginReplacement);
    return true;
}

bool HTMLPlugInElement::canLoadScriptURL(const URL&) const
{
    // FIXME: Probably want to at least check canAddSubframe.
    return true;
}

void HTMLPlugInElement::pluginDestroyedWithPendingPDFTestCallback(RefPtr<VoidCallback>&& callback)
{
    ASSERT(!m_pendingPDFTestCallback);
    m_pendingPDFTestCallback = WTFMove(callback);
}

RefPtr<VoidCallback> HTMLPlugInElement::takePendingPDFTestCallback()
{
    if (!m_pendingPDFTestCallback)
        return nullptr;
    return WTFMove(m_pendingPDFTestCallback);
}

}
