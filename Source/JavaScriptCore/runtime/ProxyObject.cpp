/*
 * Copyright (C) 2016-2021 Apple Inc. All rights reserved.
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
#include "ProxyObject.h"

#include "GetterSetter.h"
#include "JSCInlines.h"
#include "JSInternalFieldObjectImplInlines.h"
#include "ObjectConstructor.h"
#include "VMInlines.h"
#include <algorithm>
#include <wtf/NoTailCalls.h>
#include <wtf/text/MakeString.h>

// Note that we use NO_TAIL_CALLS() throughout this file because we rely on the machine stack
// growing larger for throwing OOM errors for when we have an effectively cyclic prototype chain.

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(ProxyObject);

const ClassInfo ProxyObject::s_info = { "ProxyObject"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(ProxyObject) };

static JSC_DECLARE_HOST_FUNCTION(performProxyCall);
static JSC_DECLARE_HOST_FUNCTION(performProxyConstruct);

static constexpr PropertyOffset emptyHandlerTrapCache = INT_MIN;

ProxyObject::ProxyObject(VM& vm, Structure* structure)
    : Base(vm, structure)
{
    clearHandlerTrapsOffsetsCache();
}

Structure* ProxyObject::structureForTarget(JSGlobalObject* globalObject, JSValue target)
{
    return target.isCallable() ? globalObject->callableProxyObjectStructure() : globalObject->proxyObjectStructure();
}

void ProxyObject::finishCreation(VM& vm, JSGlobalObject* globalObject, JSValue target, JSValue handler)
{
    auto scope = DECLARE_THROW_SCOPE(vm);
    Base::finishCreation(vm);
    ASSERT(type() == ProxyObjectType);
    if (!target.isObject()) {
        throwTypeError(globalObject, scope, "A Proxy's 'target' should be an Object"_s);
        return;
    }
    if (!handler.isObject()) {
        throwTypeError(globalObject, scope, "A Proxy's 'handler' should be an Object"_s);
        return;
    }

    JSObject* targetAsObject = jsCast<JSObject*>(target);

    m_isCallable = targetAsObject->isCallable();
    if (m_isCallable) {
        TypeInfo info = structure()->typeInfo();
        RELEASE_ASSERT(info.implementsHasInstance() && info.implementsDefaultHasInstance());
    }

    m_isConstructible = targetAsObject->isConstructor();

    internalField(Field::Target).set(vm, this, targetAsObject);
    internalField(Field::Handler).set(vm, this, handler);
}

JSObject* ProxyObject::getHandlerTrap(JSGlobalObject* globalObject, JSObject* handler, CallData& callData, const Identifier& ident, HandlerTrap trap)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    auto ensureIsCallable = [&](JSValue value) -> JSObject* {
        if (value.isUndefinedOrNull())
            return nullptr;

        callData = JSC::getCallData(value);
        if (callData.type == CallData::Type::None) {
            throwTypeError(globalObject, scope, makeString('\'', String(ident.impl()), "' property of a Proxy's handler should be callable"_s));
            return nullptr;
        }

        return asObject(value);
    };

    if (isHandlerTrapsCacheValid(handler)) {
        PropertyOffset offset = m_handlerTrapsOffsetsCache[static_cast<uint8_t>(trap)];
        if (offset == invalidOffset)
            return nullptr;
        if (offset != emptyHandlerTrapCache)
            return ensureIsCallable(handler->getDirect(offset));
    } else if (m_handlerStructureID)
        clearHandlerTrapsOffsetsCache();

    PropertySlot slot(handler, PropertySlot::InternalMethodType::Get);
    bool hasProperty = handler->getPropertySlot(globalObject, ident, slot);
    RETURN_IF_EXCEPTION(scope, { });

    bool isSlotCacheable = slot.isUnset() || (slot.isCacheableValue() && slot.slotBase() == handler);
    if (isSlotCacheable) {
        JSValue handlerPrototype = handler->getPrototypeDirect();
        bool isHandlerPrototypeChainCacheable = handler->type() == FinalObjectType && !handler->structure()->isDictionary()
            && handlerPrototype.inherits<ObjectPrototype>() && !asObject(handlerPrototype)->structure()->isDictionary();
        if (isHandlerPrototypeChainCacheable) {
            ASSERT(slot.cachedOffset() != emptyHandlerTrapCache);
            m_handlerTrapsOffsetsCache[static_cast<uint8_t>(trap)] = slot.cachedOffset();
            m_handlerStructureID.set(vm, this, handler->structure());
            m_handlerPrototypeStructureID.set(vm, this, asObject(handlerPrototype)->structure());
        }
    }

    if (hasProperty) {
        JSValue trapValue = slot.getValue(globalObject, ident);
        RETURN_IF_EXCEPTION(scope, { });
        return ensureIsCallable(trapValue);
    }

    return nullptr;
}

void ProxyObject::clearHandlerTrapsOffsetsCache()
{
    std::fill_n(m_handlerTrapsOffsetsCache, numberOfCachedHandlerTrapsOffsets, emptyHandlerTrapCache);
}

static const ASCIILiteral s_proxyAlreadyRevokedErrorMessage { "Proxy has already been revoked. No more operations are allowed to be performed on it"_s };

static JSValue performProxyGet(JSGlobalObject* globalObject, ProxyObject* proxyObject, JSValue receiver, PropertyName propertyName)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return { };
    }

    JSObject* target = proxyObject->target();

    auto performDefaultGet = [&] {
        scope.release();
        PropertySlot slot(receiver, PropertySlot::InternalMethodType::Get);
        bool hasProperty = target->getPropertySlot(globalObject, propertyName, slot);
        EXCEPTION_ASSERT(!scope.exception() || !hasProperty);
        if (hasProperty)
            RELEASE_AND_RETURN(scope, slot.getValue(globalObject, propertyName));

        return jsUndefined();
    };

    if (propertyName.isPrivateName())
        return jsUndefined();

    JSValue handlerValue = proxyObject->handler();
    if (handlerValue.isNull())
        return throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSObject* getHandler = proxyObject->getHandlerTrap(globalObject, handler, callData, vm.propertyNames->get, ProxyObject::HandlerTrap::Get);
    RETURN_IF_EXCEPTION(scope, { });
    if (!getHandler)
        return performDefaultGet();

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    arguments.append(receiver.toThis(globalObject, ECMAMode::strict()));
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, getHandler, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, { });

    if (target->structure()->hasNonConfigurableReadOnlyOrGetterSetterProperties()) {
        ProxyObject::validateGetTrapResult(globalObject, trapResult, target, propertyName);
        RETURN_IF_EXCEPTION(scope, { });
    }

    return trapResult;
}

void ProxyObject::validateGetTrapResult(JSGlobalObject* globalObject, JSValue trapResult, JSObject* target, PropertyName propertyName)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    PropertyDescriptor descriptor;
    bool hasProperty = target->getOwnPropertyDescriptor(globalObject, propertyName, descriptor);
    RETURN_IF_EXCEPTION(scope, void());
    if (hasProperty && !descriptor.configurable()) {
        if (descriptor.isDataDescriptor() && !descriptor.writable()) {
            bool isSame = sameValue(globalObject, descriptor.value(), trapResult);
            RETURN_IF_EXCEPTION(scope, void());
            if (!isSame) {
                throwTypeError(globalObject, scope, "Proxy handler's 'get' result of a non-configurable and non-writable property should be the same value as the target's property"_s);
                return;
            }
        } else if (descriptor.isAccessorDescriptor() && descriptor.getter().isUndefined()) {
            if (!trapResult.isUndefined()) {
                throwTypeError(globalObject, scope, "Proxy handler's 'get' result of a non-configurable accessor property without a getter should be undefined"_s);
                return;
            }
        }
    }
}

bool ProxyObject::performGet(JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSValue result = performProxyGet(globalObject, this, slot.thisValue(), propertyName);
    RETURN_IF_EXCEPTION(scope, false);
    unsigned ignoredAttributes = 0;
    slot.setValue(this, ignoredAttributes, result);
    return true;
}

// https://tc39.es/ecma262/#sec-completepropertydescriptor
static void completePropertyDescriptor(PropertyDescriptor& desc)
{
    if (desc.isAccessorDescriptor()) {
        if (!desc.getter())
            desc.setGetter(jsUndefined());
        if (!desc.setter())
            desc.setSetter(jsUndefined());
    } else {
        if (!desc.value())
            desc.setValue(jsUndefined());
        if (!desc.writablePresent())
            desc.setWritable(false);
    }
    if (!desc.enumerablePresent())
        desc.setEnumerable(false);
    if (!desc.configurablePresent())
        desc.setConfigurable(false);
}

bool ProxyObject::performInternalMethodGetOwnProperty(JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }
    JSObject* target = this->target();

    auto performDefaultGetOwnProperty = [&] {
        return target->methodTable()->getOwnPropertySlot(target, globalObject, propertyName, slot);
    };

    if (propertyName.isPrivateName())
        return false;

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSObject* getOwnPropertyDescriptorMethod = getHandlerTrap(globalObject, handler, callData, vm.propertyNames->getOwnPropertyDescriptor, HandlerTrap::GetOwnPropertyDescriptor);
    RETURN_IF_EXCEPTION(scope, false);
    if (!getOwnPropertyDescriptorMethod)
        RELEASE_AND_RETURN(scope, performDefaultGetOwnProperty());

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, getOwnPropertyDescriptorMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    if (trapResult.isUndefined() && !target->structure()->isNonExtensibleOrHasNonConfigurableProperties())
        return false;

    if (!trapResult.isUndefined() && !trapResult.isObject()) {
        throwTypeError(globalObject, scope, "result of 'getOwnPropertyDescriptor' call should either be an Object or undefined"_s);
        return false;
    }

    PropertyDescriptor targetPropertyDescriptor;
    bool isTargetPropertyDescriptorDefined = target->getOwnPropertyDescriptor(globalObject, propertyName, targetPropertyDescriptor);
    RETURN_IF_EXCEPTION(scope, false);

    if (trapResult.isUndefined()) {
        if (!isTargetPropertyDescriptorDefined)
            return false;
        if (!targetPropertyDescriptor.configurable()) {
            throwTypeError(globalObject, scope, "When the result of 'getOwnPropertyDescriptor' is undefined the target must be configurable"_s);
            return false;
        }
        bool isExtensible = target->isExtensible(globalObject);
        RETURN_IF_EXCEPTION(scope, false);
        if (!isExtensible) {
            throwTypeError(globalObject, scope, "When 'getOwnPropertyDescriptor' returns undefined, the 'target' of a Proxy should be extensible"_s);
            return false;
        }

        return false;
    }

    bool isExtensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    PropertyDescriptor trapResultAsDescriptor;
    toPropertyDescriptor(globalObject, trapResult, trapResultAsDescriptor);
    RETURN_IF_EXCEPTION(scope, false);
    completePropertyDescriptor(trapResultAsDescriptor);
    bool throwException = false;
    bool valid = validateAndApplyPropertyDescriptor(globalObject, nullptr, propertyName, isExtensible,
        trapResultAsDescriptor, isTargetPropertyDescriptorDefined, targetPropertyDescriptor, throwException);
    RETURN_IF_EXCEPTION(scope, false);
    if (!valid) {
        throwTypeError(globalObject, scope, "Result from 'getOwnPropertyDescriptor' fails the IsCompatiblePropertyDescriptor test"_s);
        return false;
    }

    if (!trapResultAsDescriptor.configurable()) {
        if (!isTargetPropertyDescriptorDefined || targetPropertyDescriptor.configurable()) {
            throwTypeError(globalObject, scope, "Result from 'getOwnPropertyDescriptor' can't be non-configurable when the 'target' doesn't have it as an own property or if it is a configurable own property on 'target'"_s);
            return false;
        }
        if (trapResultAsDescriptor.writablePresent() && !trapResultAsDescriptor.writable() && targetPropertyDescriptor.writable()) {
            throwTypeError(globalObject, scope, "Result from 'getOwnPropertyDescriptor' can't be non-configurable and non-writable when the target's property is writable"_s);
            return false;
        }
    }

    if (trapResultAsDescriptor.isAccessorDescriptor()) {
        GetterSetter* getterSetter = trapResultAsDescriptor.slowGetterSetter(globalObject);
        RETURN_IF_EXCEPTION(scope, false);
        slot.setGetterSlot(this, trapResultAsDescriptor.attributes(), getterSetter);
    } else if (trapResultAsDescriptor.isDataDescriptor() && !trapResultAsDescriptor.value().isEmpty())
        slot.setValue(this, trapResultAsDescriptor.attributes(), trapResultAsDescriptor.value());
    else
        slot.setValue(this, trapResultAsDescriptor.attributes(), jsUndefined()); // We use undefined because it's the default value in object properties.

    return true;
}

bool ProxyObject::performHasProperty(JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }
    JSObject* target = this->target();
    slot.setValue(this, static_cast<unsigned>(PropertyAttribute::None), jsUndefined()); // Nobody should rely on our value, but be safe and protect against any bad actors reading our value.

    auto performDefaultHasProperty = [&] {
        return target->getPropertySlot(globalObject, propertyName, slot);
    };

    if (propertyName.isPrivateName())
        return false;

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSObject* hasMethod = getHandlerTrap(globalObject, handler, callData, vm.propertyNames->has, HandlerTrap::Has);
    RETURN_IF_EXCEPTION(scope, false);
    if (!hasMethod)
        RELEASE_AND_RETURN(scope, performDefaultHasProperty());

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, hasMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    if (trapResultAsBool)
        return true;

    if (target->structure()->isNonExtensibleOrHasNonConfigurableProperties()) {
        validateNegativeHasTrapResult(globalObject, target, propertyName);
        RETURN_IF_EXCEPTION(scope, false);
    }

    return false;
}

void ProxyObject::validateNegativeHasTrapResult(JSGlobalObject* globalObject, JSObject* target, PropertyName propertyName)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    PropertyDescriptor descriptor;
    bool isPropertyDescriptorDefined = target->getOwnPropertyDescriptor(globalObject, propertyName, descriptor); 
    RETURN_IF_EXCEPTION(scope, void());
    if (isPropertyDescriptorDefined) {
        if (!descriptor.configurable()) {
            throwTypeError(globalObject, scope, "Proxy 'has' must return 'true' for non-configurable properties"_s);
            return;
        }
        bool isExtensible = target->isExtensible(globalObject);
        RETURN_IF_EXCEPTION(scope, void());
        if (!isExtensible) {
            throwTypeError(globalObject, scope, "Proxy 'has' must return 'true' for a non-extensible 'target' object with a configurable property"_s);
            return;
        }
    }
}

bool ProxyObject::getOwnPropertySlotCommon(JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    slot.disableCaching();
    slot.setIsTaintedByOpaqueObject();

    if (slot.isVMInquiry()) {
        slot.setValue(this, static_cast<unsigned>(JSC::PropertyAttribute::None), jsUndefined());
        return false;
    }

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }
    switch (slot.internalMethodType()) {
    case PropertySlot::InternalMethodType::Get:
        RELEASE_AND_RETURN(scope, performGet(globalObject, propertyName, slot));
    case PropertySlot::InternalMethodType::GetOwnProperty:
        RELEASE_AND_RETURN(scope, performInternalMethodGetOwnProperty(globalObject, propertyName, slot));
    case PropertySlot::InternalMethodType::HasProperty:
        RELEASE_AND_RETURN(scope, performHasProperty(globalObject, propertyName, slot));
    default:
        return false;
    }

    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

bool ProxyObject::getOwnPropertySlot(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    ProxyObject* thisObject = jsCast<ProxyObject*>(object);
    return thisObject->getOwnPropertySlotCommon(globalObject, propertyName, slot);
}

bool ProxyObject::getOwnPropertySlotByIndex(JSObject* object, JSGlobalObject* globalObject, unsigned propertyName, PropertySlot& slot)
{
    VM& vm = globalObject->vm();
    ProxyObject* thisObject = jsCast<ProxyObject*>(object);
    Identifier ident = Identifier::from(vm, propertyName);
    return thisObject->getOwnPropertySlotCommon(globalObject, ident.impl(), slot);
}

template <typename PerformDefaultPutFunction>
bool ProxyObject::performPut(JSGlobalObject* globalObject, JSValue putValue, JSValue thisValue, PropertyName propertyName, PerformDefaultPutFunction performDefaultPut, bool shouldThrow)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    if (propertyName.isPrivateName())
        return false;

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSObject* setMethod = getHandlerTrap(globalObject, handler, callData, vm.propertyNames->set, HandlerTrap::Set);
    RETURN_IF_EXCEPTION(scope, false);
    JSObject* target = this->target();
    if (!setMethod)
        RELEASE_AND_RETURN(scope, performDefaultPut());

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    arguments.append(putValue);
    arguments.append(thisValue.toThis(globalObject, ECMAMode::strict()));
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, setMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    if (!trapResultAsBool) {
        if (shouldThrow)
            throwTypeError(globalObject, scope, makeString("Proxy object's 'set' trap returned falsy value for property '"_s, StringView(propertyName.uid()), '\''));
        return false;
    }

    if (target->structure()->hasNonConfigurableReadOnlyOrGetterSetterProperties()) {
        validatePositiveSetTrapResult(globalObject, target, propertyName, putValue);
        RETURN_IF_EXCEPTION(scope, false);
    }

    return true;
}

void ProxyObject::validatePositiveSetTrapResult(JSGlobalObject* globalObject, JSObject* target, PropertyName propertyName, JSValue putValue)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    PropertyDescriptor descriptor;
    bool hasProperty = target->getOwnPropertyDescriptor(globalObject, propertyName, descriptor);
    EXCEPTION_ASSERT(!scope.exception() || !hasProperty);
    if (hasProperty && !descriptor.configurable()) {
        if (descriptor.isDataDescriptor() && !descriptor.writable()) {
            bool isSame = sameValue(globalObject, descriptor.value(), putValue);
            RETURN_IF_EXCEPTION(scope, void());
            if (!isSame) {
                throwTypeError(globalObject, scope, "Proxy handler's 'set' on a non-configurable and non-writable property on 'target' should either return false or be the same value already on the 'target'"_s);
                return;
            }
        } else if (descriptor.isAccessorDescriptor() && descriptor.setter().isUndefined()) {
            throwTypeError(globalObject, scope, "Proxy handler's 'set' method on a non-configurable accessor property without a setter should return false"_s);
            return;
        }
    }
}

bool ProxyObject::put(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, JSValue value, PutPropertySlot& slot)
{
    slot.disableCaching();
    slot.setIsTaintedByOpaqueObject();

    ProxyObject* thisObject = jsCast<ProxyObject*>(cell);
    auto performDefaultPut = [&] () {
        JSObject* target = jsCast<JSObject*>(thisObject->target());
        return target->methodTable()->put(target, globalObject, propertyName, value, slot);
    };
    return thisObject->performPut(globalObject, value, slot.thisValue(), propertyName, performDefaultPut, slot.isStrictMode());
}

bool ProxyObject::putByIndexCommon(JSGlobalObject* globalObject, JSValue thisValue, unsigned propertyName, JSValue putValue, bool shouldThrow)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    Identifier ident = Identifier::from(vm, propertyName);
    RETURN_IF_EXCEPTION(scope, false);
    auto performDefaultPut = [&] () {
        JSObject* target = this->target();
        bool isStrictMode = shouldThrow;
        PutPropertySlot slot(thisValue, isStrictMode); // We must preserve the "this" target of the putByIndex.
        return target->methodTable()->put(target, globalObject, ident.impl(), putValue, slot);
    };
    RELEASE_AND_RETURN(scope, performPut(globalObject, putValue, thisValue, ident.impl(), performDefaultPut, shouldThrow));
}

bool ProxyObject::putByIndex(JSCell* cell, JSGlobalObject* globalObject, unsigned propertyName, JSValue value, bool shouldThrow)
{
    ProxyObject* thisObject = jsCast<ProxyObject*>(cell);
    return thisObject->putByIndexCommon(globalObject, thisObject, propertyName, value, shouldThrow);
}

JSC_DEFINE_HOST_FUNCTION(performProxyCall, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return encodedJSValue();
    }
    ProxyObject* proxy = jsCast<ProxyObject*>(callFrame->jsCallee());
    JSValue handlerValue = proxy->handler();
    if (handlerValue.isNull())
        return throwVMTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue applyMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "apply"_s), "'apply' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    JSObject* target = proxy->target();
    if (applyMethod.isUndefined()) {
        auto callData = JSC::getCallData(target);
        RELEASE_ASSERT(callData.type != CallData::Type::None);
        RELEASE_AND_RETURN(scope, JSValue::encode(call(globalObject, target, callData, callFrame->thisValue(), ArgList(callFrame))));
    }

    JSArray* argArray = constructArray(globalObject, static_cast<ArrayAllocationProfile*>(nullptr), ArgList(callFrame));
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(callFrame->thisValue().toThis(globalObject, ECMAMode::strict()));
    arguments.append(argArray);
    ASSERT(!arguments.hasOverflowed());
    RELEASE_AND_RETURN(scope, JSValue::encode(call(globalObject, applyMethod, callData, handler, arguments)));
}

CallData ProxyObject::getCallData(JSCell* cell)
{
    CallData callData;
    ProxyObject* proxy = jsCast<ProxyObject*>(cell);
    if (proxy->m_isCallable) {
        callData.type = CallData::Type::Native;
        callData.native.function = performProxyCall;
        callData.native.isBoundFunction = false;
        callData.native.isWasm = false;
    }
    return callData;
}

JSC_DEFINE_HOST_FUNCTION(performProxyConstruct, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return encodedJSValue();
    }
    ProxyObject* proxy = jsCast<ProxyObject*>(callFrame->jsCallee());
    JSValue handlerValue = proxy->handler();
    if (handlerValue.isNull())
        return throwVMTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue constructMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "construct"_s), "'construct' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    JSObject* target = proxy->target();
    if (constructMethod.isUndefined()) {
        auto constructData = JSC::getConstructData(target);
        RELEASE_ASSERT(constructData.type != CallData::Type::None);
        RELEASE_AND_RETURN(scope, JSValue::encode(construct(globalObject, target, constructData, ArgList(callFrame), callFrame->newTarget())));
    }

    JSArray* argArray = constructArray(globalObject, static_cast<ArrayAllocationProfile*>(nullptr), ArgList(callFrame));
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(argArray);
    arguments.append(callFrame->newTarget());
    ASSERT(!arguments.hasOverflowed());
    JSValue result = call(globalObject, constructMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());
    if (!result.isObject())
        return throwVMTypeError(globalObject, scope, "Result from Proxy handler's 'construct' method should be an object"_s);
    return JSValue::encode(result);
}

CallData ProxyObject::getConstructData(JSCell* cell)
{
    CallData constructData;
    ProxyObject* proxy = jsCast<ProxyObject*>(cell);
    if (proxy->m_isConstructible) {
        constructData.type = CallData::Type::Native;
        constructData.native.function = performProxyConstruct;
        constructData.native.isBoundFunction = false;
        constructData.native.isWasm = false;
    }
    return constructData;
}

template <typename DefaultDeleteFunction>
bool ProxyObject::performDelete(JSGlobalObject* globalObject, PropertyName propertyName, DefaultDeleteFunction performDefaultDelete)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    if (propertyName.isPrivateName())
        return false;

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue deletePropertyMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "deleteProperty"_s), "'deleteProperty' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, false);
    JSObject* target = this->target();
    if (deletePropertyMethod.isUndefined())
        RELEASE_AND_RETURN(scope, performDefaultDelete());

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, deletePropertyMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    if (!trapResultAsBool)
        return false;

    if (!target->structure()->isNonExtensibleOrHasNonConfigurableProperties())
        return true;

    PropertyDescriptor descriptor;
    bool result = target->getOwnPropertyDescriptor(globalObject, propertyName, descriptor);
    EXCEPTION_ASSERT(!scope.exception() || !result);
    if (result) {
        if (!descriptor.configurable()) {
            throwTypeError(globalObject, scope, "Proxy handler's 'deleteProperty' method should return false when the target's property is not configurable"_s);
            return false;
        }
        bool targetIsExtensible = target->isExtensible(globalObject);
        RETURN_IF_EXCEPTION(scope, false);
        if (!targetIsExtensible) {
            throwTypeError(globalObject, scope, "Proxy handler's 'deleteProperty' method should return false when the target has property and is not extensible"_s);
            return false;
        }
    }

    RETURN_IF_EXCEPTION(scope, false);

    return true;
}

bool ProxyObject::deleteProperty(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, DeletePropertySlot& slot)
{
    ProxyObject* thisObject = jsCast<ProxyObject*>(cell);
    auto performDefaultDelete = [&] () -> bool {
        JSObject* target = thisObject->target();
        return target->methodTable()->deleteProperty(target, globalObject, propertyName, slot);
    };
    return thisObject->performDelete(globalObject, propertyName, performDefaultDelete);
}

bool ProxyObject::deletePropertyByIndex(JSCell* cell, JSGlobalObject* globalObject, unsigned propertyName)
{
    VM& vm = globalObject->vm();
    ProxyObject* thisObject = jsCast<ProxyObject*>(cell);
    Identifier ident = Identifier::from(vm, propertyName);
    auto performDefaultDelete = [&] () -> bool {
        JSObject* target = thisObject->target();
        return target->methodTable()->deletePropertyByIndex(target, globalObject, propertyName);
    };
    return thisObject->performDelete(globalObject, ident.impl(), performDefaultDelete);
}

bool ProxyObject::performPreventExtensions(JSGlobalObject* globalObject)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue preventExtensionsMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "preventExtensions"_s), "'preventExtensions' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, false);
    JSObject* target = this->target();
    if (preventExtensionsMethod.isUndefined())
        RELEASE_AND_RETURN(scope, target->methodTable()->preventExtensions(target, globalObject));

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, preventExtensionsMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    if (trapResultAsBool) {
        bool targetIsExtensible = target->isExtensible(globalObject);
        RETURN_IF_EXCEPTION(scope, false);
        if (targetIsExtensible) {
            throwTypeError(globalObject, scope, "Proxy's 'preventExtensions' trap returned true even though its target is extensible. It should have returned false"_s);
            return false;
        }
    }

    return trapResultAsBool;
}

bool ProxyObject::preventExtensions(JSObject* object, JSGlobalObject* globalObject)
{
    return jsCast<ProxyObject*>(object)->performPreventExtensions(globalObject);
}

bool ProxyObject::performIsExtensible(JSGlobalObject* globalObject)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue isExtensibleMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "isExtensible"_s), "'isExtensible' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, false);

    JSObject* target = this->target();
    if (isExtensibleMethod.isUndefined())
        RELEASE_AND_RETURN(scope, target->isExtensible(globalObject));

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, isExtensibleMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    bool isTargetExtensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    if (trapResultAsBool != isTargetExtensible) {
        if (isTargetExtensible) {
            ASSERT(!trapResultAsBool);
            throwTypeError(globalObject, scope, "Proxy object's 'isExtensible' trap returned false when the target is extensible. It should have returned true"_s);
        } else {
            ASSERT(!isTargetExtensible);
            ASSERT(trapResultAsBool);
            throwTypeError(globalObject, scope, "Proxy object's 'isExtensible' trap returned true when the target is non-extensible. It should have returned false"_s);
        }
    }
    
    return trapResultAsBool;
}

bool ProxyObject::isExtensible(JSObject* object, JSGlobalObject* globalObject)
{
    return jsCast<ProxyObject*>(object)->performIsExtensible(globalObject);
}

bool ProxyObject::performDefineOwnProperty(JSGlobalObject* globalObject, PropertyName propertyName, const PropertyDescriptor& descriptor, bool shouldThrow)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    JSObject* target = this->target();
    auto performDefaultDefineOwnProperty = [&] {
        RELEASE_AND_RETURN(scope, target->methodTable()->defineOwnProperty(target, globalObject, propertyName, descriptor, shouldThrow));
    };

    if (propertyName.isPrivateName())
        return false;

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue definePropertyMethod = handler->getMethod(globalObject, callData, vm.propertyNames->defineProperty, "'defineProperty' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, false);

    if (definePropertyMethod.isUndefined())
        return performDefaultDefineOwnProperty();

    JSObject* descriptorObject = constructObjectFromPropertyDescriptor(globalObject, descriptor);
    scope.assertNoException();
    ASSERT(descriptorObject);

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(identifierToSafePublicJSValue(vm, Identifier::fromUid(vm, propertyName.uid())));
    arguments.append(descriptorObject);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, definePropertyMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    if (!trapResultAsBool) {
        if (shouldThrow)
            throwTypeError(globalObject, scope, makeString("Proxy's 'defineProperty' trap returned falsy value for property '"_s, StringView(propertyName.uid()), '\''));
        return false;
    }

    bool settingConfigurableToFalse = descriptor.configurablePresent() && !descriptor.configurable();
    if (!settingConfigurableToFalse && !target->structure()->isNonExtensibleOrHasNonConfigurableProperties())
        return true;

    PropertyDescriptor targetDescriptor;
    bool isTargetDescriptorDefined = target->getOwnPropertyDescriptor(globalObject, propertyName, targetDescriptor);
    RETURN_IF_EXCEPTION(scope, false);

    bool targetIsExtensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, false);

    if (!isTargetDescriptorDefined) {
        if (!targetIsExtensible) {
            throwTypeError(globalObject, scope, "Proxy's 'defineProperty' trap returned true even though getOwnPropertyDescriptor of the Proxy's target returned undefined and the target is non-extensible"_s);
            return false;
        }
        if (settingConfigurableToFalse) {
            throwTypeError(globalObject, scope, "Proxy's 'defineProperty' trap returned true for a non-configurable field even though getOwnPropertyDescriptor of the Proxy's target returned undefined"_s);
            return false;
        }

        return true;
    } 

    ASSERT(isTargetDescriptorDefined);
    bool isCurrentDefined = isTargetDescriptorDefined;
    const PropertyDescriptor& current = targetDescriptor;
    bool throwException = false;
    bool isCompatibleDescriptor = validateAndApplyPropertyDescriptor(globalObject, nullptr, propertyName, targetIsExtensible, descriptor, isCurrentDefined, current, throwException);
    RETURN_IF_EXCEPTION(scope, false);    
    if (!isCompatibleDescriptor) {
        throwTypeError(globalObject, scope, "Proxy's 'defineProperty' trap did not define a property on its target that is compatible with the trap's input descriptor"_s);
        return false;
    }
    if (settingConfigurableToFalse && targetDescriptor.configurable()) {
        throwTypeError(globalObject, scope, "Proxy's 'defineProperty' trap did not define a non-configurable property on its target even though the input descriptor to the trap said it must do so"_s);
        return false;
    }
    if (targetDescriptor.isDataDescriptor() && !targetDescriptor.configurable() && targetDescriptor.writable()) {
        if (descriptor.writablePresent() && !descriptor.writable()) {
            throwTypeError(globalObject, scope, "Proxy's 'defineProperty' trap returned true for a non-writable input descriptor when the target's property is non-configurable and writable"_s);
            return false;
        }
    }
    
    return true;
}

bool ProxyObject::defineOwnProperty(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, const PropertyDescriptor& descriptor, bool shouldThrow)
{
    ProxyObject* thisObject = jsCast<ProxyObject*>(object);
    return thisObject->performDefineOwnProperty(globalObject, propertyName, descriptor, shouldThrow);
}

bool ProxyObject::forwardsGetOwnPropertyNamesToTarget(DontEnumPropertiesMode dontEnumPropertiesMode)
{
    JSValue handler = this->handler();
    if (handler.isNull())
        return false;

    if (!isHandlerTrapsCacheValid(asObject(handler)))
        return false;

    if (m_handlerTrapsOffsetsCache[static_cast<uint8_t>(HandlerTrap::OwnKeys)] != invalidOffset)
        return false;

    if (dontEnumPropertiesMode == DontEnumPropertiesMode::Exclude) {
        if (m_handlerTrapsOffsetsCache[static_cast<uint8_t>(HandlerTrap::GetOwnPropertyDescriptor)] != invalidOffset)
            return false;
    }

    return true;
}

void ProxyObject::performGetOwnPropertyNames(JSGlobalObject* globalObject, PropertyNameArray& propertyNames)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return;
    }
    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSObject* ownKeysMethod = getHandlerTrap(globalObject, handler, callData, vm.propertyNames->ownKeys, HandlerTrap::OwnKeys);
    RETURN_IF_EXCEPTION(scope, void());
    JSObject* target = this->target();
    if (!ownKeysMethod) {
        scope.release();
        target->methodTable()->getOwnPropertyNames(target, globalObject, propertyNames, DontEnumPropertiesMode::Include);
        return;
    }

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, ownKeysMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, void());

    if (!trapResult.isObject()) {
        throwTypeError(globalObject, scope, "Proxy handler's 'ownKeys' method must return an object"_s);
        return;
    }

    UncheckedKeyHashSet<UniquedStringImpl*> uncheckedResultKeys;
    forEachInArrayLike(globalObject, asObject(trapResult), [&] (JSValue value) -> bool {
        if (!value.isString() && !value.isSymbol()) {
            throwTypeError(globalObject, scope, "Proxy handler's 'ownKeys' method must return an array-like object containing only Strings and Symbols"_s);
            return false;
        }

        Identifier ident = value.toPropertyKey(globalObject);
        RETURN_IF_EXCEPTION(scope, false);

        if (!uncheckedResultKeys.add(ident.impl()).isNewEntry) {
            throwTypeError(globalObject, scope, "Proxy handler's 'ownKeys' trap result must not contain any duplicate names"_s);
            return false;
        }

        propertyNames.add(ident);
        return true;
    });
    RETURN_IF_EXCEPTION(scope, void());

    if (!target->structure()->isNonExtensibleOrHasNonConfigurableProperties())
        return;

    bool targetIsExensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, void());

    PropertyNameArray targetKeys(vm, PropertyNameMode::StringsAndSymbols, PrivateSymbolMode::Exclude);
    target->methodTable()->getOwnPropertyNames(target, globalObject, targetKeys, DontEnumPropertiesMode::Include);
    RETURN_IF_EXCEPTION(scope, void());
    UncheckedKeyHashSet<UniquedStringImpl*> targetNonConfigurableKeys;
    UncheckedKeyHashSet<UniquedStringImpl*> targetConfigurableKeys;
    for (const Identifier& ident : targetKeys) {
        PropertyDescriptor descriptor;
        bool isPropertyDefined = target->getOwnPropertyDescriptor(globalObject, ident.impl(), descriptor); 
        RETURN_IF_EXCEPTION(scope, void());
        if (isPropertyDefined && !descriptor.configurable())
            targetNonConfigurableKeys.add(ident.impl());
        else if (!targetIsExensible)
            targetConfigurableKeys.add(ident.impl());
    }

    for (UniquedStringImpl* impl : targetNonConfigurableKeys) {
        if (!uncheckedResultKeys.remove(impl)) {
            throwTypeError(globalObject, scope, makeString("Proxy object's 'target' has the non-configurable property '"_s, StringView(impl), "' that was not in the result from the 'ownKeys' trap"_s));
            return;
        }
    }

    if (!targetIsExensible) {
        for (UniquedStringImpl* impl : targetConfigurableKeys) {
            if (!uncheckedResultKeys.remove(impl)) {
                throwTypeError(globalObject, scope, makeString("Proxy object's non-extensible 'target' has configurable property '"_s, StringView(impl), "' that was not in the result from the 'ownKeys' trap"_s));
                return;
            }
        }

        if (uncheckedResultKeys.size()) {
            throwTypeError(globalObject, scope, "Proxy handler's 'ownKeys' method returned a key that was not present in its non-extensible target"_s);
            return;
        }
    }
}

void ProxyObject::performGetOwnEnumerablePropertyNames(JSGlobalObject* globalObject, PropertyNameArray& propertyNames)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    PropertyNameArray unfilteredNames(vm, propertyNames.propertyNameMode(), propertyNames.privateSymbolMode());
    performGetOwnPropertyNames(globalObject, unfilteredNames);
    RETURN_IF_EXCEPTION(scope, void());
    // Filtering DontEnum properties is observable in proxies and must occur after the invariant checks pass.
    for (const auto& propertyName : unfilteredNames) {
        PropertySlot slot(this, PropertySlot::InternalMethodType::GetOwnProperty);
        auto isPropertyDefined = getOwnPropertySlotCommon(globalObject, propertyName, slot);
        RETURN_IF_EXCEPTION(scope, void());
        if (!isPropertyDefined)
            continue;
        if (slot.attributes() & PropertyAttribute::DontEnum)
            continue;
        propertyNames.add(propertyName.impl());
    }
}

void ProxyObject::getOwnPropertyNames(JSObject* object, JSGlobalObject* globalObject, PropertyNameArray& propertyNameArray, DontEnumPropertiesMode mode)
{
    ProxyObject* thisObject = jsCast<ProxyObject*>(object);
    if (mode == DontEnumPropertiesMode::Include)
        thisObject->performGetOwnPropertyNames(globalObject, propertyNameArray);
    else
        thisObject->performGetOwnEnumerablePropertyNames(globalObject, propertyNameArray);
}

bool ProxyObject::performSetPrototype(JSGlobalObject* globalObject, JSValue prototype, bool shouldThrowIfCantSet)
{
    NO_TAIL_CALLS();

    ASSERT(prototype.isObject() || prototype.isNull());

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return false;
    }

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return false;
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue setPrototypeOfMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "setPrototypeOf"_s), "'setPrototypeOf' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, false);

    JSObject* target = this->target();
    if (setPrototypeOfMethod.isUndefined())
        RELEASE_AND_RETURN(scope, target->setPrototype(vm, globalObject, prototype, shouldThrowIfCantSet));

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    arguments.append(prototype);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, setPrototypeOfMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, false);

    bool trapResultAsBool = trapResult.toBoolean(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    
    if (!trapResultAsBool) {
        if (shouldThrowIfCantSet)
            throwTypeError(globalObject, scope, "Proxy 'setPrototypeOf' returned false indicating it could not set the prototype value. The operation was expected to succeed"_s);
        return false;
    }

    bool targetIsExtensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    if (targetIsExtensible)
        return true;

    JSValue targetPrototype = target->getPrototype(globalObject);
    RETURN_IF_EXCEPTION(scope, false);
    bool isSame = sameValue(globalObject, prototype, targetPrototype);
    RETURN_IF_EXCEPTION(scope, false);
    if (!isSame) {
        throwTypeError(globalObject, scope, "Proxy 'setPrototypeOf' trap returned true when its target is non-extensible and the new prototype value is not the same as the current prototype value. It should have returned false"_s);
        return false;
    }

    return true;
}

bool ProxyObject::setPrototype(JSObject* object, JSGlobalObject* globalObject, JSValue prototype, bool shouldThrowIfCantSet)
{
    return jsCast<ProxyObject*>(object)->performSetPrototype(globalObject, prototype, shouldThrowIfCantSet);
}

JSValue ProxyObject::performGetPrototype(JSGlobalObject* globalObject)
{
    NO_TAIL_CALLS();

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    if (!vm.isSafeToRecurseSoft()) [[unlikely]] {
        throwStackOverflowError(globalObject, scope);
        return { };
    }

    JSValue handlerValue = this->handler();
    if (handlerValue.isNull()) {
        throwTypeError(globalObject, scope, s_proxyAlreadyRevokedErrorMessage);
        return { };
    }

    JSObject* handler = jsCast<JSObject*>(handlerValue);
    CallData callData;
    JSValue getPrototypeOfMethod = handler->getMethod(globalObject, callData, makeIdentifier(vm, "getPrototypeOf"_s), "'getPrototypeOf' property of a Proxy's handler should be callable"_s);
    RETURN_IF_EXCEPTION(scope, { });

    JSObject* target = this->target();
    if (getPrototypeOfMethod.isUndefined()) 
        RELEASE_AND_RETURN(scope, target->getPrototype(globalObject));

    MarkedArgumentBuffer arguments;
    arguments.append(target);
    ASSERT(!arguments.hasOverflowed());
    JSValue trapResult = call(globalObject, getPrototypeOfMethod, callData, handler, arguments);
    RETURN_IF_EXCEPTION(scope, { });

    if (!trapResult.isObject() && !trapResult.isNull()) {
        throwTypeError(globalObject, scope, "Proxy handler's 'getPrototypeOf' trap should either return an object or null"_s);
        return { };
    }

    bool targetIsExtensible = target->isExtensible(globalObject);
    RETURN_IF_EXCEPTION(scope, { });
    if (targetIsExtensible)
        return trapResult;

    JSValue targetPrototype = target->getPrototype(globalObject);
    RETURN_IF_EXCEPTION(scope, { });
    bool isSame = sameValue(globalObject, targetPrototype, trapResult);
    RETURN_IF_EXCEPTION(scope, { });
    if (!isSame) {
        throwTypeError(globalObject, scope, "Proxy's 'getPrototypeOf' trap for a non-extensible target should return the same value as the target's prototype"_s);
        return { };
    }

    return trapResult;
}

JSValue ProxyObject::getPrototype(JSObject* object, JSGlobalObject* globalObject)
{
    return jsCast<ProxyObject*>(object)->performGetPrototype(globalObject);
}

void ProxyObject::revoke(VM& vm)
{
    // This should only ever be called once and we should strictly transition from Object to null.
    RELEASE_ASSERT(!handler().isNull() && handler().isObject());
    internalField(Field::Handler).set(vm, this, jsNull());
}

bool ProxyObject::isRevoked() const
{
    return handler().isNull();
}

template<typename Visitor>
void ProxyObject::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    auto* thisObject = jsCast<ProxyObject*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);
    visitor.append(thisObject->m_handlerStructureID);
    visitor.append(thisObject->m_handlerPrototypeStructureID);
}

DEFINE_VISIT_CHILDREN(ProxyObject);

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
