/*
 * Copyright (C) 2018 Andy VanWagoner (andy@vanwagoner.family)
 * Copyright (C) 2019-2023 Apple Inc. All rights reserved.
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
#include "IntlPluralRules.h"

#include "IntlNumberFormatInlines.h"
#include "IntlObjectInlines.h"
#include "JSCInlines.h"
#include "ObjectConstructor.h"

#ifdef U_HIDE_DRAFT_API
#undef U_HIDE_DRAFT_API
#endif
#include <unicode/upluralrules.h>
#include <unicode/unumberformatter.h>
#include <unicode/unumberrangeformatter.h>
#define U_HIDE_DRAFT_API 1

namespace JSC {

void UPluralRulesDeleter::operator()(UPluralRules* pluralRules)
{
    if (pluralRules)
        uplrules_close(pluralRules);
}

const ClassInfo IntlPluralRules::s_info = { "Object"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(IntlPluralRules) };

using UEnumerationDeleter = ICUDeleter<uenum_close>;

IntlPluralRules* IntlPluralRules::create(VM& vm, Structure* structure)
{
    IntlPluralRules* format = new (NotNull, allocateCell<IntlPluralRules>(vm)) IntlPluralRules(vm, structure);
    format->finishCreation(vm);
    return format;
}

Structure* IntlPluralRules::createStructure(VM& vm, JSGlobalObject* globalObject, JSValue prototype)
{
    return Structure::create(vm, globalObject, prototype, TypeInfo(ObjectType, StructureFlags), info());
}

IntlPluralRules::IntlPluralRules(VM& vm, Structure* structure)
    : Base(vm, structure)
{
}

template<typename Visitor>
void IntlPluralRules::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    IntlPluralRules* thisObject = jsCast<IntlPluralRules*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());

    Base::visitChildren(thisObject, visitor);
}

DEFINE_VISIT_CHILDREN(IntlPluralRules);

Vector<String> IntlPluralRules::localeData(const String&, RelevantExtensionKey)
{
    return { };
}

// https://tc39.github.io/ecma402/#sec-initializepluralrules
void IntlPluralRules::initializePluralRules(JSGlobalObject* globalObject, JSValue locales, JSValue optionsValue)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    Vector<String> requestedLocales = canonicalizeLocaleList(globalObject, locales);
    RETURN_IF_EXCEPTION(scope, void());

    JSObject* options = intlCoerceOptionsToObject(globalObject, optionsValue);
    RETURN_IF_EXCEPTION(scope, void());

    ResolveLocaleOptions localeOptions;
    LocaleMatcher localeMatcher = intlOption<LocaleMatcher>(globalObject, options, vm.propertyNames->localeMatcher, { { "lookup"_s, LocaleMatcher::Lookup }, { "best fit"_s, LocaleMatcher::BestFit } }, "localeMatcher must be either \"lookup\" or \"best fit\""_s, LocaleMatcher::BestFit);
    RETURN_IF_EXCEPTION(scope, void());

    const auto& availableLocales = intlPluralRulesAvailableLocales();
    auto resolved = resolveLocale(globalObject, availableLocales, requestedLocales, localeMatcher, localeOptions, { }, localeData);
    m_locale = resolved.locale;
    if (m_locale.isEmpty()) {
        throwTypeError(globalObject, scope, "failed to initialize PluralRules due to invalid locale"_s);
        return;
    }

    m_type = intlOption<Type>(globalObject, options, vm.propertyNames->type, { { "cardinal"_s, Type::Cardinal }, { "ordinal"_s, Type::Ordinal } }, "type must be \"cardinal\" or \"ordinal\""_s, Type::Cardinal);
    RETURN_IF_EXCEPTION(scope, void());

    m_notation = intlOption<IntlNotation>(globalObject, options, Identifier::fromString(vm, "notation"_s), { { "standard"_s, IntlNotation::Standard }, { "scientific"_s, IntlNotation::Scientific }, { "engineering"_s, IntlNotation::Engineering }, { "compact"_s, IntlNotation::Compact } }, "notation must be either \"standard\", \"scientific\", \"engineering\", or \"compact\""_s, IntlNotation::Standard);
    RETURN_IF_EXCEPTION(scope, void());

    setNumberFormatDigitOptions(globalObject, this, options, 0, 3, m_notation);
    RETURN_IF_EXCEPTION(scope, void());

    auto locale = m_locale.utf8();
    UErrorCode status = U_ZERO_ERROR;

    StringBuilder skeletonBuilder;

    appendNumberFormatDigitOptionsToSkeleton(this, skeletonBuilder);
    appendNumberFormatNotationOptionsToSkeleton(this, skeletonBuilder);

    StringView skeletonView { skeletonBuilder.toString() };
    auto upconverted = skeletonView.upconvertedCharacters();

    m_numberFormatter = std::unique_ptr<UNumberFormatter, UNumberFormatterDeleter>(unumf_openForSkeletonAndLocale(upconverted.get(), skeletonView.length(), locale.data(), &status));
    if (U_FAILURE(status)) {
        throwTypeError(globalObject, scope, "failed to initialize PluralRules"_s);
        return;
    }

    m_numberRangeFormatter = std::unique_ptr<UNumberRangeFormatter, UNumberRangeFormatterDeleter>(unumrf_openForSkeletonWithCollapseAndIdentityFallback(upconverted.get(), skeletonView.length(), UNUM_RANGE_COLLAPSE_NONE, UNUM_IDENTITY_FALLBACK_RANGE, locale.data(), nullptr, &status));
    if (U_FAILURE(status)) {
        throwTypeError(globalObject, scope, "failed to initialize PluralRules"_s);
        return;
    }

    m_pluralRules = std::unique_ptr<UPluralRules, UPluralRulesDeleter>(uplrules_openForType(locale.data(), m_type == Type::Ordinal ? UPLURAL_TYPE_ORDINAL : UPLURAL_TYPE_CARDINAL, &status));
    if (U_FAILURE(status)) {
        throwTypeError(globalObject, scope, "failed to initialize PluralRules"_s);
        return;
    }
}

// https://tc39.es/ecma402/#sec-intl.pluralrules.prototype.resolvedoptions
JSObject* IntlPluralRules::resolvedOptions(JSGlobalObject* globalObject) const
{
    ASSERT(m_pluralRules);

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSObject* options = constructEmptyObject(globalObject);
    options->putDirect(vm, vm.propertyNames->locale, jsNontrivialString(vm, m_locale));
    options->putDirect(vm, vm.propertyNames->type, jsNontrivialString(vm, m_type == Type::Ordinal ? "ordinal"_s : "cardinal"_s));
    options->putDirect(vm, Identifier::fromString(vm, "notation"_s), jsNontrivialString(vm, IntlNumberFormat::notationString(m_notation)));
    options->putDirect(vm, vm.propertyNames->minimumIntegerDigits, jsNumber(m_minimumIntegerDigits));
    switch (m_roundingType) {
    case IntlRoundingType::FractionDigits:
        options->putDirect(vm, vm.propertyNames->minimumFractionDigits, jsNumber(m_minimumFractionDigits));
        options->putDirect(vm, vm.propertyNames->maximumFractionDigits, jsNumber(m_maximumFractionDigits));
        break;
    case IntlRoundingType::SignificantDigits:
        options->putDirect(vm, vm.propertyNames->minimumSignificantDigits, jsNumber(m_minimumSignificantDigits));
        options->putDirect(vm, vm.propertyNames->maximumSignificantDigits, jsNumber(m_maximumSignificantDigits));
        break;
    case IntlRoundingType::MorePrecision:
    case IntlRoundingType::LessPrecision:
        options->putDirect(vm, vm.propertyNames->minimumFractionDigits, jsNumber(m_minimumFractionDigits));
        options->putDirect(vm, vm.propertyNames->maximumFractionDigits, jsNumber(m_maximumFractionDigits));
        options->putDirect(vm, vm.propertyNames->minimumSignificantDigits, jsNumber(m_minimumSignificantDigits));
        options->putDirect(vm, vm.propertyNames->maximumSignificantDigits, jsNumber(m_maximumSignificantDigits));
        break;
    }

    JSArray* categories = JSArray::tryCreate(vm, globalObject->arrayStructureForIndexingTypeDuringAllocation(ArrayWithContiguous), 0);
    if (!categories) [[unlikely]] {
        throwOutOfMemoryError(globalObject, scope);
        return nullptr;
    }

    UErrorCode status = U_ZERO_ERROR;
    auto keywords = std::unique_ptr<UEnumeration, UEnumerationDeleter>(uplrules_getKeywords(m_pluralRules.get(), &status));
    ASSERT(U_SUCCESS(status));
    int32_t resultLength;

    // Category names are always ASCII, so use char[].
    UncheckedKeyHashSet<String> categoriesSet;
    unsigned index = 0;
    while (const char* result = uenum_next(keywords.get(), &resultLength, &status)) {
        ASSERT(U_SUCCESS(status));
        categoriesSet.add(String(unsafeMakeSpan(result, static_cast<size_t>(resultLength))));
    }

    // 4. Let pluralCategories be a List of Strings containing all possible results of PluralRuleSelect for the selected locale pr.[[Locale]],
    // sorted according to the following order: "zero", "one", "two", "few", "many", "other".
    static constexpr std::array<ASCIILiteral, 6> candidates = { {
        "zero"_s, "one"_s, "two"_s, "few"_s, "many"_s, "other"_s
    } };

    for (auto candidate : candidates) {
        auto iter = categoriesSet.find(candidate);
        if (iter != categoriesSet.end()) {
            categories->putDirectIndex(globalObject, index++, jsNontrivialString(vm, *iter));
            RETURN_IF_EXCEPTION(scope, { });
        }
    }

    options->putDirect(vm, vm.propertyNames->pluralCategories, categories);
    options->putDirect(vm, vm.propertyNames->roundingIncrement, jsNumber(m_roundingIncrement));
    options->putDirect(vm, vm.propertyNames->roundingMode, jsNontrivialString(vm, IntlNumberFormat::roundingModeString(m_roundingMode)));
    options->putDirect(vm, vm.propertyNames->roundingPriority, jsNontrivialString(vm, IntlNumberFormat::roundingPriorityString(m_roundingType)));
    options->putDirect(vm, vm.propertyNames->trailingZeroDisplay, jsNontrivialString(vm, IntlNumberFormat::trailingZeroDisplayString(m_trailingZeroDisplay)));

    return options;
}

// https://tc39.es/ecma402/#sec-resolveplural
JSValue IntlPluralRules::select(JSGlobalObject* globalObject, double value) const
{
    ASSERT(m_pluralRules);

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (!std::isfinite(value))
        return jsNontrivialString(vm, "other"_s);

    UErrorCode status = U_ZERO_ERROR;

    auto formattedNumber = std::unique_ptr<UFormattedNumber, ICUDeleter<unumf_closeResult>>(unumf_openResult(&status));
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select plural value"_s);
    unumf_formatDouble(m_numberFormatter.get(), value, formattedNumber.get(), &status);
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select plural value"_s);
    Vector<char16_t, 32> buffer;
    status = callBufferProducingFunction(uplrules_selectFormatted, m_pluralRules.get(), formattedNumber.get(), buffer);
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select plural value"_s);
    return jsString(vm, String(WTFMove(buffer)));
}

JSValue IntlPluralRules::selectRange(JSGlobalObject* globalObject, double start, double end) const
{
    ASSERT(m_numberRangeFormatter);

    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (std::isnan(start) || std::isnan(end))
        return throwRangeError(globalObject, scope, "Passed numbers are out of range"_s);

    UErrorCode status = U_ZERO_ERROR;
    auto range = std::unique_ptr<UFormattedNumberRange, ICUDeleter<unumrf_closeResult>>(unumrf_openResult(&status));
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select range of plural value"_s);

    unumrf_formatDoubleRange(m_numberRangeFormatter.get(), start, end, range.get(), &status);
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select range of plural value"_s);

    Vector<char16_t, 32> buffer;
    status = callBufferProducingFunction(uplrules_selectForRange, m_pluralRules.get(), range.get(), buffer);
    if (U_FAILURE(status))
        return throwTypeError(globalObject, scope, "failed to select plural value"_s);
    return jsString(vm, String(WTFMove(buffer)));
}

} // namespace JSC
