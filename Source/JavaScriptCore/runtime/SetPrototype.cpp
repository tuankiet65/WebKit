/*
 * Copyright (C) 2013-2019 Apple Inc. All rights reserved.
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
#include "SetPrototype.h"

#include "CachedCall.h"
#include "InterpreterInlines.h"
#include "BuiltinNames.h"
#include "GetterSetter.h"
#include "IteratorOperations.h"
#include "JSCInlines.h"
#include "JSSet.h"
#include "JSSetIterator.h"
#include "SetPrototypeInlines.h"
#include "VMEntryScopeInlines.h"

namespace JSC {

const ClassInfo SetPrototype::s_info = { "Set"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(SetPrototype) };

static JSC_DECLARE_HOST_FUNCTION(setProtoFuncAdd);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncClear);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncDelete);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncHas);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncValues);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncEntries);
static JSC_DECLARE_HOST_FUNCTION(setProtoFuncIntersection);

static JSC_DECLARE_HOST_FUNCTION(setProtoFuncSize);

void SetPrototype::finishCreation(VM& vm, JSGlobalObject* globalObject)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));

    JSFunction* addFunc = JSFunction::create(vm, globalObject, 1, vm.propertyNames->add.string(), setProtoFuncAdd, ImplementationVisibility::Public, JSSetAddIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->add, addFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().addPrivateName(), addFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* clearFunc = JSFunction::create(vm, globalObject, 0, vm.propertyNames->clear.string(), setProtoFuncClear, ImplementationVisibility::Public);
    putDirectWithoutTransition(vm, vm.propertyNames->clear, clearFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().clearPrivateName(), clearFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* deleteFunc = JSFunction::create(vm, globalObject, 1, vm.propertyNames->deleteKeyword.string(), setProtoFuncDelete, ImplementationVisibility::Public, JSSetDeleteIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->deleteKeyword, deleteFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().deletePrivateName(), deleteFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* entriesFunc = JSFunction::create(vm, globalObject, 0, vm.propertyNames->builtinNames().entriesPublicName().string(), setProtoFuncEntries, ImplementationVisibility::Public, JSSetEntriesIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().entriesPublicName(), entriesFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().entriesPrivateName(), entriesFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* forEachFunc = JSFunction::create(vm, globalObject, setPrototypeForEachCodeGenerator(vm), globalObject);
    putDirectWithoutTransition(vm, vm.propertyNames->forEach, forEachFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().forEachPrivateName(), forEachFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* hasFunc = JSFunction::create(vm, globalObject, 1, vm.propertyNames->has.string(), setProtoFuncHas, ImplementationVisibility::Public, JSSetHasIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->has, hasFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().hasPrivateName(), hasFunc, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* values = JSFunction::create(vm, globalObject, 0, vm.propertyNames->builtinNames().valuesPublicName().string(), setProtoFuncValues, ImplementationVisibility::Public, JSSetValuesIntrinsic);
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().keysPublicName(), values, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().keysPrivateName(), values, static_cast<unsigned>(PropertyAttribute::DontEnum));

    JSFunction* sizeGetter = JSFunction::create(vm, globalObject, 0, "get size"_s, setProtoFuncSize, ImplementationVisibility::Public);
    GetterSetter* sizeAccessor = GetterSetter::create(vm, globalObject, sizeGetter, nullptr);
    putDirectNonIndexAccessorWithoutTransition(vm, vm.propertyNames->size, sizeAccessor, PropertyAttribute::DontEnum | PropertyAttribute::Accessor);
    putDirectNonIndexAccessorWithoutTransition(vm, vm.propertyNames->builtinNames().sizePrivateName(), sizeAccessor, PropertyAttribute::DontEnum | PropertyAttribute::Accessor);

    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().valuesPublicName(), values, static_cast<unsigned>(PropertyAttribute::DontEnum));
    putDirectWithoutTransition(vm, vm.propertyNames->builtinNames().valuesPrivateName(), values, static_cast<unsigned>(PropertyAttribute::DontEnum));

    putDirectWithoutTransition(vm, vm.propertyNames->iteratorSymbol, values, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();

    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().unionPublicName(), setPrototypeUnionCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_NATIVE_FUNCTION_WITHOUT_TRANSITION("intersection"_s, setProtoFuncIntersection, static_cast<unsigned>(PropertyAttribute::DontEnum), 1, ImplementationVisibility::Public);
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().differencePublicName(), setPrototypeDifferenceCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().symmetricDifferencePublicName(), setPrototypeSymmetricDifferenceCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().isSubsetOfPublicName(), setPrototypeIsSubsetOfCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().isSupersetOfPublicName(), setPrototypeIsSupersetOfCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));
    JSC_BUILTIN_FUNCTION_WITHOUT_TRANSITION(vm.propertyNames->builtinNames().isDisjointFromPublicName(), setPrototypeIsDisjointFromCodeGenerator, static_cast<unsigned>(PropertyAttribute::DontEnum));

    globalObject->installSetPrototypeWatchpoint(this);
}

ALWAYS_INLINE static JSSet* getSet(JSGlobalObject* globalObject, JSValue thisValue)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!thisValue.isCell()) [[unlikely]] {
        throwVMError(globalObject, scope, createNotAnObjectError(globalObject, thisValue));
        return nullptr;
    }
    if (auto* set = jsDynamicCast<JSSet*>(thisValue.asCell())) [[likely]]
        return set;
    throwTypeError(globalObject, scope, "Set operation called on non-Set object"_s);
    return nullptr;
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncAdd, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSValue thisValue = callFrame->thisValue();
    JSSet* set = getSet(globalObject, thisValue);
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));

    set->add(globalObject, callFrame->argument(0));
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));
    return JSValue::encode(thisValue);
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncClear, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* set = getSet(globalObject, callFrame->thisValue());
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));

    scope.release();
    set->clear(globalObject);
    return JSValue::encode(jsUndefined());
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncDelete, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* set = getSet(globalObject, callFrame->thisValue());
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));

    RELEASE_AND_RETURN(scope, JSValue::encode(jsBoolean(set->remove(globalObject, callFrame->argument(0)))));
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncHas, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* set = getSet(globalObject, callFrame->thisValue());
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));

    RELEASE_AND_RETURN(scope, JSValue::encode(jsBoolean(set->has(globalObject, callFrame->argument(0)))));
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncSize, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* set = getSet(globalObject, callFrame->thisValue());
    RETURN_IF_EXCEPTION(scope, JSValue::encode(jsUndefined()));

    return JSValue::encode(jsNumber(set->size()));
}

// https://tc39.es/ecma262/#sec-getsetrecord ( Step 1 ~ Step 7 )
static uint32_t getSetSizeAsInt(JSGlobalObject* globalObject, JSValue value)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!value.isObject()) [[unlikely]]
        return throwVMTypeError(globalObject, scope, "Set operation expects first argument to be an object");

    JSObject* obj = asObject(value);

    JSValue rawSize = obj->get(globalObject, vm.propertyNames->size);
    RETURN_IF_EXCEPTION(scope, 0);

    double numSize = rawSize.toNumber(globalObject);
    RETURN_IF_EXCEPTION(scope, 0);

    if (std::isnan(numSize)) [[unlikely]]
        return throwVMTypeError(globalObject, scope, "Set operation expects first argument to have non-NaN 'size' property"_s);

    double intOrInfSize = jsNumber(numSize).toIntegerOrInfinity(globalObject);

    if (intOrInfSize < 0) [[unlikely]]
        return throwVMRangeError(globalObject, scope, "Set operation expects first argument to have non-negative 'size' property"_s);

    if (std::isinf(intOrInfSize)) [[unlikely]]
        return std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(intOrInfSize);
}

static EncodedJSValue fastSetIntersection(JSGlobalObject* globalObject, JSSet* thisSet, JSSet* otherSet)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* result = JSSet::create(vm, globalObject->setStructure());

    JSSet* sourceSet = thisSet->size() <= otherSet->size() ? thisSet : otherSet;
    JSSet* targetSet = thisSet->size() <= otherSet->size() ? otherSet : thisSet;

    JSCell* sourceStorageCell = sourceSet->storageOrSentinel(vm);
    if (sourceStorageCell == vm.orderedHashTableSentinel())
        return JSValue::encode(result);

    auto* sourceStorage = jsCast<JSSet::Storage*>(sourceStorageCell);
    JSSet::Helper::Entry entry = 0;

    while (true) {
        sourceStorageCell = JSSet::Helper::nextAndUpdateIterationEntry(vm, *sourceStorage, entry);
        if (sourceStorageCell == vm.orderedHashTableSentinel())
            break;

        auto* currentStorage = jsCast<JSSet::Storage*>(sourceStorageCell);
        entry = JSSet::Helper::iterationEntry(*currentStorage) + 1;
        JSValue entryKey = JSSet::Helper::getIterationEntryKey(*currentStorage);

        bool targetHasEntry = targetSet->has(globalObject, entryKey);
        RETURN_IF_EXCEPTION(scope, { });
        if (targetHasEntry) {
            result->add(globalObject, entryKey);
            RETURN_IF_EXCEPTION(scope, { });
        }

        sourceStorage = currentStorage;
    }
    return JSValue::encode(result);
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncIntersection, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSSet* thisSet = getSet(globalObject, callFrame->thisValue());
    RETURN_IF_EXCEPTION(scope, { });

    JSValue otherValue = callFrame->argument(0);

    if (otherValue.isCell()) [[likely]] {
        if (auto* otherSet = jsDynamicCast<JSSet*>(otherValue.asCell())) [[likely]] {
            if (setPrimordialWatchpointIsValid(vm, otherSet)) [[likely]] {
                scope.release();
                return fastSetIntersection(globalObject, thisSet, otherSet);
            }
        }
    }

    uint32_t size = getSetSizeAsInt(globalObject, otherValue);
    RETURN_IF_EXCEPTION(scope, { });

    ASSERT(otherValue.isObject());
    JSObject* otherObject = asObject(otherValue);

    JSValue has = otherObject->get(globalObject, vm.propertyNames->has);
    RETURN_IF_EXCEPTION(scope, { });
    if (!has.isCallable()) [[unlikely]]
        return throwVMTypeError(globalObject, scope, "Set.prototype.intersection expects other.has to be callable");

    JSValue keys = otherObject->get(globalObject, vm.propertyNames->keys);
    RETURN_IF_EXCEPTION(scope, { });
    if (!keys.isCallable()) [[unlikely]]
        return throwVMTypeError(globalObject, scope, "Set.prototype.intersection expects other.keys to be callable");

    JSSet* result = JSSet::create(vm, globalObject->setStructure());
    if (thisSet->size() <= size) {
        JSCell* storageCell = thisSet->storageOrSentinel(vm);
        if (storageCell == vm.orderedHashTableSentinel())
            return JSValue::encode(result);

        auto* storage = jsCast<JSSet::Storage*>(storageCell);
        JSSet::Helper::Entry entry = 0;
        CallData hasCallData = JSC::getCallData(has);

        std::optional<CachedCall> cachedHasCall;
        if (hasCallData.type == CallData::Type::JS) [[likely]] {
            cachedHasCall.emplace(globalObject, jsCast<JSFunction*>(has), 1);
            RETURN_IF_EXCEPTION(scope, { });
        }

        while (true) {
            storageCell = JSSet::Helper::nextAndUpdateIterationEntry(vm, *storage, entry);
            if (storageCell == vm.orderedHashTableSentinel())
                break;

            storage = jsCast<JSSet::Storage*>(storageCell);
            entry = JSSet::Helper::iterationEntry(*storage) + 1;
            JSValue entryKey = JSSet::Helper::getIterationEntryKey(*storage);

            JSValue hasResult;
            if (cachedHasCall) [[likely]] {
                hasResult = cachedHasCall->callWithArguments(globalObject, otherValue, entryKey);
                RETURN_IF_EXCEPTION(scope, { });
            } else {
                MarkedArgumentBuffer args;
                args.append(entryKey);
                ASSERT(!args.hasOverflowed());
                hasResult = call(globalObject, has, hasCallData, otherValue, args);
                RETURN_IF_EXCEPTION(scope, { });
            }

            bool hasResultBool = hasResult.toBoolean(globalObject);
            RETURN_IF_EXCEPTION(scope, { });
            if (hasResultBool) {
                result->add(globalObject, entryKey);
                RETURN_IF_EXCEPTION(scope, { });
            }
        }
    } else {
        CallData keysCallData = JSC::getCallData(keys);
        MarkedArgumentBuffer args;
        ASSERT(!args.hasOverflowed());
        JSValue iterator = call(globalObject, keys, keysCallData, otherValue, args);
        RETURN_IF_EXCEPTION(scope, { });
        scope.release();
        forEachInIteratorProtocol(globalObject, iterator, [&](VM&, JSGlobalObject* globalObject, JSValue key) -> void {
            bool thisSetHasKey = thisSet->has(globalObject, key);
            RETURN_IF_EXCEPTION(scope, void());
            if (thisSetHasKey) {
                result->add(globalObject, key);
                RETURN_IF_EXCEPTION(scope, void());
            }
        });
    }

    return JSValue::encode(result);
}

inline JSValue createSetIteratorObject(JSGlobalObject* globalObject, CallFrame* callFrame, IterationKind kind)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSValue thisValue = callFrame->thisValue();
    JSSet* set = getSet(globalObject, thisValue);
    RETURN_IF_EXCEPTION(scope, jsUndefined());

    RELEASE_AND_RETURN(scope, JSSetIterator::create(globalObject, globalObject->setIteratorStructure(), set, kind));
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncValues, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(createSetIteratorObject(globalObject, callFrame, IterationKind::Values));
}

JSC_DEFINE_HOST_FUNCTION(setProtoFuncEntries, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    return JSValue::encode(createSetIteratorObject(globalObject, callFrame, IterationKind::Entries));
}

}
