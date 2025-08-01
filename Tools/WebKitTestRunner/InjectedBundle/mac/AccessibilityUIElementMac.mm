/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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

#import "config.h"
#import "AccessibilityCommonCocoa.h"

#import "AccessibilityNotificationHandler.h"
#import "AccessibilityUIElement.h"
#import "InjectedBundle.h"
#import "InjectedBundlePage.h"
#import "JSBasics.h"
#import <AppKit/NSAccessibility.h>
#import <Foundation/Foundation.h>
#import <JavaScriptCore/JSStringRefCF.h>
#import <JavaScriptCore/JSObjectRef.h>
#import <WebCore/DateComponents.h>
#import <WebKit/WKBundleFrame.h>
#import <wtf/RetainPtr.h>
#import <wtf/Vector.h>
#import <wtf/cocoa/VectorCocoa.h>

#if HAVE(ACCESSIBILITY_FRAMEWORK)
#import <Accessibility/Accessibility.h>
#endif

#define NSAccessibilityDOMIdentifierAttribute @"AXDOMIdentifier"

#ifndef NSAccessibilityOwnsAttribute
#define NSAccessibilityOwnsAttribute @"AXOwns"
#endif

#ifndef NSAccessibilityGrabbedAttribute
#define NSAccessibilityGrabbedAttribute @"AXGrabbed"
#endif

#ifndef NSAccessibilityDropEffectsAttribute
#define NSAccessibilityDropEffectsAttribute @"AXDropEffects"
#endif

#ifndef NSAccessibilityPathAttribute
#define NSAccessibilityPathAttribute @"AXPath"
#endif

#ifndef NSAccessibilityARIACurrentAttribute
#define NSAccessibilityARIACurrentAttribute @"AXARIACurrent"
#endif

// Text
#ifndef NSAccessibilityEndTextMarkerForBoundsParameterizedAttribute
#define NSAccessibilityEndTextMarkerForBoundsParameterizedAttribute @"AXEndTextMarkerForBounds"
#endif

#ifndef NSAccessibilityStartTextMarkerForBoundsParameterizedAttribute
#define NSAccessibilityStartTextMarkerForBoundsParameterizedAttribute @"AXStartTextMarkerForBounds"
#endif

#ifndef NSAccessibilitySelectedTextMarkerRangeAttribute
#define NSAccessibilitySelectedTextMarkerRangeAttribute @"AXSelectedTextMarkerRange"
#endif

#ifndef NSAccessibilityTextInputMarkedRangeAttribute
#define NSAccessibilityTextInputMarkedRangeAttribute @"AXTextInputMarkedRange"
#endif

#ifndef NSAccessibilityTextInputMarkedTextMarkerRangeAttribute
#define NSAccessibilityTextInputMarkedTextMarkerRangeAttribute @"AXTextInputMarkedTextMarkerRange"
#endif

#ifndef NSAccessibilityIntersectionWithSelectionRangeAttribute
#define NSAccessibilityIntersectionWithSelectionRangeAttribute @"AXIntersectionWithSelectionRange"
#endif

typedef void (*AXPostedNotificationCallback)(id element, NSString* notification, void* context);

@interface NSObject (WebKitAccessibilityAdditions)
- (BOOL)accessibilityReplaceRange:(NSRange)range withText:(NSString *)string;
- (BOOL)accessibilityInsertText:(NSString *)text;
- (NSArray *)accessibilityArrayAttributeValues:(NSString *)attribute index:(NSUInteger)index maxCount:(NSUInteger)maxCount;
- (NSArray *)_accessibilityChildrenFromIndex:(NSUInteger)index maxCount:(NSUInteger)maxCount returnPlatformElements:(BOOL)returnPlatformElements;
- (NSUInteger)accessibilityIndexOfChild:(id)child;
- (NSUInteger)accessibilityArrayAttributeCount:(NSString *)attribute;
- (void)_accessibilityScrollToMakeVisibleWithSubFocus:(NSRect)rect;
- (void)_accessibilityScrollToGlobalPoint:(NSPoint)point;
- (void)_accessibilitySetValue:(id)value forAttribute:(NSString*)attributeName;
// For site-isolation testing use only.
- (id)_accessibilityHitTest:(NSPoint)point returnPlatformElements:(BOOL)returnPlatformElements;
- (void)_accessibilityHitTestResolvingRemoteFrame:(NSPoint)point callback:(void(^)(NSString *))callback;
@end

namespace WTR {

RefPtr<AccessibilityController> AccessibilityUIElement::s_controller;

AccessibilityUIElement::AccessibilityUIElement(id element)
    : m_element(element)
{
    if (!s_controller)
        s_controller = InjectedBundle::singleton().accessibilityController();
}

AccessibilityUIElement::AccessibilityUIElement(const AccessibilityUIElement& other)
    : JSWrappable()
    , m_element(other.m_element)
{
}

AccessibilityUIElement::~AccessibilityUIElement()
{
}

bool AccessibilityUIElement::isEqual(AccessibilityUIElement* otherElement)
{
    if (!otherElement)
        return false;
    return platformUIElement() == otherElement->platformUIElement();
}

RetainPtr<NSArray> supportedAttributes(id element)
{
    RetainPtr<NSArray> attributes;

    BEGIN_AX_OBJC_EXCEPTIONS
    AccessibilityUIElement::s_controller->executeOnAXThreadAndWait([&attributes, &element] {
        attributes = [element accessibilityAttributeNames];
    });
    END_AX_OBJC_EXCEPTIONS

    return attributes;
}

static id attributeValue(id element, NSString *attribute)
{
    // The given `element` may not respond to `accessibilityAttributeValue`, so first check to see if it responds to the attribute-specific selector.
    if ([attribute isEqual:NSAccessibilityChildrenAttribute] && [element respondsToSelector:@selector(accessibilityChildren)])
        return [element accessibilityChildren];
    if ([attribute isEqual:NSAccessibilityDescriptionAttribute] && [element respondsToSelector:@selector(accessibilityLabel)])
        return [element accessibilityLabel];
    if ([attribute isEqual:NSAccessibilityParentAttribute] && [element respondsToSelector:@selector(accessibilityParent)])
        return [element accessibilityParent];
    if ([attribute isEqual:NSAccessibilityRoleAttribute] && [element respondsToSelector:@selector(accessibilityRole)])
        return [element accessibilityRole];
    if ([attribute isEqual:NSAccessibilityValueAttribute] && [element respondsToSelector:@selector(accessibilityValue)])
        return [element accessibilityValue];
    if ([attribute isEqual:NSAccessibilityFocusedUIElementAttribute] && [element respondsToSelector:@selector(accessibilityFocusedUIElement)])
        return [element accessibilityFocusedUIElement];

    // These are internal APIs used by DRT/WKTR; tests are allowed to use them but we don't want to advertise them.
    static NeverDestroyed<RetainPtr<NSArray>> internalAttributes = @[
        @"AXARIAPressedIsPresent",
        @"AXARIARole",
        @"AXAutocompleteValue",
        @"AXClickPoint",
        @"AXControllerFor",
        @"AXControllers",
        @"AXDRTSpeechAttribute",
        @"AXDateTimeComponentsType",
        @"AXDescribedBy",
        @"AXDescriptionFor",
        @"AXDetailsFor",
        @"AXErrorMessageFor",
        @"AXFlowFrom",
        @"AXIsInCell",
        @"_AXIsInTable",
        @"AXIsInDescriptionListDetail",
        @"AXIsInDescriptionListTerm",
        @"AXIsIndeterminate",
        @"AXIsMultiSelectable",
        @"AXIsOnScreen",
        @"AXIsRemoteFrame",
        @"AXLabelFor",
        @"AXLabelledBy",
        @"AXLineRectsAndText",
        @"AXOwners",
        @"_AXPageRelativePosition",
        @"AXStringValue",
        @"AXValueAutofillType",

        // FIXME: these shouldn't be here, but removing one of these causes tests to fail.
        @"AXARIACurrent",
        @"AXARIALive",
        @"AXDescription",
        @"AXKeyShortcutsValue",
        @"AXOwns",
        @"AXPopupValue",
        @"AXValue",
    ];

    NSArray<NSString *> *supportedAttributes = [element accessibilityAttributeNames];
    if (![supportedAttributes containsObject:attribute] && ![internalAttributes.get() containsObject:attribute] && ![attribute isEqualToString:NSAccessibilityRoleAttribute])
        return nil;
    return [element accessibilityAttributeValue:attribute];
}

void setAttributeValue(id element, NSString* attribute, id value, bool synchronous = false)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    AccessibilityUIElement::s_controller->executeOnAXThreadAndWait([&element, &attribute, &value, &synchronous] {
        // FIXME: should always be asynchronous, fix tests.
        synchronous ? [element _accessibilitySetValue:value forAttribute:attribute] :
            [element accessibilitySetValue:value forAttribute:attribute];
    });
    END_AX_OBJC_EXCEPTIONS
}

RetainPtr<NSString> AccessibilityUIElement::descriptionOfValue(id valueObject) const
{
    if (!valueObject)
        return nil;

    if ([valueObject isKindOfClass:[NSArray class]])
        return [NSString stringWithFormat:@"<array of size %lu>", static_cast<unsigned long>([(NSArray*)valueObject count])];

    if ([valueObject isKindOfClass:[NSNumber class]])
        return [(NSNumber*)valueObject stringValue];

    if ([valueObject isKindOfClass:[NSValue class]]) {
        NSString *type = [NSString stringWithCString:[valueObject objCType] encoding:NSASCIIStringEncoding];
        NSValue *value = (NSValue *)valueObject;
        if ([type rangeOfString:@"NSRect"].length > 0)
            return [NSString stringWithFormat:@"NSRect: %@", NSStringFromRect([value rectValue])];
        if ([type rangeOfString:@"NSPoint"].length > 0)
            return [NSString stringWithFormat:@"NSPoint: %@", NSStringFromPoint([value pointValue])];
        if ([type rangeOfString:@"NSSize"].length > 0)
            return [NSString stringWithFormat:@"NSSize: %@", NSStringFromSize([value sizeValue])];
        if ([type rangeOfString:@"NSRange"].length > 0)
            return [NSString stringWithFormat:@"NSRange: %@", NSStringFromRange([value rangeValue])];
    }

    // Strip absolute URL paths
    NSString *description = [valueObject description];
    NSRange range = [description rangeOfString:@"LayoutTests"];
    if (range.length)
        return [description substringFromIndex:range.location];

    // Strip pointer locations
    if ([description rangeOfString:@"0x"].length) {
        auto role = attributeValue(NSAccessibilityRoleAttribute);
        auto title = attributeValue(NSAccessibilityTitleAttribute);
        if ([title length])
            return [NSString stringWithFormat:@"<%@: '%@'>", role.get(), title.get()];
        return [NSString stringWithFormat:@"<%@>", role.get()];
    }

    return [valueObject description];
}

static JSRetainPtr<JSStringRef> concatenateAttributeAndValue(NSString* attribute, NSString* value)
{
    Vector<UniChar> buffer([attribute length]);
    [attribute getCharacters:buffer.mutableSpan().data()];
    buffer.append(':');
    buffer.append(' ');

    Vector<UniChar> valueBuffer([value length]);
    [value getCharacters:valueBuffer.mutableSpan().data()];
    buffer.appendVector(valueBuffer);

    return adopt(JSStringCreateWithCharacters(buffer.span().data(), buffer.size()));
}

static JSRetainPtr<JSStringRef> descriptionOfElements(const Vector<RefPtr<AccessibilityUIElement>>& elements)
{
    NSMutableString *allElementString = [NSMutableString string];
    for (auto element : elements) {
        NSString *attributes = [NSString stringWithJSStringRef:element->allAttributes().get()];
        [allElementString appendFormat:@"%@\n------------\n", attributes];
    }

    return [allElementString createJSStringRef];
}

static NSDictionary *selectTextParameterizedAttributeForCriteria(JSContextRef context, JSStringRef ambiguityResolution, JSValueRef searchStrings, JSStringRef replacementString, JSStringRef activity)
{
    NSMutableDictionary *parameterizedAttribute = [NSMutableDictionary dictionary];
    
    if (ambiguityResolution)
        [parameterizedAttribute setObject:[NSString stringWithJSStringRef:ambiguityResolution] forKey:@"AXSelectTextAmbiguityResolution"];
    
    if (searchStrings) {
        NSMutableArray *searchStringsParameter = [NSMutableArray array];
        if (JSValueIsString(context, searchStrings))
            [searchStringsParameter addObject:toWTFString(context, searchStrings).createNSString().get()];
        else {
            JSObjectRef searchStringsArray = JSValueToObject(context, searchStrings, nullptr);
            unsigned searchStringsArrayLength = arrayLength(context, searchStringsArray);
            for (unsigned i = 0; i < searchStringsArrayLength; ++i)
                [searchStringsParameter addObject:toWTFString(context, JSObjectGetPropertyAtIndex(context, searchStringsArray, i, nullptr)).createNSString().get()];
        }
        [parameterizedAttribute setObject:searchStringsParameter forKey:@"AXSelectTextSearchStrings"];
    }
    
    if (replacementString) {
        [parameterizedAttribute setObject:@"AXSelectTextActivityFindAndReplace" forKey:@"AXSelectTextActivity"];
        [parameterizedAttribute setObject:[NSString stringWithJSStringRef:replacementString] forKey:@"AXSelectTextReplacementString"];
    } else
        [parameterizedAttribute setObject:@"AXSelectTextActivityFindAndSelect" forKey:@"AXSelectTextActivity"];
    
    if (activity)
        [parameterizedAttribute setObject:[NSString stringWithJSStringRef:activity] forKey:@"AXSelectTextActivity"];
    
    return parameterizedAttribute;
}

static NSDictionary *searchTextParameterizedAttributeForCriteria(JSContextRef context, JSValueRef searchStrings, JSStringRef startFrom, JSStringRef direction)
{
    NSMutableDictionary *parameterizedAttribute = [NSMutableDictionary dictionary];

    if (searchStrings) {
        NSMutableArray *searchStringsParameter = [NSMutableArray array];
        if (JSValueIsString(context, searchStrings))
            [searchStringsParameter addObject:toWTFString(context, searchStrings).createNSString().get()];
        else {
            JSObjectRef searchStringsArray = JSValueToObject(context, searchStrings, nullptr);
            unsigned searchStringsArrayLength = arrayLength(context, searchStringsArray);
            for (unsigned i = 0; i < searchStringsArrayLength; ++i)
                [searchStringsParameter addObject:toWTFString(context, JSObjectGetPropertyAtIndex(context, searchStringsArray, i, nullptr)).createNSString().get()];
        }
        [parameterizedAttribute setObject:searchStringsParameter forKey:@"AXSearchTextSearchStrings"];
    }

    if (startFrom)
        [parameterizedAttribute setObject:[NSString stringWithJSStringRef:startFrom] forKey:@"AXSearchTextStartFrom"];

    if (direction)
        [parameterizedAttribute setObject:[NSString stringWithJSStringRef:direction] forKey:@"AXSearchTextDirection"];

    return parameterizedAttribute;
}

static NSDictionary *textOperationParameterizedAttribute(JSContextRef context, JSStringRef operationType, JSValueRef markerRanges, JSValueRef replacementStrings, bool shouldSmartReplace)
{
    NSMutableDictionary *attributeParameters = [NSMutableDictionary dictionary];

    if (operationType)
        [attributeParameters setObject:[NSString stringWithJSStringRef:operationType] forKey:@"AXTextOperationType"];

    if (markerRanges) {
        JSObjectRef markerRangesArray = JSValueToObject(context, markerRanges, nullptr);
        unsigned markerRangesArrayLength = arrayLength(context, markerRangesArray);
        NSMutableArray *platformRanges = [NSMutableArray array];
        for (unsigned i = 0; i < markerRangesArrayLength; ++i) {
            auto propertyAtIndex = JSObjectGetPropertyAtIndex(context, markerRangesArray, i, nullptr);
            auto markerRangeRef = toTextMarkerRange(JSValueToObject(context, propertyAtIndex, nullptr));
            [platformRanges addObject:markerRangeRef->platformTextMarkerRange()];
        }
        [attributeParameters setObject:platformRanges forKey:@"AXTextOperationMarkerRanges"];
    }

    if (JSValueIsString(context, replacementStrings))
        [attributeParameters setObject:toWTFString(context, replacementStrings).createNSString().get() forKey:@"AXTextOperationReplacementString"];
    else {
        NSMutableArray *individualReplacementStringsParameter = [NSMutableArray array];
        JSObjectRef replacementStringsArray = JSValueToObject(context, replacementStrings, nullptr);
        unsigned replacementStringsArrayLength = arrayLength(context, replacementStringsArray);
        for (unsigned i = 0; i < replacementStringsArrayLength; ++i)
            [individualReplacementStringsParameter addObject:toWTFString(context, JSObjectGetPropertyAtIndex(context, replacementStringsArray, i, nullptr)).createNSString().get()];

        [attributeParameters setObject:individualReplacementStringsParameter forKey:@"AXTextOperationIndividualReplacementStrings"];
    }

    [attributeParameters setObject:[NSNumber numberWithBool:shouldSmartReplace] forKey:@"AXTextOperationSmartReplace"];

    return attributeParameters;
}

static NSDictionary *misspellingSearchParameterizedAttributeForCriteria(AccessibilityTextMarkerRange* start, bool forward)
{
    if (!start || !start->platformTextMarkerRange())
        return nil;

    NSMutableDictionary *parameters = [NSMutableDictionary dictionary];

    [parameters setObject:start->platformTextMarkerRange() forKey:@"AXStartTextMarkerRange"];
    [parameters setObject:[NSNumber numberWithBool:forward] forKey:@"AXSearchTextDirection"];

    return parameters;
}

RetainPtr<id> AccessibilityUIElement::attributeValue(NSString *attributeName) const
{
    RetainPtr<id> value;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([this, &attributeName, &value] {
        value = WTR::attributeValue(m_element.getAutoreleased(), attributeName);
    });
    END_AX_OBJC_EXCEPTIONS

    return value;
}

RetainPtr<id> AccessibilityUIElement::attributeValueForParameter(NSString *attributeName, id parameter) const
{
    RetainPtr<id> value;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([this, &attributeName, &parameter, &value] {
        value = [m_element accessibilityAttributeValue:attributeName forParameter:parameter];
    });
    END_AX_OBJC_EXCEPTIONS

    return value;
}

unsigned AccessibilityUIElement::arrayAttributeCount(NSString *attributeName) const
{
    unsigned count = 0;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([&attributeName, &count, this] {
        count = [m_element accessibilityArrayAttributeCount:attributeName];
    });
    END_AX_OBJC_EXCEPTIONS

    return count;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::domIdentifier() const
{
    return stringAttributeValueNS(NSAccessibilityDOMIdentifierAttribute);
}

void AccessibilityUIElement::getLinkedUIElements(Vector<RefPtr<AccessibilityUIElement>>& elementVector)
{
    elementVector = makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(NSAccessibilityLinkedUIElementsAttribute).get());
}

void AccessibilityUIElement::getDocumentLinks(Vector<RefPtr<AccessibilityUIElement>>& elementVector)
{
    elementVector = makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(@"AXLinkUIElements").get());
}

void AccessibilityUIElement::getUIElementsWithAttribute(JSStringRef attribute, Vector<RefPtr<AccessibilityUIElement>>& elements) const
{
    auto value = attributeValue([NSString stringWithJSStringRef:attribute]);
    if ([value isKindOfClass:[NSArray class]])
        elements = makeVector<RefPtr<AccessibilityUIElement>>(value.get());
}

JSValueRef AccessibilityUIElement::children(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(NSAccessibilityChildrenAttribute).get()));
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

Vector<RefPtr<AccessibilityUIElement>> AccessibilityUIElement::getChildren() const
{
    return makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(NSAccessibilityChildrenAttribute).get());
}

Vector<RefPtr<AccessibilityUIElement>> AccessibilityUIElement::getChildrenInRange(unsigned location, unsigned length) const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    RetainPtr<NSArray> children;
    s_controller->executeOnAXThreadAndWait([&children, location, length, this] {
        children = [m_element accessibilityArrayAttributeValues:NSAccessibilityChildrenAttribute index:location maxCount:length];
    });
    return makeVector<RefPtr<AccessibilityUIElement>>(children.get());
    END_AX_OBJC_EXCEPTIONS

    return { };
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::childAtIndexWithRemoteElement(unsigned index)
{
    RetainPtr<NSArray> children;
    s_controller->executeOnAXThreadAndWait([&children, index, this] {
        children = [m_element _accessibilityChildrenFromIndex:index maxCount:1 returnPlatformElements:NO];
    });
    auto resultChildren = makeVector<RefPtr<AccessibilityUIElement>>(children.get());
    return resultChildren.size() == 1 ? resultChildren[0] : nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::customContent() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
#if HAVE(ACCESSIBILITY_FRAMEWORK)
    auto customContent = adoptNS([[NSMutableArray alloc] init]);
    s_controller->executeOnAXThreadAndWait([this, &customContent] {
        for (AXCustomContent *content in [m_element accessibilityCustomContent])
            [customContent addObject:[NSString stringWithFormat:@"%@: %@", content.label, content.value]];
    });

    return [[customContent.get() componentsJoinedByString:@"\n"] createJSStringRef];
#else
    return nullptr;
#endif
    END_AX_OBJC_EXCEPTIONS
}

JSValueRef AccessibilityUIElement::rowHeaders(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    Vector<RefPtr<AccessibilityUIElement>> elements;
    auto value = attributeValue(NSAccessibilityRowHeaderUIElementsAttribute);
    if ([value isKindOfClass:[NSArray class]])
        elements = makeVector<RefPtr<AccessibilityUIElement>>(value.get());
    return makeJSArray(context, elements);
    END_AX_OBJC_EXCEPTIONS
}

JSValueRef AccessibilityUIElement::selectedCells(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilitySelectedCellsAttribute);
    if ([value isKindOfClass:[NSArray class]])
        return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(value.get()));
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

JSValueRef AccessibilityUIElement::columnHeaders(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    Vector<RefPtr<AccessibilityUIElement>> elements;
    auto value = attributeValue(NSAccessibilityColumnHeaderUIElementsAttribute);
    if ([value isKindOfClass:[NSArray class]])
        elements = makeVector<RefPtr<AccessibilityUIElement>>(value.get());
    return makeJSArray(context, elements);
    END_AX_OBJC_EXCEPTIONS
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::elementAtPoint(int x, int y)
{
    RetainPtr<id> element;
    s_controller->executeOnAXThreadAndWait([&x, &y, &element, this] {
        element = [m_element accessibilityHitTest:NSMakePoint(x, y)];
    });

    if (!element)
        return nullptr;

    return AccessibilityUIElement::create(element.get());
}

RefPtr<AccessibilityUIElement>  AccessibilityUIElement::elementAtPointWithRemoteElement(int x, int y)
{
    RetainPtr<id> element;
    s_controller->executeOnAXThreadAndWait([&x, &y, &element, this] {
        element = [m_element _accessibilityHitTest:NSMakePoint(x, y) returnPlatformElements:NO];
    });

    if (!element)
        return nullptr;

    return AccessibilityUIElement::create(element.get());
}

void AccessibilityUIElement::elementAtPointResolvingRemoteFrame(JSContextRef context, int x, int y, JSValueRef jsCallback)
{
    JSValueProtect(context, jsCallback);
    s_controller->executeOnAXThreadAndWait([x, y, protectedThis = Ref { *this }, jsCallback = WTFMove(jsCallback), context = JSRetainPtr { JSContextGetGlobalContext(context) }] () mutable {
        auto callback = [jsCallback = WTFMove(jsCallback), context = WTFMove(context)](NSString *result) {
            s_controller->executeOnMainThread([result = WTFMove(result), jsCallback = WTFMove(jsCallback), context = WTFMove(context)] () {
                JSValueRef arguments[1];
                arguments[0] = makeValueRefForValue(context.get(), result);
                JSObjectCallAsFunction(context.get(), const_cast<JSObjectRef>(jsCallback), 0, 1, arguments, 0);
                JSValueUnprotect(context.get(), jsCallback);
            });
        };

        [protectedThis->m_element _accessibilityHitTestResolvingRemoteFrame:NSMakePoint(x, y) callback:WTFMove(callback)];
    });
}

unsigned AccessibilityUIElement::indexOfChild(AccessibilityUIElement* element)
{
    unsigned index;
    id platformElement = element->platformUIElement();
    s_controller->executeOnAXThreadAndWait([&platformElement, &index, this] {
        index = [m_element accessibilityIndexOfChild:platformElement];
    });
    return index;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::elementForAttribute(NSString *attribute) const
{
    auto element = attributeValue(attribute);
    return element ? AccessibilityUIElement::create(element.get()) : RefPtr<AccessibilityUIElement>();
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::elementForAttributeAtIndex(NSString* attribute, unsigned index) const
{
    auto elements = attributeValue(attribute);
    return index < [elements count] ? AccessibilityUIElement::create([elements objectAtIndex:index]) : RefPtr<AccessibilityUIElement>();
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::linkedUIElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilityLinkedUIElementsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::controllerElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXControllers", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaControlsElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXControllerFor", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaDescribedByElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXDescribedBy", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::descriptionForElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXDescriptionFor", index);
}

JSValueRef AccessibilityUIElement::detailsElements(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto elements = attributeValue(@"AXDetailsElements");
    if ([elements isKindOfClass:NSArray.class])
        return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(elements.get()));
    END_AX_OBJC_EXCEPTIONS
    return { };
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaDetailsElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXDetailsElements", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::detailsForElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXDetailsFor", index);
}

JSValueRef AccessibilityUIElement::errorMessageElements(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto elements = attributeValue(@"AXErrorMessageElements");
    if ([elements isKindOfClass:NSArray.class])
        return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(elements.get()));
    END_AX_OBJC_EXCEPTIONS
    return { };
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaErrorMessageElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXErrorMessageElements", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::errorMessageForElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXErrorMessageFor", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::flowFromElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXFlowFrom", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaFlowToElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilityLinkedUIElementsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaLabelledByElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXLabelledBy", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::labelForElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXLabelFor", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ownerElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(@"AXOwners", index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::ariaOwnsElementAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilityOwnsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::disclosedRowAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilityDisclosedRowsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::rowAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilityRowsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::activeElement() const
{
    return elementForAttribute(@"AXActiveElement");
}

JSValueRef AccessibilityUIElement::selectedChildren(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto children = attributeValue(NSAccessibilitySelectedChildrenAttribute);
    if ([children isKindOfClass:NSArray.class])
        return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(children.get()));
    END_AX_OBJC_EXCEPTIONS
    return makeJSArray(context, { });
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::selectedChildAtIndex(unsigned index) const
{
    return elementForAttributeAtIndex(NSAccessibilitySelectedChildrenAttribute, index);
}

unsigned AccessibilityUIElement::selectedChildrenCount() const
{
    return arrayAttributeCount(NSAccessibilitySelectedChildrenAttribute);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::selectedRowAtIndex(unsigned index)
{
    return elementForAttributeAtIndex(NSAccessibilitySelectedRowsAttribute, index);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::titleUIElement()
{
    return elementForAttribute(NSAccessibilityTitleUIElementAttribute);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::parentElement()
{
    return elementForAttribute(NSAccessibilityParentAttribute);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::disclosedByRow()
{
    return elementForAttribute(NSAccessibilityDisclosedByRowAttribute);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfLinkedUIElements()
{
    Vector<RefPtr<AccessibilityUIElement> > linkedElements;
    getLinkedUIElements(linkedElements);
    return descriptionOfElements(linkedElements);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfDocumentLinks()
{
    Vector<RefPtr<AccessibilityUIElement> > linkElements;
    getDocumentLinks(linkElements);
    return descriptionOfElements(linkElements);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfChildren()
{
    return descriptionOfElements(getChildren());
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::allAttributes()
{
    auto attributes = supportedAttributes(m_element.getAutoreleased());

    NSMutableString *values = [NSMutableString string];
    for (NSString *attribute in attributes.get()) {
        // Exclude screen-specific values, since they can change depending on the system.
        if ([attribute isEqualToString:@"AXPosition"]
            || [attribute isEqualToString:@"_AXPrimaryScreenHeight"]
            || [attribute isEqualToString:@"AXRelativeFrame"])
            continue;

        if ([attribute isEqualToString:@"AXVisibleCharacterRange"]) {
            id value = attributeValue(NSAccessibilityRoleAttribute).get();
            NSString *role = [value isKindOfClass:[NSString class]] ? (NSString *)value : nil;
            if (role == nil || [role isEqualToString:@"AXList"] || [role isEqualToString:@"AXLink"] || [role isEqualToString:@"AXGroup"] || [role isEqualToString:@"AXRow"] || [role isEqualToString:@"AXColumn"] || [role isEqualToString:@"AXTable"] || [role isEqualToString:@"AXWebArea"]) {
                // For some roles, behavior with ITM on and ITM off differ for this API in ways
                // that are not clearly meaningful to any actual user-facing behavior. Skip dumping this
                // attribute for all of the "dump every attribute for every element" tests.
                // We can test visible-character-range in dedicated tests.
                continue;
            }
        }

        auto value = descriptionOfValue(attributeValue(attribute).get());

        if (!value
            && ([attribute isEqualToString:NSAccessibilityTextInputMarkedRangeAttribute]
                || [attribute isEqualToString:NSAccessibilityTextInputMarkedTextMarkerRangeAttribute]))
            continue;

        [values appendFormat:@"%@: %@\n", attribute, value.get()];
    }

    return [values createJSStringRef];
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringDescriptionOfAttributeValue(JSStringRef attribute)
{
    auto value = attributeValue([NSString stringWithJSStringRef:attribute]);
    auto valueDescription = descriptionOfValue(value.get());
    return [valueDescription createJSStringRef];
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringAttributeValue(JSStringRef attribute)
{
    return stringAttributeValueNS([NSString stringWithJSStringRef:attribute]);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringAttributeValueNS(NSString *attribute) const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(attribute);
    if ([value isKindOfClass:[NSString class]])
        return [value createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

double AccessibilityUIElement::numberAttributeValue(JSStringRef attribute)
{
    return numberAttributeValueNS([NSString stringWithJSStringRef:attribute]);
}

double AccessibilityUIElement::numberAttributeValueNS(NSString *attribute) const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(attribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [value doubleValue];
    END_AX_OBJC_EXCEPTIONS

    return 0;
}

JSValueRef AccessibilityUIElement::uiElementArrayAttributeValue(JSContextRef context, JSStringRef attribute)
{
    Vector<RefPtr<AccessibilityUIElement>> elements;
    getUIElementsWithAttribute(attribute, elements);
    return makeJSArray(context, elements);
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::uiElementAttributeValue(JSStringRef attribute) const
{
    if (auto value = attributeValue([NSString stringWithJSStringRef:attribute]))
        return AccessibilityUIElement::create(value.get());
    return nullptr;
}

bool AccessibilityUIElement::boolAttributeValueNS(NSString *attribute) const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(attribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [value boolValue];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::boolAttributeValue(JSStringRef attribute)
{
    return boolAttributeValueNS([NSString stringWithJSStringRef:attribute]);
}

void AccessibilityUIElement::attributeValueAsync(JSContextRef context, JSStringRef attribute, JSValueRef callback)
{
    if (!attribute || !callback)
        return;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([attribute = retainPtr([NSString stringWithJSStringRef:attribute]), callback = WTFMove(callback), context = JSRetainPtr { JSContextGetGlobalContext(context) }, this] () mutable {
        id value = [m_element accessibilityAttributeValue:attribute.get()];
        if ([value isKindOfClass:[NSArray class]] || [value isKindOfClass:[NSDictionary class]])
            value = [value description];

        s_controller->executeOnMainThread([value = retainPtr(value), callback = WTFMove(callback), context = WTFMove(context)] () {
            JSValueRef arguments[1];
            arguments[0] = makeValueRefForValue(context.get(), value.get());
            JSObjectCallAsFunction(context.get(), const_cast<JSObjectRef>(callback), 0, 1, arguments, 0);
        });
    });
    END_AX_OBJC_EXCEPTIONS
}

void AccessibilityUIElement::setBoolAttributeValue(JSStringRef attribute, bool value)
{
    setAttributeValue(m_element.getAutoreleased(), [NSString stringWithJSStringRef:attribute], @(value), true);
}

void AccessibilityUIElement::setValue(JSStringRef value)
{
    setAttributeValue(m_element.getAutoreleased(), NSAccessibilityValueAttribute, [NSString stringWithJSStringRef:value]);
}

bool AccessibilityUIElement::isAttributeSettable(JSStringRef attribute)
{
    return isAttributeSettableNS([NSString stringWithJSStringRef:attribute]);
}

bool AccessibilityUIElement::isAttributeSettableNS(NSString *attribute) const
{
    bool value = false;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([this, &attribute, &value] {
        value = [m_element accessibilityIsAttributeSettable:attribute];
    });
    END_AX_OBJC_EXCEPTIONS

    return value;
}

bool AccessibilityUIElement::isAttributeSupported(JSStringRef attribute)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return [supportedAttributes(m_element.getAutoreleased()) containsObject:[NSString stringWithJSStringRef:attribute]];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::parameterizedAttributeNames()
{
    NSArray *attributes = nil;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([&attributes, this] {
        attributes = [m_element accessibilityParameterizedAttributeNames];
    });
    END_AX_OBJC_EXCEPTIONS

    NSMutableString *attributesString = [NSMutableString string];
    for (id attribute in attributes)
        [attributesString appendFormat:@"%@\n", attribute];
    return [attributesString createJSStringRef];
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::role()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto role = descriptionOfValue(attributeValue(NSAccessibilityRoleAttribute).get());
    return concatenateAttributeAndValue(@"AXRole", role.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::subrole()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto subrole = descriptionOfValue(attributeValue(NSAccessibilitySubroleAttribute).get());
    return concatenateAttributeAndValue(@"AXSubrole", subrole.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::roleDescription()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto role = descriptionOfValue(attributeValue(NSAccessibilityRoleDescriptionAttribute).get());
    return concatenateAttributeAndValue(@"AXRoleDescription", role.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::computedRoleString()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto computedRoleString = descriptionOfValue(attributeValue(@"AXARIARole").get());
    return [computedRoleString createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::title()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto title = descriptionOfValue(attributeValue(NSAccessibilityTitleAttribute).get());
    return concatenateAttributeAndValue(@"AXTitle", title.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::description()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(NSAccessibilityDescriptionAttribute).get());
    return concatenateAttributeAndValue(@"AXDescription", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::brailleLabel() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(@"AXBrailleLabel").get());
    return concatenateAttributeAndValue(@"AXBrailleLabel", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::brailleRoleDescription() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(@"AXBrailleRoleDescription").get());
    return concatenateAttributeAndValue(@"AXBrailleRoleDescription", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::liveRegionStatus() const
{
    return stringAttributeValueNS(@"AXARIALive");
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::liveRegionRelevant() const
{
    return stringAttributeValueNS(@"AXARIARelevant");
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::orientation() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(NSAccessibilityOrientationAttribute).get());
    return concatenateAttributeAndValue(@"AXOrientation", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringValue()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    RetainPtr<id> value;
    auto role = attributeValue(NSAccessibilityRoleAttribute);
    if ([role isEqualToString:@"AXDateTimeArea"])
        value = attributeValue(@"AXStringValue");
    else
        value = attributeValue(NSAccessibilityValueAttribute);

    if (auto description = descriptionOfValue(value.get()))
        return concatenateAttributeAndValue(@"AXValue", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::dateValue()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityValueAttribute);
    if (![value isKindOfClass:[NSDate class]])
        return nullptr;

    // Adjust the returned date per time zone and daylight savings in an equivalent way to what VoiceOver does.
    NSInteger offset = [[NSTimeZone localTimeZone] secondsFromGMTForDate:[NSDate date]];
    auto type = attributeValue(@"AXDateTimeComponentsType");
    if ([type unsignedShortValue] != (uint8_t)WebCore::DateComponentsType::DateTimeLocal && [[NSTimeZone localTimeZone] isDaylightSavingTimeForDate:[NSDate date]])
        offset -= 3600;
    value = [NSDate dateWithTimeInterval:offset sinceDate:value.get()];
    if (auto description = descriptionOfValue(value.get()))
        return concatenateAttributeAndValue(@"AXDateValue", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::dateTimeValue() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return stringAttributeValueNS(@"AXDateTimeValue");
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::language()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(@"AXLanguage").get());
    return concatenateAttributeAndValue(@"AXLanguage", description.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::helpText() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto description = descriptionOfValue(attributeValue(NSAccessibilityHelpAttribute).get());
    return concatenateAttributeAndValue(@"AXHelp", description.get());
    END_AX_OBJC_EXCEPTIONS
    
    return nullptr;
}

double AccessibilityUIElement::pageX()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(@"_AXPageRelativePosition");
    return static_cast<double>([positionValue pointValue].x);
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::pageY()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(@"_AXPageRelativePosition");
    return static_cast<double>([positionValue pointValue].y);
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::x()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(NSAccessibilityPositionAttribute);
    return static_cast<double>([positionValue pointValue].x);    
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::y()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(NSAccessibilityPositionAttribute);
    return static_cast<double>([positionValue pointValue].y);    
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::width()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto sizeValue = attributeValue(NSAccessibilitySizeAttribute);
    return static_cast<double>([sizeValue sizeValue].width);
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::height()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto sizeValue = attributeValue(NSAccessibilitySizeAttribute);
    return static_cast<double>([sizeValue sizeValue].height);
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::clickPointX()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(@"AXClickPoint");
    return static_cast<double>([positionValue pointValue].x);        
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

double AccessibilityUIElement::clickPointY()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto positionValue = attributeValue(@"AXClickPoint");
    return static_cast<double>([positionValue pointValue].y);
    END_AX_OBJC_EXCEPTIONS

    return 0.0f;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::lineRectsAndText() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto lineRectsAndText = attributeValue(@"AXLineRectsAndText");
    if (![lineRectsAndText isKindOfClass:NSArray.class])
        return { };
    return [[lineRectsAndText componentsJoinedByString:@"|"] createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return { };
}

double AccessibilityUIElement::intValue() const
{
    return numberAttributeValueNS(NSAccessibilityValueAttribute);
}

double AccessibilityUIElement::minValue()
{
    return numberAttributeValueNS(NSAccessibilityMinValueAttribute);
}

double AccessibilityUIElement::maxValue()
{
    return numberAttributeValueNS(NSAccessibilityMaxValueAttribute);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::valueDescription()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto valueDescription = attributeValue(NSAccessibilityValueDescriptionAttribute);
    if ([valueDescription isKindOfClass:[NSString class]])
        return concatenateAttributeAndValue(@"AXValueDescription", valueDescription.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

unsigned AccessibilityUIElement::numberOfCharacters() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityNumberOfCharactersAttribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [(NSNumber *)value unsignedIntValue];
    END_AX_OBJC_EXCEPTIONS
    return 0;
}

int AccessibilityUIElement::insertionPointLineNumber()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityInsertionPointLineNumberAttribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [(NSNumber *)value intValue]; 
    END_AX_OBJC_EXCEPTIONS

    return -1;
}

bool AccessibilityUIElement::isPressActionSupported()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return [actionNames() containsObject:NSAccessibilityPressAction];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::isIncrementActionSupported()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return [actionNames() containsObject:NSAccessibilityIncrementAction];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::isDecrementActionSupported()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return [actionNames() containsObject:NSAccessibilityDecrementAction];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::isAtomicLiveRegion() const
{
    return boolAttributeValueNS(@"AXARIAAtomic");
}

bool AccessibilityUIElement::isBusy() const
{
    return boolAttributeValueNS(@"AXElementBusy");
}

bool AccessibilityUIElement::isEnabled()
{
    return boolAttributeValueNS(NSAccessibilityEnabledAttribute);
}

bool AccessibilityUIElement::isRequired() const
{
    return boolAttributeValueNS(@"AXRequired");
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::focusedElement() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (auto focus = attributeValue(NSAccessibilityFocusedUIElementAttribute))
        return AccessibilityUIElement::create(focus.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::isFocused() const
{
    return boolAttributeValueNS(NSAccessibilityFocusedAttribute);
}

bool AccessibilityUIElement::isSelected() const
{
    auto value = attributeValue(NSAccessibilitySelectedAttribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [value boolValue];
    return false;
}

bool AccessibilityUIElement::isSelectedOptionActive() const
{
    return false;
}

bool AccessibilityUIElement::isIndeterminate() const
{
    return boolAttributeValueNS(@"AXIsIndeterminate");
}

bool AccessibilityUIElement::isExpanded() const
{
    return boolAttributeValueNS(NSAccessibilityExpandedAttribute);
}

bool AccessibilityUIElement::isChecked() const
{
    // On the Mac, intValue()==1 if a a checkable control is checked.
    return intValue() == 1;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::currentStateValue() const
{
    return stringAttributeValueNS(NSAccessibilityARIACurrentAttribute);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::sortDirection() const
{
    return stringAttributeValueNS(NSAccessibilitySortDirectionAttribute);
}

int AccessibilityUIElement::hierarchicalLevel() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityDisclosureLevelAttribute);
    if ([value isKindOfClass:[NSNumber class]])
        return [value intValue];
    END_AX_OBJC_EXCEPTIONS

    return 0;
}
    
JSRetainPtr<JSStringRef> AccessibilityUIElement::classList() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(@"AXDOMClassList");
    if (![value isKindOfClass:[NSArray class]])
        return nullptr;

    NSMutableString *classList = [NSMutableString string];
    NSInteger length = [value count];
    for (NSInteger k = 0; k < length; ++k) {
        [classList appendString:[value objectAtIndex:k]];
        if (k < length - 1)
            [classList appendString:@", "];
    }

    return [classList createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::speakAs()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(@"AXDRTSpeechAttribute");
    if ([value isKindOfClass:[NSArray class]])
        return [[(NSArray *)value componentsJoinedByString:@", "] createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::isGrabbed() const
{
    return boolAttributeValueNS(NSAccessibilityGrabbedAttribute);
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::ariaDropEffects() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityDropEffectsAttribute);
    if (![value isKindOfClass:[NSArray class]])
        return nullptr;

    NSMutableString *dropEffects = [NSMutableString string];
    NSInteger length = [value count];
    for (NSInteger k = 0; k < length; ++k) {
        [dropEffects appendString:[value objectAtIndex:k]];
        if (k < length - 1)
            [dropEffects appendString:@","];
    }

    return [dropEffects createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

// parameterized attributes
int AccessibilityUIElement::lineForIndex(int index)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(NSAccessibilityLineForIndexParameterizedAttribute, @(index));
    if ([value isKindOfClass:[NSNumber class]])
        return [(NSNumber *)value intValue]; 
    END_AX_OBJC_EXCEPTIONS

    return -1;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::rangeForLine(int line)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(NSAccessibilityRangeForLineParameterizedAttribute, @(line));
    if ([value isKindOfClass:[NSValue class]])
        return [NSStringFromRange([value rangeValue]) createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::rangeForPosition(int x, int y)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(NSAccessibilityRangeForPositionParameterizedAttribute, [NSValue valueWithPoint:NSMakePoint(x, y)]);
    if ([value isKindOfClass:[NSValue class]])
        return [NSStringFromRange([value rangeValue]) createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

static NSMutableString* makeBoundsDescription(NSRect rect, bool exposePosition)
{
    return [NSMutableString stringWithFormat:@"{{%f, %f}, {%f, %f}}", exposePosition ? rect.origin.x : -1.0f, exposePosition ? rect.origin.y : -1.0f, rect.size.width, rect.size.height];
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::boundsForRange(unsigned location, unsigned length)
{
    NSRange range = NSMakeRange(location, length);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(NSAccessibilityBoundsForRangeParameterizedAttribute, [NSValue valueWithRange:range]);
    NSRect rect = NSMakeRect(0,0,0,0);
    if ([value isKindOfClass:[NSValue class]])
        rect = [value rectValue]; 

    // don't return position information because it is platform dependent
    NSMutableString* boundsDescription = makeBoundsDescription(rect, false /* exposePosition */);
    return [boundsDescription createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::boundsForRangeWithPagePosition(unsigned location, unsigned length)
{
    NSRange range = NSMakeRange(location, length);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(@"_AXPageBoundsForTextMarkerRange", [NSValue valueWithRange:range]);
    NSRect rect = NSMakeRect(0, 0, 0, 0);
    if ([value isKindOfClass:[NSValue class]])
        rect = [value rectValue];

    NSMutableString* boundsDescription = makeBoundsDescription(rect, true /* exposePosition */);
    return [boundsDescription createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringForRange(unsigned location, unsigned length)
{
    NSRange range = NSMakeRange(location, length);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(NSAccessibilityStringForRangeParameterizedAttribute, [NSValue valueWithRange:range]);
    if (![string isKindOfClass:[NSString class]])
        return nullptr;

    return [string createJSStringRef];
    END_AX_OBJC_EXCEPTIONS
    
    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributedStringForRange(unsigned location, unsigned length)
{
    NSRange range = NSMakeRange(location, length);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(NSAccessibilityAttributedStringForRangeParameterizedAttribute, [NSValue valueWithRange:range]);
    if (![string isKindOfClass:[NSAttributedString class]])
        return nullptr;

    NSString* stringWithAttrs = [string description];
    return [stringWithAttrs createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::attributedStringRangeIsMisspelled(unsigned location, unsigned length)
{
    NSRange range = NSMakeRange(location, length);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(NSAccessibilityAttributedStringForRangeParameterizedAttribute, [NSValue valueWithRange:range]);
    if (![string isKindOfClass:[NSAttributedString class]])
        return false;

    NSDictionary* attrs = [string attributesAtIndex:0 effectiveRange:nil];
    BOOL misspelled = [[attrs objectForKey:NSAccessibilityMisspelledTextAttribute] boolValue];
#if PLATFORM(MAC)
    if (misspelled)
        misspelled = [[attrs objectForKey:NSAccessibilityMarkedMisspelledTextAttribute] boolValue];
#endif
    return misspelled;
    END_AX_OBJC_EXCEPTIONS

    return false;
}

unsigned AccessibilityUIElement::uiElementCountForSearchPredicate(JSContextRef context, AccessibilityUIElement *startElement, bool isDirectionNext, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameter = searchPredicateForSearchCriteria(context, startElement, nullptr, isDirectionNext, UINT_MAX, searchKey, searchText, visibleOnly, immediateDescendantsOnly);
    auto value = attributeValueForParameter(@"AXUIElementsForSearchPredicate", parameter);
    if ([value isKindOfClass:[NSArray class]])
        return [value count];
    END_AX_OBJC_EXCEPTIONS

    return 0;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::uiElementForSearchPredicate(JSContextRef context, AccessibilityUIElement *startElement, bool isDirectionNext, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameter = searchPredicateForSearchCriteria(context, startElement, nullptr, isDirectionNext, 1, searchKey, searchText, visibleOnly, immediateDescendantsOnly);
    auto searchResults = attributeValueForParameter(@"AXUIElementsForSearchPredicate", parameter);
    if (![searchResults isKindOfClass:[NSArray class]] || ![searchResults count])
        return nullptr;

    id result = [searchResults firstObject];
    if ([result isKindOfClass:NSDictionary.class]) {
        RELEASE_ASSERT([result objectForKey:@"AXSearchResultElement"]);
        return AccessibilityUIElement::create([result objectForKey:@"AXSearchResultElement"]);
    }
    return AccessibilityUIElement::create(result);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::selectTextWithCriteria(JSContextRef context, JSStringRef ambiguityResolution, JSValueRef searchStrings, JSStringRef replacementString, JSStringRef activity)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameterizedAttribute = selectTextParameterizedAttributeForCriteria(context, ambiguityResolution, searchStrings, replacementString, activity);
    auto result = attributeValueForParameter(@"AXSelectTextWithCriteria", parameterizedAttribute);
    if ([result isKindOfClass:[NSString class]])
        return [result.get() createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

#if PLATFORM(MAC)
JSValueRef AccessibilityUIElement::searchTextWithCriteria(JSContextRef context, JSValueRef searchStrings, JSStringRef startFrom, JSStringRef direction)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameterizedAttribute = searchTextParameterizedAttributeForCriteria(context, searchStrings, startFrom, direction);
    auto result = attributeValueForParameter(@"AXSearchTextWithCriteria", parameterizedAttribute);
    if ([result isKindOfClass:[NSArray class]])
        return makeJSArray(context, makeVector<RefPtr<AccessibilityTextMarkerRange>>(result.get()));
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSValueRef AccessibilityUIElement::performTextOperation(JSContextRef context, JSStringRef operationType, JSValueRef markerRanges, JSValueRef replacementStrings, bool shouldSmartReplace)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameterizedAttribute = textOperationParameterizedAttribute(context, operationType, markerRanges, replacementStrings, shouldSmartReplace);
    auto result = attributeValueForParameter(@"AXTextOperation", parameterizedAttribute);
    if ([result isKindOfClass:[NSArray class]])
        return makeValueRefForValue(context, result.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}
#endif

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfColumnHeaders()
{
    // not yet defined in AppKit... odd
    BEGIN_AX_OBJC_EXCEPTIONS
    auto columnHeaders = attributeValue(@"AXColumnHeaderUIElements");
    auto columnHeadersVector = makeVector<RefPtr<AccessibilityUIElement>>(columnHeaders.get());
    return descriptionOfElements(columnHeadersVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfRowHeaders()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto rowHeaders = attributeValue(@"AXRowHeaderUIElements");
    auto rowHeadersVector = makeVector<RefPtr<AccessibilityUIElement>>(rowHeaders.get());
    return descriptionOfElements(rowHeadersVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfColumns()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto columns = attributeValue(NSAccessibilityColumnsAttribute);
    auto columnsVector = makeVector<RefPtr<AccessibilityUIElement>>(columns.get());
    return descriptionOfElements(columnsVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSValueRef AccessibilityUIElement::columns(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(NSAccessibilityColumnsAttribute).get()));
    END_AX_OBJC_EXCEPTIONS
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfRows()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto rows = attributeValue(NSAccessibilityRowsAttribute);
    auto rowsVector = makeVector<RefPtr<AccessibilityUIElement>>(rows.get());
    return descriptionOfElements(rowsVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfVisibleCells()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto cells = attributeValue(@"AXVisibleCells");
    auto cellsVector = makeVector<RefPtr<AccessibilityUIElement>>(cells.get());
    return descriptionOfElements(cellsVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributesOfHeader()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto header = attributeValue(NSAccessibilityHeaderAttribute);
    if (!header)
        return [@"" createJSStringRef];

    Vector<RefPtr<AccessibilityUIElement>> headerVector;
    headerVector.append(AccessibilityUIElement::create(header.get()));
    return descriptionOfElements(headerVector);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

int AccessibilityUIElement::rowCount()
{
    return arrayAttributeCount(NSAccessibilityRowsAttribute);
}

int AccessibilityUIElement::columnCount()
{
    return arrayAttributeCount(NSAccessibilityColumnsAttribute);
}

int AccessibilityUIElement::indexInTable()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto indexNumber = attributeValue(NSAccessibilityIndexAttribute);
    if (indexNumber)
        return [indexNumber intValue];
    END_AX_OBJC_EXCEPTIONS

    return -1;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::rowIndexRange()
{
    NSRange range = NSMakeRange(0, 0);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto indexRange = attributeValue(@"AXRowIndexRange");
    if (indexRange)
        range = [indexRange rangeValue];
    NSMutableString *rangeDescription = [NSMutableString stringWithFormat:@"{%lu, %lu}", static_cast<unsigned long>(range.location), static_cast<unsigned long>(range.length)];
    return [rangeDescription createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::columnIndexRange()
{
    NSRange range = NSMakeRange(0, 0);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto indexRange = attributeValue(@"AXColumnIndexRange");
    if (indexRange)
        range = [indexRange rangeValue];
    NSMutableString *rangeDescription = [NSMutableString stringWithFormat:@"{%lu, %lu}", static_cast<unsigned long>(range.location), static_cast<unsigned long>(range.length)];
    return [rangeDescription createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::cellForColumnAndRow(unsigned col, unsigned row)
{
    NSArray *colRowArray = @[@(col), @(row)];
    BEGIN_AX_OBJC_EXCEPTIONS
    if (auto cell = attributeValueForParameter(@"AXCellForColumnAndRow", colRowArray))
        return AccessibilityUIElement::create(cell.get());
    END_AX_OBJC_EXCEPTIONS    

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::horizontalScrollbar() const
{
    if (!m_element)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    if (id scrollbar = attributeValue(NSAccessibilityHorizontalScrollBarAttribute).get())
        return AccessibilityUIElement::create(scrollbar);
    END_AX_OBJC_EXCEPTIONS    

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::verticalScrollbar() const
{
    if (!m_element)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    if (id scrollbar = attributeValue(NSAccessibilityVerticalScrollBarAttribute).get())
        return AccessibilityUIElement::create(scrollbar);
    END_AX_OBJC_EXCEPTIONS        

    return nullptr;
}

void AccessibilityUIElement::scrollToMakeVisible()
{
    performAction(@"AXScrollToVisible");
}

void AccessibilityUIElement::scrollToGlobalPoint(int x, int y)
{
    NSPoint point = NSMakePoint(x, y);
    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThread([point, this] {
        [m_element _accessibilityScrollToGlobalPoint:point];
    });
    END_AX_OBJC_EXCEPTIONS
}

void AccessibilityUIElement::scrollToMakeVisibleWithSubFocus(int x, int y, int width, int height)
{
    NSRect rect = NSMakeRect(x, y, width, height);
    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThread([rect, this] {
        [m_element _accessibilityScrollToMakeVisibleWithSubFocus:rect];
    });
    END_AX_OBJC_EXCEPTIONS
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::selectedText()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValue(@"AXSelectedText");
    if (![string isKindOfClass:[NSString class]])
        return nullptr;
    return [string createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::selectedTextRange()
{
    NSRange range = NSMakeRange(NSNotFound, 0);
    BEGIN_AX_OBJC_EXCEPTIONS
    auto indexRange = attributeValue(NSAccessibilitySelectedTextRangeAttribute);
    if (indexRange)
        range = [indexRange rangeValue];
    NSString *rangeDescription = [NSString stringWithFormat:@"{%lu, %lu}", static_cast<unsigned long>(range.location), static_cast<unsigned long>(range.length)];
    return [rangeDescription createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::intersectionWithSelectionRange()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (auto rangeAttribute = attributeValue(NSAccessibilityIntersectionWithSelectionRangeAttribute)) {
        NSRange range = [rangeAttribute rangeValue];
        NSString *rangeDescription = [NSString stringWithFormat:@"{%lu, %lu}", static_cast<unsigned long>(range.location), static_cast<unsigned long>(range.length)];
        return [rangeDescription createJSStringRef];
    }
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::setSelectedTextRange(unsigned location, unsigned length)
{
    NSRange textRange = NSMakeRange(location, length);
    NSValue *textRangeValue = [NSValue valueWithRange:textRange];
    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedTextRangeAttribute, textRangeValue);

    return true;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::textInputMarkedRange() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(NSAccessibilityTextInputMarkedRangeAttribute);
    if (value)
        return [NSStringFromRange([value rangeValue]) createJSStringRef];
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

bool AccessibilityUIElement::dismiss()
{
    return performAction(@"AXDismissAction");
}

bool AccessibilityUIElement::setSelectedTextMarkerRange(AccessibilityTextMarkerRange* markerRange)
{
    if (!markerRange)
        return false;

    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedTextMarkerRangeAttribute, markerRange->platformTextMarkerRange());

    return true;
}

void AccessibilityUIElement::increment()
{
    performAction(@"AXSyncIncrementAction");
}

void AccessibilityUIElement::decrement()
{
    performAction(@"AXSyncDecrementAction");
}

void AccessibilityUIElement::asyncIncrement()
{
    performAction(NSAccessibilityIncrementAction);
}

void AccessibilityUIElement::asyncDecrement()
{
    performAction(NSAccessibilityDecrementAction);
}

void AccessibilityUIElement::showMenu()
{
    performAction(NSAccessibilityShowMenuAction);
}

void AccessibilityUIElement::press()
{
    performAction(NSAccessibilityPressAction);
}

void AccessibilityUIElement::syncPress()
{
    performAction(@"AXSyncPressAction");
}

void AccessibilityUIElement::setSelectedChild(AccessibilityUIElement* element) const
{
    NSArray* array = element ? @[element->platformUIElement()] : @[];
    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedChildrenAttribute, array);
}

void AccessibilityUIElement::setSelectedChildAtIndex(unsigned index) const
{
    RefPtr<AccessibilityUIElement> element = const_cast<AccessibilityUIElement*>(this)->childAtIndex(index);
    if (!element)
        return;

    auto selectedChildren = attributeValue(NSAccessibilitySelectedChildrenAttribute);
    NSArray *array = @[element->platformUIElement()];
    if (selectedChildren)
        array = [selectedChildren arrayByAddingObjectsFromArray:array];

    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedChildrenAttribute, array, true);
}

void AccessibilityUIElement::removeSelectionAtIndex(unsigned index) const
{
    RefPtr<AccessibilityUIElement> element = const_cast<AccessibilityUIElement*>(this)->childAtIndex(index);
    if (!element)
        return;

    auto selectedChildren = attributeValue(NSAccessibilitySelectedChildrenAttribute);
    if (!selectedChildren)
        return;

    NSMutableArray *array = [NSMutableArray arrayWithArray:selectedChildren.get()];
    [array removeObject:element->platformUIElement()];

    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedChildrenAttribute, array, true);
}

void AccessibilityUIElement::clearSelectedChildren() const
{
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::accessibilityValue() const
{
    return createJSString();
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::url()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto url = attributeValue(NSAccessibilityURLAttribute);
    if ([url isKindOfClass:[NSURL class]])
        return [[url absoluteString] createJSStringRef];
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

bool AccessibilityUIElement::addNotificationListener(JSContextRef context, JSValueRef functionCallback)
{
    if (!functionCallback)
        return false;
 
    // Mac programmers should not be adding more than one notification listener per element.
    // Other platforms may be different.
    if (m_notificationHandler)
        return false;

    m_notificationHandler = adoptNS([[AccessibilityNotificationHandler alloc] initWithContext:context]);
    [m_notificationHandler setPlatformElement:platformUIElement()];
    [m_notificationHandler setCallback:functionCallback];
    [m_notificationHandler startObserving];

    return true;
}

bool AccessibilityUIElement::removeNotificationListener()
{
    // Mac programmers should not be trying to remove a listener that's already removed.
    ASSERT(m_notificationHandler);

    [m_notificationHandler stopObserving];
    m_notificationHandler = nil;
    
    return true;
}

bool AccessibilityUIElement::isFocusable() const
{
    return isAttributeSettableNS(NSAccessibilityFocusedAttribute);
}

bool AccessibilityUIElement::isSelectable() const
{
    return isAttributeSettableNS(NSAccessibilitySelectedAttribute);
}

bool AccessibilityUIElement::isMultiSelectable() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(@"AXIsMultiSelectable");
    if ([value isKindOfClass:[NSNumber class]])
        return [value boolValue];
    END_AX_OBJC_EXCEPTIONS
    return false;
}

bool AccessibilityUIElement::isVisible() const
{
    return false;
}

bool AccessibilityUIElement::isOnScreen() const
{
    auto value = attributeValue(@"AXIsOnScreen");
    if ([value isKindOfClass:[NSNumber class]])
        return [value boolValue];
    return false;
}

bool AccessibilityUIElement::isOffScreen() const
{
    return false;
}

bool AccessibilityUIElement::isCollapsed() const
{
    return false;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::embeddedImageDescription() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = descriptionOfValue(attributeValue(@"AXEmbeddedImageDescription").get());
    return concatenateAttributeAndValue(@"AXEmbeddedImageDescription", value.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

JSValueRef AccessibilityUIElement::imageOverlayElements(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(@"AXImageOverlayElements").get()));
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::isIgnored() const
{
    BOOL result = NO;
    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([&result, this] {
        result = m_element ? [m_element accessibilityIsIgnored] : YES;
    });
    END_AX_OBJC_EXCEPTIONS
    return result;
}

bool AccessibilityUIElement::isSingleLine() const
{
    return false;
}

bool AccessibilityUIElement::isMultiLine() const
{
    return false;
}

bool AccessibilityUIElement::hasPopup() const
{
    return boolAttributeValueNS(@"AXHasPopup");
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::popupValue() const
{
    if (auto result = stringAttributeValueNS(@"AXPopupValue"))
        return result;

    return [@"false" createJSStringRef];
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::focusableAncestor()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (id ancestor = attributeValue(@"AXFocusableAncestor").get())
        return AccessibilityUIElement::create(ancestor);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::editableAncestor()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (id ancestor = attributeValue(@"AXEditableAncestor").get())
        return AccessibilityUIElement::create(ancestor);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::highestEditableAncestor()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (id ancestor = attributeValue(@"AXHighestEditableAncestor").get())
        return AccessibilityUIElement::create(ancestor);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::isInDescriptionListDetail() const
{
    return boolAttributeValueNS(@"AXIsInDescriptionListDetail");
}

bool AccessibilityUIElement::isInDescriptionListTerm() const
{
    return boolAttributeValueNS(@"AXIsInDescriptionListTerm");
}

bool AccessibilityUIElement::isInCell() const
{
    return boolAttributeValueNS(@"AXIsInCell");
}

bool AccessibilityUIElement::isInTable() const
{
    return boolAttributeValueNS(@"_AXIsInTable");
}

void AccessibilityUIElement::takeFocus()
{
    setAttributeValue(m_element.getAutoreleased(), NSAccessibilityFocusedAttribute, @YES);
}

void AccessibilityUIElement::takeSelection()
{
}

void AccessibilityUIElement::addSelection()
{
}

void AccessibilityUIElement::removeSelection()
{
}

// Text markers
RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::lineTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXLineTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::rightLineTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXRightLineTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::leftLineTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXLeftLineTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::previousLineStartTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto marker = attributeValueForParameter(@"AXPreviousLineStartTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(marker.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::nextLineEndTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto marker = attributeValueForParameter(@"AXNextLineEndTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(marker.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

int AccessibilityUIElement::lineIndexForTextMarker(AccessibilityTextMarker* marker) const
{
    if (!marker)
        return -1;

    BEGIN_AX_OBJC_EXCEPTIONS
    return [attributeValueForParameter(@"AXLineForTextMarker", marker->platformTextMarker()) intValue];
    END_AX_OBJC_EXCEPTIONS

    return -1;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::styleTextMarkerRangeForTextMarker(AccessibilityTextMarker* marker)
{
    if (!marker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXStyleTextMarkerRangeForTextMarker", marker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForSearchPredicate(JSContextRef context, AccessibilityTextMarkerRange *startRange, bool forward, JSValueRef searchKey, JSStringRef searchText, bool visibleOnly, bool immediateDescendantsOnly)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameter = searchPredicateForSearchCriteria(context, nullptr, startRange, forward, 1, searchKey, searchText, visibleOnly, immediateDescendantsOnly);
    auto searchResults = attributeValueForParameter(@"AXRangesForSearchPredicate", parameter);
    if (![searchResults isKindOfClass:[NSArray class]] || ![searchResults count])
        return nullptr;

    id result = [searchResults firstObject];
    if (![result isKindOfClass:NSDictionary.class])
        return nullptr;

    id rangeRef = [result objectForKey:@"AXSearchResultRange"];
    if (rangeRef && CFGetTypeID((__bridge CFTypeRef)rangeRef) == AXTextMarkerRangeGetTypeID())
        return AccessibilityTextMarkerRange::create(rangeRef);
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::misspellingTextMarkerRange(AccessibilityTextMarkerRange* start, bool forward)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSDictionary *parameters = misspellingSearchParameterizedAttributeForCriteria(start, forward);
    auto textMarkerRange = attributeValueForParameter(@"AXMisspellingTextMarkerRange", parameters);
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForElement(AccessibilityUIElement* element)
{
    if (!element)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXTextMarkerRangeForUIElement", element->platformUIElement());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

int AccessibilityUIElement::textMarkerRangeLength(AccessibilityTextMarkerRange* range)
{
    if (!range)
        return 0;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto lengthValue = attributeValueForParameter(@"AXLengthForTextMarkerRange", range->platformTextMarkerRange());
    return [lengthValue intValue];
    END_AX_OBJC_EXCEPTIONS

    return 0;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::previousTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto previousMarker = attributeValueForParameter(@"AXPreviousTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(previousMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::nextTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto nextMarker = attributeValueForParameter(@"AXNextTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(nextMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForLine(long lineIndex)
{
    if (lineIndex < 0)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXTextMarkerRangeForLine", @(static_cast<unsigned>(lineIndex)));
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::stringForTextMarkerRange(AccessibilityTextMarkerRange* markerRange)
{
    if (!markerRange)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textString = attributeValueForParameter(@"AXStringForTextMarkerRange", markerRange->platformTextMarkerRange());
    return [textString createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::rectsForTextMarkerRange(AccessibilityTextMarkerRange*, JSStringRef)
{
    // Not implemented on macOS
    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForMarkers(AccessibilityTextMarker* startMarker, AccessibilityTextMarker* endMarker)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (!startMarker->platformTextMarker() || !endMarker->platformTextMarker())
        return nullptr;
    NSArray *textMarkers = @[startMarker->platformTextMarker(), endMarker->platformTextMarker()];
    auto textMarkerRange = attributeValueForParameter(@"AXTextMarkerRangeForTextMarkers", textMarkers);
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForUnorderedMarkers(AccessibilityTextMarker* startMarker, AccessibilityTextMarker* endMarker)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    if (!startMarker->platformTextMarker() || !endMarker->platformTextMarker())
        return nullptr;
    NSArray *textMarkers = @[startMarker->platformTextMarker(), endMarker->platformTextMarker()];
    auto textMarkerRange = attributeValueForParameter(@"AXTextMarkerRangeForUnorderedTextMarkers", textMarkers);
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textMarkerRangeForRange(unsigned location, unsigned length)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return AccessibilityTextMarkerRange::create(attributeValueForParameter(@"_AXTextMarkerRangeForNSRange",
        [NSValue valueWithRange:NSMakeRange(location, length)]).get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::selectedTextMarkerRange()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValue(NSAccessibilitySelectedTextMarkerRangeAttribute);
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

void AccessibilityUIElement::resetSelectedTextMarkerRange()
{
    auto start = attributeValue(@"AXStartTextMarker");
    if (!start)
        return;

    NSArray *textMarkers = @[start.get(), start.get()];
    auto textMarkerRange = attributeValueForParameter(@"AXTextMarkerRangeForUnorderedTextMarkers", textMarkers);
    if (!textMarkerRange)
        return;

    setAttributeValue(m_element.getAutoreleased(), NSAccessibilitySelectedTextMarkerRangeAttribute, textMarkerRange.get(), true);
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::textInputMarkedTextMarkerRange() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValue(NSAccessibilityTextInputMarkedTextMarkerRangeAttribute);
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::startTextMarkerForTextMarkerRange(AccessibilityTextMarkerRange* range)
{
    if (!range)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(@"_AXStartTextMarkerForTextMarkerRange", range->platformTextMarkerRange());
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::endTextMarkerForTextMarkerRange(AccessibilityTextMarkerRange* range)
{
    if (!range)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(@"_AXEndTextMarkerForTextMarkerRange", range->platformTextMarkerRange());
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::endTextMarkerForBounds(int x, int y, int width, int height)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(NSAccessibilityEndTextMarkerForBoundsParameterizedAttribute,
        [NSValue valueWithRect:NSMakeRect(x, y, width, height)]);
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::replaceTextInRange(JSStringRef string, int location, int length)
{
    bool result = false;

    BEGIN_AX_OBJC_EXCEPTIONS
    AccessibilityUIElement::s_controller->executeOnAXThreadAndWait([text = [NSString stringWithJSStringRef:string], range = NSMakeRange(location, length), this, &result] {
        result = [m_element accessibilityReplaceRange:range withText:text];
    });
    END_AX_OBJC_EXCEPTIONS

    return result;
}

bool AccessibilityUIElement::insertText(JSStringRef text)
{
    bool result = false;
    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([text = [NSString stringWithJSStringRef:text], &result, this] {
        result = [m_element accessibilityInsertText:text];
    });
    END_AX_OBJC_EXCEPTIONS
    return result;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::startTextMarkerForBounds(int x, int y, int width, int height)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(NSAccessibilityStartTextMarkerForBoundsParameterizedAttribute,
        [NSValue valueWithRect:NSMakeRect(x, y, width, height)]);
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::textMarkerForPoint(int x, int y)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(@"AXTextMarkerForPosition", [NSValue valueWithPoint:NSMakePoint(x, y)]);
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityUIElement> AccessibilityUIElement::accessibilityElementForTextMarker(AccessibilityTextMarker* marker)
{
    if (!marker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto uiElement = attributeValueForParameter(@"AXUIElementForTextMarker", marker->platformTextMarker());
    if (uiElement)
        return AccessibilityUIElement::create(uiElement.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

static NSString *descriptionForColor(CGColorRef color)
{
    // This is a hack to get an OK description for a CGColor by crudely parsing its debug description, e.g.:
    //
    // <CGColor 0x13bf07670> <CGColorSpace 0x12be0ce40> [ (kCGColorSpaceICCBased; kCGColorSpaceModelRGB; sRGB IEC61966-2.1)] ( 0 0 0 0 )
    // Ideally we convert it to a WebCore::Color and then call serializationForCSS(const Color&), but I can't
    // get that to link succesfully, despite these symbols being WEBCORE_EXPORTed.
    auto string = adoptNS([[NSMutableString alloc] init]);
    [string appendFormat:@"%@", color];
    NSArray *stringComponents = [string componentsSeparatedByString:@">"];
    if (stringComponents.count) {
        NSString *bracketsRemovedString = [[stringComponents objectAtIndex:stringComponents.count - 1] stringByReplacingOccurrencesOfString:@"]" withString:@""];
        return [bracketsRemovedString stringByReplacingOccurrencesOfString:@"headroom = 1.000000 " withString:@""];
    }
    return nil;
}

static void appendColorDescription(RetainPtr<NSMutableString> string, NSString* attributeKey, NSDictionary<NSString *, id> *attributes)
{
    id color = [attributes objectForKey:attributeKey];
    if (!color)
        return;

    if (CFGetTypeID(color) == CGColorGetTypeID())
        [string appendFormat:@"%@:%@\n", attributeKey, descriptionForColor((CGColorRef)color)];
}

static JSRetainPtr<JSStringRef> createJSStringRef(id string, bool includeDidSpellCheck)
{
    auto mutableString = adoptNS([[NSMutableString alloc] init]);
    id attributeEnumerationBlock = ^(NSDictionary<NSString *, id> *attributes, NSRange range, BOOL *stop) {
        [mutableString appendFormat:@"Attributes in range %@:\n", NSStringFromRange(range)];
        BOOL misspelled = [[attributes objectForKey:NSAccessibilityMisspelledTextAttribute] boolValue];
        if (misspelled)
            misspelled = [[attributes objectForKey:NSAccessibilityMarkedMisspelledTextAttribute] boolValue];
        if (misspelled)
            [mutableString appendString:@"Misspelled, "];
        if (includeDidSpellCheck) {
            BOOL didSpellCheck = [[attributes objectForKey:@"AXDidSpellCheck"] boolValue];
            [mutableString appendFormat:@"AXDidSpellCheck: %d\n", didSpellCheck];
        }
        id font = [attributes objectForKey:(__bridge id)kAXFontTextAttribute];
        if (font)
            [mutableString appendFormat:@"%@: %@\n", (__bridge id)kAXFontTextAttribute, font];

        appendColorDescription(mutableString, NSAccessibilityForegroundColorTextAttribute, attributes);
        appendColorDescription(mutableString, NSAccessibilityBackgroundColorTextAttribute, attributes);

        int scriptState = [[attributes objectForKey:NSAccessibilitySuperscriptTextAttribute] intValue];
        if (scriptState == -1) {
            // -1 == subscript
            [mutableString appendFormat:@"%@: -1\n", NSAccessibilitySuperscriptTextAttribute];
        } else if (scriptState == 1) {
            // 1 == superscript
            [mutableString appendFormat:@"%@: 1\n", NSAccessibilitySuperscriptTextAttribute];
        }

        BOOL hasTextShadow = [[attributes objectForKey:NSAccessibilityShadowTextAttribute] boolValue];
        if (hasTextShadow)
            [mutableString appendFormat:@"%@: YES\n", NSAccessibilityShadowTextAttribute];

        BOOL hasUnderline = [[attributes objectForKey:NSAccessibilityUnderlineTextAttribute] boolValue];
        if (hasUnderline) {
            [mutableString appendFormat:@"%@: YES\n", NSAccessibilityUnderlineTextAttribute];
            appendColorDescription(mutableString, NSAccessibilityUnderlineColorTextAttribute, attributes);
        }

        BOOL hasLinethrough = [[attributes objectForKey:NSAccessibilityStrikethroughTextAttribute] boolValue];
        if (hasLinethrough) {
            [mutableString appendFormat:@"%@: YES\n", NSAccessibilityStrikethroughTextAttribute];
            appendColorDescription(mutableString, NSAccessibilityStrikethroughColorTextAttribute, attributes);
        }

        id attachment = [attributes objectForKey:NSAccessibilityAttachmentTextAttribute];
        if (attachment)
            [mutableString appendFormat:@"%@: {present}\n", NSAccessibilityAttachmentTextAttribute];
    };
    [string enumerateAttributesInRange:NSMakeRange(0, [string length]) options:(NSAttributedStringEnumerationOptions)0 usingBlock:attributeEnumerationBlock];
    [mutableString appendString:[string string]];
    return [mutableString createJSStringRef];
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributedStringForTextMarkerRange(AccessibilityTextMarkerRange* markerRange)
{
    if (!markerRange)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(@"AXAttributedStringForTextMarkerRange", markerRange->platformTextMarkerRange());
    if ([string isKindOfClass:[NSAttributedString class]])
        return createJSStringRef(string.get(), /* IncludeDidSpellCheck */ false);
    END_AX_OBJC_EXCEPTIONS

    return nil;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributedStringForTextMarkerRangeWithDidSpellCheck(AccessibilityTextMarkerRange* markerRange)
{
    if (!markerRange)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(@"AXAttributedStringForTextMarkerRange", markerRange->platformTextMarkerRange());
    if ([string isKindOfClass:[NSAttributedString class]])
        return createJSStringRef(string.get(), /* IncludeDidSpellCheck */ true);
    END_AX_OBJC_EXCEPTIONS

    return nil;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::attributedStringForTextMarkerRangeWithOptions(AccessibilityTextMarkerRange* markerRange, bool includeSpellCheck)
{
    if (!markerRange || !markerRange->platformTextMarkerRange())
        return nullptr;

    id parameter = nil;
    if (includeSpellCheck)
        parameter = @{ @"AXSpellCheck": includeSpellCheck ? @YES : @NO, @"AXTextMarkerRange": markerRange->platformTextMarkerRange() };
    else
        parameter = markerRange->platformTextMarkerRange();

    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(@"AXAttributedStringForTextMarkerRangeWithOptions", parameter);
    if ([string isKindOfClass:[NSAttributedString class]])
        return createJSStringRef(string.get(), /* IncludeDidSpellCheck */ false);
    END_AX_OBJC_EXCEPTIONS

    return nil;
}

bool AccessibilityUIElement::attributedStringForTextMarkerRangeContainsAttribute(JSStringRef attribute, AccessibilityTextMarkerRange* range)
{
    if (!range)
        return false;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto string = attributeValueForParameter(@"AXAttributedStringForTextMarkerRange", range->platformTextMarkerRange());
    if (![string isKindOfClass:[NSAttributedString class]])
        return false;

    NSDictionary* attrs = [string attributesAtIndex:0 effectiveRange:nil];
    if ([attrs objectForKey:[NSString stringWithJSStringRef:attribute]])
        return true;    
    END_AX_OBJC_EXCEPTIONS

    return false;
}
    
int AccessibilityUIElement::indexForTextMarker(AccessibilityTextMarker* marker)
{
    if (!marker)
        return -1;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto indexNumber = attributeValueForParameter(@"AXIndexForTextMarker", marker->platformTextMarker());
    return [indexNumber intValue];
    END_AX_OBJC_EXCEPTIONS

    return -1;
}

bool AccessibilityUIElement::isTextMarkerNull(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return true;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(@"AXTextMarkerIsNull", textMarker->platformTextMarker());
    return [value boolValue];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::isTextMarkerValid(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return false;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValueForParameter(@"AXTextMarkerIsValid", textMarker->platformTextMarker());
    return [value boolValue];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

bool AccessibilityUIElement::isTextMarkerRangeValid(AccessibilityTextMarkerRange* textMarkerRange)
{
    if (!textMarkerRange)
        return false;

    BEGIN_AX_OBJC_EXCEPTIONS
    return [[m_element accessibilityAttributeValue:@"AXTextMarkerRangeIsValid" forParameter:textMarkerRange->platformTextMarkerRange()] boolValue];
    END_AX_OBJC_EXCEPTIONS

    return false;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::textMarkerForIndex(int textIndex)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValueForParameter(@"AXTextMarkerForIndex", [NSNumber numberWithInteger:textIndex]);
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::startTextMarker()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValue(@"AXStartTextMarker");
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::endTextMarker()
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarker = attributeValue(@"AXEndTextMarker");
    return AccessibilityTextMarker::create(textMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::leftWordTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXLeftWordTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::rightWordTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXRightWordTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::previousWordStartTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto previousWordStartMarker = attributeValueForParameter(@"AXPreviousWordStartTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(previousWordStartMarker.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::nextWordEndTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto nextWordEndMarker = attributeValueForParameter(@"AXNextWordEndTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(nextWordEndMarker.get());
    END_AX_OBJC_EXCEPTIONS
    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::paragraphTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXParagraphTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::previousParagraphStartTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto previousParagraphStartMarker = attributeValueForParameter(@"AXPreviousParagraphStartTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(previousParagraphStartMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::nextParagraphEndTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto nextParagraphEndMarker = attributeValueForParameter(@"AXNextParagraphEndTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(nextParagraphEndMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarkerRange> AccessibilityUIElement::sentenceTextMarkerRangeForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto textMarkerRange = attributeValueForParameter(@"AXSentenceTextMarkerRangeForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarkerRange::create(textMarkerRange.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::previousSentenceStartTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto previousParagraphStartMarker = attributeValueForParameter(@"AXPreviousSentenceStartTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(previousParagraphStartMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

RefPtr<AccessibilityTextMarker> AccessibilityUIElement::nextSentenceEndTextMarkerForTextMarker(AccessibilityTextMarker* textMarker)
{
    if (!textMarker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    auto nextParagraphEndMarker = attributeValueForParameter(@"AXNextSentenceEndTextMarkerForTextMarker", textMarker->platformTextMarker());
    return AccessibilityTextMarker::create(nextParagraphEndMarker.get());
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::textMarkerDebugDescription(AccessibilityTextMarker* marker)
{
    if (!marker)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    RetainPtr description = attributeValueForParameter(@"AXTextMarkerDebugDescription", marker->platformTextMarker());
    return [description createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::textMarkerRangeDebugDescription(AccessibilityTextMarkerRange* range)
{
    if (!range)
        return nullptr;

    BEGIN_AX_OBJC_EXCEPTIONS
    RetainPtr description = attributeValueForParameter(@"AXTextMarkerRangeDebugDescription", range->platformTextMarkerRange());
    return [description createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

static NSString *_convertMathMultiscriptPairsToString(NSArray *pairs)
{
    __block NSMutableString *result = [NSMutableString string];
    [pairs enumerateObjectsUsingBlock:^(id pair, NSUInteger index, BOOL *stop) {
        for (NSString *key in pair) {
            auto element = AccessibilityUIElement::create([pair objectForKey:key]);
            auto subrole = element->attributeValue(NSAccessibilitySubroleAttribute);
            [result appendFormat:@"\t%lu. %@ = %@\n", (unsigned long)index, key, subrole.get()];
        }
    }];

    return result;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::mathPostscriptsDescription() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto pairs = attributeValue(@"AXMathPostscripts");
    return [_convertMathMultiscriptPairsToString(pairs.get()) createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::mathPrescriptsDescription() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto pairs = attributeValue(@"AXMathPrescripts");
    return [_convertMathMultiscriptPairsToString(pairs.get()) createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSValueRef AccessibilityUIElement::mathRootRadicand(JSContextRef context)
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return makeJSArray(context, makeVector<RefPtr<AccessibilityUIElement>>(attributeValue(@"AXMathRootRadicand").get()));
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::pathDescription() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    NSMutableString *result = [NSMutableString stringWithString:@"\nStart Path\n"];
    auto bezierPath = attributeValue(NSAccessibilityPathAttribute);

    NSUInteger elementCount = [bezierPath elementCount];
    NSPoint points[3];
    for (NSUInteger i = 0; i < elementCount; i++) {
        switch ([bezierPath elementAtIndex:i associatedPoints:points]) {
        case NSMoveToBezierPathElement:
            [result appendString:@"\tMove to point\n"];
            break;
        case NSLineToBezierPathElement:
            [result appendString:@"\tLine to\n"];
            break;
        case NSCurveToBezierPathElement:
            [result appendString:@"\tCurve to\n"];
            break;
        case NSClosePathBezierPathElement:
            [result appendString:@"\tClose\n"];
            break;
        default:
            break;
        }
    }

    return [result createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

NSArray *AccessibilityUIElement::actionNames() const
{
    NSArray *actions = nil;

    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThreadAndWait([this, &actions] {
        actions = [m_element accessibilityActionNames];
    });
    END_AX_OBJC_EXCEPTIONS

    return actions;
}

JSRetainPtr<JSStringRef> AccessibilityUIElement::supportedActions() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    return [[actionNames() componentsJoinedByString:@","] createJSStringRef];
    END_AX_OBJC_EXCEPTIONS

    return nullptr;
}

bool AccessibilityUIElement::performAction(NSString *actionName) const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    s_controller->executeOnAXThread([actionName, this] {
        [m_element accessibilityPerformAction:actionName];
    });
    END_AX_OBJC_EXCEPTIONS

    // macOS actions don't return a value.
    return true;
}

bool AccessibilityUIElement::isInsertion() const
{
    return false;
}

bool AccessibilityUIElement::isDeletion() const
{
    return false;
}

bool AccessibilityUIElement::isFirstItemInSuggestion() const
{
    return false;
}

bool AccessibilityUIElement::isLastItemInSuggestion() const
{
    return false;
}

bool AccessibilityUIElement::isRemoteFrame() const
{
    BEGIN_AX_OBJC_EXCEPTIONS
    auto value = attributeValue(@"AXIsRemoteFrame");
    if ([value isKindOfClass:[NSNumber class]])
        return [value boolValue];
    END_AX_OBJC_EXCEPTIONS
    return false;
}

} // namespace WTR
