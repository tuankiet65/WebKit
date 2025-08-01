/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestInterfaceLeadingUnderscore.h"

#include "ActiveDOMObject.h"
#include "ContextDestructionObserverInlines.h"
#include "ExtendedDOMClientIsoSubspaces.h"
#include "ExtendedDOMIsoSubspaces.h"
#include "JSDOMAttribute.h"
#include "JSDOMBinding.h"
#include "JSDOMConstructorNotConstructable.h"
#include "JSDOMConvertStrings.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObjectInlines.h"
#include "JSDOMWrapperCache.h"
#include "ScriptExecutionContext.h"
#include "WebCoreJSClientData.h"
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/HeapAnalyzer.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/JSDestructibleObjectHeapCellType.h>
#include <JavaScriptCore/SlotVisitorMacros.h>
#include <JavaScriptCore/SubspaceInlines.h>
#include <wtf/GetPtr.h>
#include <wtf/PointerPreparations.h>
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace JSC;

// Attributes

static JSC_DECLARE_CUSTOM_GETTER(jsTestInterfaceLeadingUnderscoreConstructor);
static JSC_DECLARE_CUSTOM_GETTER(jsTestInterfaceLeadingUnderscore_readonly);

class JSTestInterfaceLeadingUnderscorePrototype final : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSTestInterfaceLeadingUnderscorePrototype* create(JSC::VM& vm, JSDOMGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSTestInterfaceLeadingUnderscorePrototype* ptr = new (NotNull, JSC::allocateCell<JSTestInterfaceLeadingUnderscorePrototype>(vm)) JSTestInterfaceLeadingUnderscorePrototype(vm, globalObject, structure);
        ptr->finishCreation(vm);
        return ptr;
    }

    DECLARE_INFO;
    template<typename CellType, JSC::SubspaceAccess>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm)
    {
        STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestInterfaceLeadingUnderscorePrototype, Base);
        return &vm.plainObjectSpace();
    }
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSTestInterfaceLeadingUnderscorePrototype(JSC::VM& vm, JSC::JSGlobalObject*, JSC::Structure* structure)
        : JSC::JSNonFinalObject(vm, structure)
    {
    }

    void finishCreation(JSC::VM&);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestInterfaceLeadingUnderscorePrototype, JSTestInterfaceLeadingUnderscorePrototype::Base);

using JSTestInterfaceLeadingUnderscoreDOMConstructor = JSDOMConstructorNotConstructable<JSTestInterfaceLeadingUnderscore>;

template<> const ClassInfo JSTestInterfaceLeadingUnderscoreDOMConstructor::s_info = { "TestInterfaceLeadingUnderscore"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestInterfaceLeadingUnderscoreDOMConstructor) };

template<> JSValue JSTestInterfaceLeadingUnderscoreDOMConstructor::prototypeForStructure(JSC::VM& vm, const JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(vm);
    return globalObject.functionPrototype();
}

template<> void JSTestInterfaceLeadingUnderscoreDOMConstructor::initializeProperties(VM& vm, JSDOMGlobalObject& globalObject)
{
    putDirect(vm, vm.propertyNames->length, jsNumber(0), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    JSString* nameString = jsNontrivialString(vm, "TestInterfaceLeadingUnderscore"_s);
    m_originalName.set(vm, this, nameString);
    putDirect(vm, vm.propertyNames->name, nameString, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->prototype, JSTestInterfaceLeadingUnderscore::prototype(vm, globalObject), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::DontDelete);
}

/* Hash table for prototype */

static const std::array<HashTableValue, 2> JSTestInterfaceLeadingUnderscorePrototypeTableValues {
    HashTableValue { "constructor"_s, static_cast<unsigned>(PropertyAttribute::DontEnum), NoIntrinsic, { HashTableValue::GetterSetterType, jsTestInterfaceLeadingUnderscoreConstructor, 0 } },
    HashTableValue { "readonly"_s, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::CustomAccessor | JSC::PropertyAttribute::DOMAttribute, NoIntrinsic, { HashTableValue::GetterSetterType, jsTestInterfaceLeadingUnderscore_readonly, 0 } },
};

const ClassInfo JSTestInterfaceLeadingUnderscorePrototype::s_info = { "TestInterfaceLeadingUnderscore"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestInterfaceLeadingUnderscorePrototype) };

void JSTestInterfaceLeadingUnderscorePrototype::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    reifyStaticProperties(vm, JSTestInterfaceLeadingUnderscore::info(), JSTestInterfaceLeadingUnderscorePrototypeTableValues, *this);
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();
}

const ClassInfo JSTestInterfaceLeadingUnderscore::s_info = { "TestInterfaceLeadingUnderscore"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestInterfaceLeadingUnderscore) };

JSTestInterfaceLeadingUnderscore::JSTestInterfaceLeadingUnderscore(Structure* structure, JSDOMGlobalObject& globalObject, Ref<TestInterfaceLeadingUnderscore>&& impl)
    : JSDOMWrapper<TestInterfaceLeadingUnderscore>(structure, globalObject, WTFMove(impl))
{
}

static_assert(!std::is_base_of<ActiveDOMObject, TestInterfaceLeadingUnderscore>::value, "Interface is not marked as [ActiveDOMObject] even though implementation class subclasses ActiveDOMObject.");

JSObject* JSTestInterfaceLeadingUnderscore::createPrototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    auto* structure = JSTestInterfaceLeadingUnderscorePrototype::createStructure(vm, &globalObject, globalObject.objectPrototype());
    structure->setMayBePrototype(true);
    return JSTestInterfaceLeadingUnderscorePrototype::create(vm, &globalObject, structure);
}

JSObject* JSTestInterfaceLeadingUnderscore::prototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return getDOMPrototype<JSTestInterfaceLeadingUnderscore>(vm, globalObject);
}

JSValue JSTestInterfaceLeadingUnderscore::getConstructor(VM& vm, const JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSTestInterfaceLeadingUnderscoreDOMConstructor, DOMConstructorID::TestInterfaceLeadingUnderscore>(vm, *jsCast<const JSDOMGlobalObject*>(globalObject));
}

void JSTestInterfaceLeadingUnderscore::destroy(JSC::JSCell* cell)
{
    JSTestInterfaceLeadingUnderscore* thisObject = static_cast<JSTestInterfaceLeadingUnderscore*>(cell);
    thisObject->JSTestInterfaceLeadingUnderscore::~JSTestInterfaceLeadingUnderscore();
}

JSC_DEFINE_CUSTOM_GETTER(jsTestInterfaceLeadingUnderscoreConstructor, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName))
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestInterfaceLeadingUnderscorePrototype*>(JSValue::decode(thisValue));
    if (!prototype) [[unlikely]]
        return throwVMTypeError(lexicalGlobalObject, throwScope);
    return JSValue::encode(JSTestInterfaceLeadingUnderscore::getConstructor(vm, prototype->globalObject()));
}

static inline JSValue jsTestInterfaceLeadingUnderscore_readonlyGetter(JSGlobalObject& lexicalGlobalObject, JSTestInterfaceLeadingUnderscore& thisObject)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(&lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    SUPPRESS_UNCOUNTED_LOCAL auto& impl = thisObject.wrapped();
    RELEASE_AND_RETURN(throwScope, (toJS<IDLDOMString>(lexicalGlobalObject, throwScope, impl.readonly())));
}

JSC_DEFINE_CUSTOM_GETTER(jsTestInterfaceLeadingUnderscore_readonly, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName attributeName))
{
    return IDLAttribute<JSTestInterfaceLeadingUnderscore>::get<jsTestInterfaceLeadingUnderscore_readonlyGetter, CastedThisErrorBehavior::Assert>(*lexicalGlobalObject, thisValue, attributeName);
}

JSC::GCClient::IsoSubspace* JSTestInterfaceLeadingUnderscore::subspaceForImpl(JSC::VM& vm)
{
    return WebCore::subspaceForImpl<JSTestInterfaceLeadingUnderscore, UseCustomHeapCellType::No>(vm, "JSTestInterfaceLeadingUnderscore"_s,
        [] (auto& spaces) { return spaces.m_clientSubspaceForTestInterfaceLeadingUnderscore.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_clientSubspaceForTestInterfaceLeadingUnderscore = std::forward<decltype(space)>(space); },
        [] (auto& spaces) { return spaces.m_subspaceForTestInterfaceLeadingUnderscore.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_subspaceForTestInterfaceLeadingUnderscore = std::forward<decltype(space)>(space); }
    );
}

void JSTestInterfaceLeadingUnderscore::analyzeHeap(JSCell* cell, HeapAnalyzer& analyzer)
{
    auto* thisObject = jsCast<JSTestInterfaceLeadingUnderscore*>(cell);
    analyzer.setWrappedObjectForCell(cell, &thisObject->wrapped());
    if (RefPtr context = thisObject->scriptExecutionContext())
        analyzer.setLabelForCell(cell, makeString("url "_s, context->url().string()));
    Base::analyzeHeap(cell, analyzer);
}

bool JSTestInterfaceLeadingUnderscoreOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, AbstractSlotVisitor& visitor, ASCIILiteral* reason)
{
    UNUSED_PARAM(handle);
    UNUSED_PARAM(visitor);
    UNUSED_PARAM(reason);
    return false;
}

void JSTestInterfaceLeadingUnderscoreOwner::finalize(JSC::Handle<JSC::Unknown> handle, void* context)
{
    auto* jsTestInterfaceLeadingUnderscore = static_cast<JSTestInterfaceLeadingUnderscore*>(handle.slot()->asCell());
    auto& world = *static_cast<DOMWrapperWorld*>(context);
    uncacheWrapper(world, jsTestInterfaceLeadingUnderscore->protectedWrapped().ptr(), jsTestInterfaceLeadingUnderscore);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
#if ENABLE(BINDING_INTEGRITY)
#if PLATFORM(WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestInterfaceLeadingUnderscore@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore30TestInterfaceLeadingUnderscoreE[]; }
#endif
template<std::same_as<TestInterfaceLeadingUnderscore> T>
static inline void verifyVTable(TestInterfaceLeadingUnderscore* ptr) 
{
    if constexpr (std::is_polymorphic_v<T>) {
        const void* actualVTablePointer = getVTablePointer<T>(ptr);
#if PLATFORM(WIN)
        void* expectedVTablePointer = __identifier("??_7TestInterfaceLeadingUnderscore@WebCore@@6B@");
#else
        void* expectedVTablePointer = &_ZTVN7WebCore30TestInterfaceLeadingUnderscoreE[2];
#endif

        // If you hit this assertion you either have a use after free bug, or
        // TestInterfaceLeadingUnderscore has subclasses. If TestInterfaceLeadingUnderscore has subclasses that get passed
        // to toJS() we currently require TestInterfaceLeadingUnderscore you to opt out of binding hardening
        // by adding the SkipVTableValidation attribute to the interface IDL definition
        RELEASE_ASSERT(actualVTablePointer == expectedVTablePointer);
    }
}
#endif
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

JSC::JSValue toJSNewlyCreated(JSC::JSGlobalObject*, JSDOMGlobalObject* globalObject, Ref<TestInterfaceLeadingUnderscore>&& impl)
{
#if ENABLE(BINDING_INTEGRITY)
    verifyVTable<TestInterfaceLeadingUnderscore>(impl.ptr());
#endif
    return createWrapper<TestInterfaceLeadingUnderscore>(globalObject, WTFMove(impl));
}

JSC::JSValue toJS(JSC::JSGlobalObject* lexicalGlobalObject, JSDOMGlobalObject* globalObject, TestInterfaceLeadingUnderscore& impl)
{
    return wrap(lexicalGlobalObject, globalObject, impl);
}

TestInterfaceLeadingUnderscore* JSTestInterfaceLeadingUnderscore::toWrapped(JSC::VM&, JSC::JSValue value)
{
    if (auto* wrapper = jsDynamicCast<JSTestInterfaceLeadingUnderscore*>(value))
        return &wrapper->wrapped();
    return nullptr;
}

}
