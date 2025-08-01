/*
 *  Copyright (C) 1999-2000,2003 Harri Porten (porten@kde.org)
 *  Copyright (C) 2007-2020 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 *
 */

#include "config.h"
#include "NumberPrototype.h"

#include "BigInteger.h"
#include "IntegrityInlines.h"
#include "IntlNumberFormat.h"
#include "JSCInlines.h"
#include "Operations.h"
#include "ParseInt.h"
#include "Uint16WithFraction.h"
#include <wtf/Assertions.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/dragonbox/dragonbox_to_chars.h>
#include <wtf/dtoa.h>
#include <wtf/dtoa/double-conversion.h>
#include <wtf/text/MakeString.h>

using DoubleToStringConverter = WTF::double_conversion::DoubleToStringConverter;

// To avoid conflict with WTF::StringBuilder.
typedef WTF::double_conversion::StringBuilder DoubleConversionStringBuilder;

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC {

static JSC_DECLARE_HOST_FUNCTION(numberProtoFuncToLocaleString);
static JSC_DECLARE_HOST_FUNCTION(numberProtoFuncToFixed);
static JSC_DECLARE_HOST_FUNCTION(numberProtoFuncToExponential);
static JSC_DECLARE_HOST_FUNCTION(numberProtoFuncToPrecision);

}

#include "NumberPrototype.lut.h"

namespace JSC {

WTF_MAKE_TZONE_ALLOCATED_IMPL(NumericStrings::DoubleCache);

const ClassInfo NumberPrototype::s_info = { "Number"_s, &NumberObject::s_info, &numberPrototypeTable, nullptr, CREATE_METHOD_TABLE(NumberPrototype) };

/* Source for NumberPrototype.lut.h
@begin numberPrototypeTable
  toLocaleString    numberProtoFuncToLocaleString   DontEnum|Function 0
  valueOf           numberProtoFuncValueOf          DontEnum|Function 0
  toFixed           numberProtoFuncToFixed          DontEnum|Function 1
  toExponential     numberProtoFuncToExponential    DontEnum|Function 1
  toPrecision       numberProtoFuncToPrecision      DontEnum|Function 1
@end
*/

STATIC_ASSERT_IS_TRIVIALLY_DESTRUCTIBLE(NumberPrototype);

NumberPrototype::NumberPrototype(VM& vm, Structure* structure)
    : NumberObject(vm, structure)
{
}

void NumberPrototype::finishCreation(VM& vm, JSGlobalObject* globalObject)
{
    Base::finishCreation(vm);
    setInternalValue(vm, jsNumber(0));
    putDirectWithoutTransition(vm, vm.propertyNames->toString, globalObject->numberProtoToStringFunction(), static_cast<unsigned>(PropertyAttribute::DontEnum));
    ASSERT(inherits(info()));
    globalObject->installNumberPrototypeWatchpoint(this);
}

// ------------------------------ Functions ---------------------------

static ALWAYS_INLINE bool toThisNumber(JSValue thisValue, double& x)
{
    if (thisValue.isInt32()) {
        x = thisValue.asInt32();
        return true;
    }

    if (thisValue.isDouble()) {
        x = thisValue.asDouble();
        return true;
    }

    if (auto* numberObject = jsDynamicCast<NumberObject*>(thisValue)) {
        Integrity::auditStructureID(numberObject->structureID());
        x = numberObject->internalValue().asNumber();
        return true;
    }

    return false;
}

static ALWAYS_INLINE EncodedJSValue throwVMToThisNumberError(JSGlobalObject* globalObject, ThrowScope& scope, JSValue thisValue)
{
    auto typeString = jsTypeStringForValue(globalObject, thisValue)->value(globalObject);
    scope.assertNoException();
    return throwVMTypeError(globalObject, scope, WTF::makeString("thisNumberValue called on incompatible "_s, typeString.data));
}

// The largest finite floating point number is 1.mantissa * 2^(0x7fe-0x3ff).
// Since 2^N in binary is a one bit followed by N zero bits. 1 * 2^3ff requires
// at most 1024 characters to the left of a decimal point, in base 2 (1025 if
// we include a minus sign). For the fraction, a value with an exponent of 0
// has up to 52 bits to the right of the decimal point. Each decrement of the
// exponent down to a minimum of -0x3fe adds an additional digit to the length
// of the fraction. As such the maximum fraction size is 1075 (1076 including
// a point). We pick a buffer size such that can simply place the point in the
// center of the buffer, and are guaranteed to have enough space in each direction
// fo any number of digits an IEEE number may require to represent.
typedef char RadixBuffer[2180];

static inline char* int52ToStringWithRadix(char* startOfResultString, int64_t int52Value, unsigned radix)
{
    bool negative = false;
    uint64_t positiveNumber = int52Value;
    if (int52Value < 0) {
        negative = true;
        positiveNumber = -int52Value;
    }

    do {
        uint64_t index = positiveNumber % radix;
        ASSERT(index < sizeof(radixDigits));
        *--startOfResultString = radixDigits[index];
        positiveNumber /= radix;
    } while (positiveNumber);
    if (negative)
        *--startOfResultString = '-';

    return startOfResultString;
}

static char* toStringWithRadixInternal(RadixBuffer& buffer, double originalNumber, unsigned radix)
{
    ASSERT(std::isfinite(originalNumber));
    ASSERT(radix >= 2 && radix <= 36);

    // Position the decimal point at the center of the string, set
    // the startOfResultString pointer to point at the decimal point.
    char* decimalPoint = buffer + sizeof(buffer) / 2;
    char* startOfResultString = decimalPoint;

    // Extract the sign.
    bool isNegative = originalNumber < 0;
    double number = originalNumber;
    if (std::signbit(originalNumber))
        number = -originalNumber;
    double integerPart = floor(number);

    // Check if the value has a fractional part to convert.
    double fractionPart = number - integerPart;
    if (!fractionPart) {
        *decimalPoint = '\0';
        // We do not need to care the negative zero (-0) since it is also converted to "0" in all the radix.
        if (integerPart < (static_cast<int64_t>(1) << (JSValue::numberOfInt52Bits - 1)))
            return int52ToStringWithRadix(startOfResultString, static_cast<int64_t>(originalNumber), radix);
    } else {
        // We use this to test for odd values in odd radix bases.
        // Where the base is even, (e.g. 10), to determine whether a value is even we need only
        // consider the least significant digit. For example, 124 in base 10 is even, because '4'
        // is even. if the radix is odd, then the radix raised to an integer power is also odd.
        // E.g. in base 5, 124 represents (1 * 125 + 2 * 25 + 4 * 5). Since each digit in the value
        // is multiplied by an odd number, the result is even if the sum of all digits is even.
        //
        // For the integer portion of the result, we only need test whether the integer value is
        // even or odd. For each digit of the fraction added, we should invert our idea of whether
        // the number is odd if the new digit is odd.
        //
        // Also initialize digit to this value; for even radix values we only need track whether
        // the last individual digit was odd.
        bool integerPartIsOdd = integerPart <= static_cast<double>(0x1FFFFFFFFFFFFFull) && static_cast<int64_t>(integerPart) & 1;
        ASSERT(integerPartIsOdd == static_cast<bool>(fmod(integerPart, 2)));
        bool isOddInOddRadix = integerPartIsOdd;
        uint32_t digit = integerPartIsOdd;

        // Write the decimal point now.
        *decimalPoint = '.';

        // Higher precision representation of the fractional part.
        Uint16WithFraction fraction(fractionPart);

        bool needsRoundingUp = false;
        char* endOfResultString = decimalPoint + 1;

        // Calculate the delta from the current number to the next & previous possible IEEE numbers.
        double nextNumber = nextafter(number, std::numeric_limits<double>::infinity());
        double lastNumber = nextafter(number, -std::numeric_limits<double>::infinity());
        ASSERT(std::isfinite(nextNumber) && !std::signbit(nextNumber));
        ASSERT(std::isfinite(lastNumber) && !std::signbit(lastNumber));
        double deltaNextDouble = nextNumber - number;
        double deltaLastDouble = number - lastNumber;
        ASSERT(std::isfinite(deltaNextDouble) && !std::signbit(deltaNextDouble));
        ASSERT(std::isfinite(deltaLastDouble) && !std::signbit(deltaLastDouble));

        // We track the delta from the current value to the next, to track how many digits of the
        // fraction we need to write. For example, if the value we are converting is precisely
        // 1.2345, so far we have written the digits "1.23" to a string leaving a remainder of
        // 0.45, and we want to determine whether we can round off, or whether we need to keep
        // appending digits ('4'). We can stop adding digits provided that then next possible
        // lower IEEE value is further from 1.23 than the remainder we'd be rounding off (0.45),
        // which is to say, less than 1.2255. Put another way, the delta between the prior
        // possible value and this number must be more than 2x the remainder we'd be rounding off
        // (or more simply half the delta between numbers must be greater than the remainder).
        //
        // Similarly we need track the delta to the next possible value, to dertermine whether
        // to round up. In almost all cases (other than at exponent boundaries) the deltas to
        // prior and subsequent values are identical, so we don't need track then separately.
        if (deltaNextDouble != deltaLastDouble) {
            // Since the deltas are different track them separately. Pre-multiply by 0.5.
            Uint16WithFraction halfDeltaNext(deltaNextDouble, 1);
            Uint16WithFraction halfDeltaLast(deltaLastDouble, 1);

            while (true) {
                // examine the remainder to determine whether we should be considering rounding
                // up or down. If remainder is precisely 0.5 rounding is to even.
                int dComparePoint5 = fraction.comparePoint5();
                if (dComparePoint5 > 0 || (!dComparePoint5 && (radix & 1 ? isOddInOddRadix : digit & 1))) {
                    // Check for rounding up; are we closer to the value we'd round off to than
                    // the next IEEE value would be?
                    if (fraction.sumGreaterThanOne(halfDeltaNext)) {
                        needsRoundingUp = true;
                        break;
                    }
                } else {
                    // Check for rounding down; are we closer to the value we'd round off to than
                    // the prior IEEE value would be?
                    if (fraction < halfDeltaLast)
                        break;
                }

                ASSERT(endOfResultString < (buffer + sizeof(buffer) - 1));
                // Write a digit to the string.
                fraction *= radix;
                digit = fraction.floorAndSubtract();
                *endOfResultString++ = radixDigits[digit];
                // Keep track whether the portion written is currently even, if the radix is odd.
                if (digit & 1)
                    isOddInOddRadix = !isOddInOddRadix;

                // Shift the fractions by radix.
                halfDeltaNext *= radix;
                halfDeltaLast *= radix;
            }
        } else {
            // This code is identical to that above, except since deltaNextDouble != deltaLastDouble
            // we don't need to track these two values separately.
            Uint16WithFraction halfDelta(deltaNextDouble, 1);

            while (true) {
                int dComparePoint5 = fraction.comparePoint5();
                if (dComparePoint5 > 0 || (!dComparePoint5 && (radix & 1 ? isOddInOddRadix : digit & 1))) {
                    if (fraction.sumGreaterThanOne(halfDelta)) {
                        needsRoundingUp = true;
                        break;
                    }
                } else if (fraction < halfDelta)
                    break;

                ASSERT(endOfResultString < (buffer + sizeof(buffer) - 1));
                fraction *= radix;
                digit = fraction.floorAndSubtract();
                if (digit & 1)
                    isOddInOddRadix = !isOddInOddRadix;
                *endOfResultString++ = radixDigits[digit];

                halfDelta *= radix;
            }
        }

        // Check if the fraction needs rounding off (flag set in the loop writing digits, above).
        if (needsRoundingUp) {
            // Whilst the last digit is the maximum in the current radix, remove it.
            // e.g. rounding up the last digit in "12.3999" is the same as rounding up the
            // last digit in "12.3" - both round up to "12.4".
            while (endOfResultString[-1] == radixDigits[radix - 1])
                --endOfResultString;

            // Radix digits are sequential in ascii/unicode, except for '9' and 'a'.
            // E.g. the first 'if' case handles rounding 67.89 to 67.8a in base 16.
            // The 'else if' case handles rounding of all other digits.
            if (endOfResultString[-1] == '9')
                endOfResultString[-1] = 'a';
            else if (endOfResultString[-1] != '.')
                ++endOfResultString[-1];
            else {
                // One other possibility - there may be no digits to round up in the fraction
                // (or all may be been rounded off already), in which case we may need to
                // round into the integer portion of the number. Remove the decimal point.
                --endOfResultString;
                // In order to get here there must have been a non-zero fraction, in which case
                // there must be at least one bit of the value's mantissa not in use in the
                // integer part of the number. As such, adding to the integer part should not
                // be able to lose precision.
                ASSERT((integerPart + 1) - integerPart == 1);
                ++integerPart;
            }
        } else {
            // We only need to check for trailing zeros if the value does not get rounded up.
            while (endOfResultString[-1] == '0')
                --endOfResultString;
        }

        *endOfResultString = '\0';
        ASSERT(endOfResultString < buffer + sizeof(buffer));
    }

    BigInteger units(integerPart);

    // Always loop at least once, to emit at least '0'.
    do {
        ASSERT(buffer < startOfResultString);

        // Read a single digit and write it to the front of the string.
        // Divide by radix to remove one digit from the value.
        uint32_t digit = units.divide(radix);
        *--startOfResultString = radixDigits[digit];
    } while (!!units);

    // If the number is negative, prepend '-'.
    if (isNegative)
        *--startOfResultString = '-';
    ASSERT(buffer <= startOfResultString);

    return startOfResultString;
}

static String toStringWithRadixInternal(int32_t number, unsigned radix)
{
    LChar buf[1 + 32]; // Worst case is radix == 2, which gives us 32 digits + sign.
    LChar* end = std::end(buf);
    LChar* p = end;

    bool negative = false;
    uint32_t positiveNumber = number;
    if (number < 0) {
        negative = true;
        positiveNumber = static_cast<uint32_t>(-static_cast<int64_t>(number));
    }

    // Always loop at least once, to emit at least '0'.
    do {
        uint32_t index = positiveNumber % radix;
        ASSERT(index < sizeof(radixDigits));
        *--p = radixDigits[index];
        positiveNumber /= radix;
    } while (positiveNumber);

    if (negative)
        *--p = '-';

    return String({ p, end });
}

String toStringWithRadix(double doubleValue, int32_t radix)
{
    ASSERT(2 <= radix && radix <= 36);

    int32_t integerValue = static_cast<int32_t>(doubleValue);
    if (integerValue == doubleValue)
        return toStringWithRadixInternal(integerValue, radix);

    if (radix == 10 || !std::isfinite(doubleValue))
        return String::number(doubleValue);

    RadixBuffer buffer;
    return String::fromLatin1(toStringWithRadixInternal(buffer, doubleValue, radix));
}

// toExponential converts a number to a string, always formatting as an exponential.
// This method takes an optional argument specifying a number of *decimal places*
// to round the significand to (or, put another way, this method optionally rounds
// to argument-plus-one significant figures).
JSC_DEFINE_HOST_FUNCTION(numberProtoFuncToExponential, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double x;
    if (!toThisNumber(callFrame->thisValue(), x))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());

    JSValue arg = callFrame->argument(0);
    // Perform ToInteger on the argument before remaining steps.
    int decimalPlaces = static_cast<int>(arg.toIntegerOrInfinity(globalObject));
    RETURN_IF_EXCEPTION(scope, { });

    // Handle NaN and Infinity.
    if (!std::isfinite(x))
        return JSValue::encode(jsNontrivialString(vm, String::number(x)));

    if (decimalPlaces < 0 || decimalPlaces > 100)
        return throwVMRangeError(globalObject, scope, "toExponential() argument must be between 0 and 100"_s);

    // Round if the argument is not undefined, always format as exponential.
    NumberToStringBuffer buffer;
    DoubleConversionStringBuilder builder { std::span<char> { buffer } };
    builder.Reset();
    if (arg.isUndefined())
        WTF::dragonbox::ToExponential(x, &builder);
    else {
        const DoubleToStringConverter& converter = DoubleToStringConverter::EcmaScriptConverter();
        converter.ToExponential(x, decimalPlaces, &builder);
    }
    return JSValue::encode(jsString(vm, String { builder.Finalize() }));
}

// toFixed converts a number to a string, always formatting as an a decimal fraction.
// This method takes an argument specifying a number of decimal places to round the
// significand to. However when converting large values (1e+21 and above) this
// method will instead fallback to calling ToString. 
JSC_DEFINE_HOST_FUNCTION(numberProtoFuncToFixed, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double x;
    if (!toThisNumber(callFrame->thisValue(), x))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());

    int decimalPlaces = static_cast<int>(callFrame->argument(0).toIntegerOrInfinity(globalObject));
    RETURN_IF_EXCEPTION(scope, { });
    if (decimalPlaces < 0 || decimalPlaces > 100)
        return throwVMRangeError(globalObject, scope, "toFixed() argument must be between 0 and 100"_s);

    // 15.7.4.5.7 states "If x >= 10^21, then let m = ToString(x)"
    // This also covers Ininity, and structure the check so that NaN
    // values are also handled by numberToString
    if (!(std::abs(x) < 1e+21))
        return JSValue::encode(jsString(vm, String::number(x)));

    // The check above will return false for NaN or Infinity, these will be
    // handled by numberToString.
    ASSERT(std::isfinite(x));

    return JSValue::encode(jsString(vm, String::numberToStringFixedWidth(x, decimalPlaces)));
}

// toPrecision converts a number to a string, taking an argument specifying a
// number of significant figures to round the significand to. For positive
// exponent, all values that can be represented using a decimal fraction will
// be, e.g. when rounding to 3 s.f. any value up to 999 will be formated as a
// decimal, whilst 1000 is converted to the exponential representation 1.00e+3.
// For negative exponents values >= 1e-6 are formated as decimal fractions,
// with smaller values converted to exponential representation.
JSC_DEFINE_HOST_FUNCTION(numberProtoFuncToPrecision, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double x;
    if (!toThisNumber(callFrame->thisValue(), x))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());

    JSValue arg = callFrame->argument(0);
    // To precision called with no argument is treated as ToString.
    if (arg.isUndefined())
        return JSValue::encode(jsString(vm, String::number(x)));

    // Perform ToInteger on the argument before remaining steps.
    int significantFigures = static_cast<int>(arg.toIntegerOrInfinity(globalObject));
    RETURN_IF_EXCEPTION(scope, { });

    // Handle NaN and Infinity.
    if (!std::isfinite(x))
        return JSValue::encode(jsNontrivialString(vm, String::number(x)));

    if (significantFigures < 1 || significantFigures > 100)
        return throwVMRangeError(globalObject, scope, "toPrecision() argument must be between 1 and 100"_s);

    return JSValue::encode(jsString(vm, String::numberToStringFixedPrecision(x, significantFigures, TrailingZerosPolicy::Keep)));
}

JSString* NumericStrings::addJSString(VM& vm, int i)
{
    if (static_cast<unsigned>(i) < cacheSize) {
        auto& entry = lookupSmallString(static_cast<unsigned>(i));
        if (entry.jsString)
            return entry.jsString;
        entry.jsString = jsNontrivialString(vm, entry.value);
        return entry.jsString;
    }
    auto& entry = lookup(i);
    if (i != entry.key || entry.value.isNull()) {
        entry.key = i;
        entry.value = String::number(i);
    } else {
        if (entry.jsString)
            return entry.jsString;
    }
    entry.jsString = jsNontrivialString(vm, entry.value);
    return entry.jsString;
}

JSString* NumericStrings::addJSString(VM& vm, double value)
{
    if (!m_doubleCache) [[unlikely]]
        initializeDoubleCache();
    auto& entry = lookup(value);
    if (value != entry.key || entry.value.isNull()) {
        entry.key = value;
        entry.value = String::number(value);
    } else {
        if (entry.jsString)
            return entry.jsString;
    }
    entry.jsString = jsNontrivialString(vm, entry.value);
    return entry.jsString;
}

void NumericStrings::initializeSmallIntCache(VM& vm)
{
    for (int i = 0; i < 10; ++i) {
        auto* string = vm.smallStrings.singleCharacterString(i + '0');
        auto& entry = lookupSmallString(static_cast<unsigned>(i));
        entry.jsString = string;
        ASSERT(string->tryGetValueImpl());
        entry.value = string->tryGetValue();
    }
}

static ALWAYS_INLINE JSString* int32ToStringInternal(VM& vm, int32_t value, int32_t radix)
{
    ASSERT(!(radix < 2 || radix > 36));

    if (radix == 10)
        return vm.numericStrings.addJSString(vm, value);

    // A negative value casted to unsigned would be bigger than 36 (the max radix).
    if (static_cast<unsigned>(value) < static_cast<unsigned>(radix)) {
        ASSERT(value <= 36);
        ASSERT(value >= 0);
        return vm.smallStrings.singleCharacterString(radixDigits[value]);
    }

    return jsNontrivialString(vm, toStringWithRadixInternal(value, radix));

}

static ALWAYS_INLINE JSString* numberToStringInternal(VM& vm, double doubleValue, int32_t radix)
{
    ASSERT(!(radix < 2 || radix > 36));

    int32_t integerValue = static_cast<int32_t>(doubleValue);
    if (integerValue == doubleValue)
        return int32ToStringInternal(vm, integerValue, radix);

    if (radix == 10)
        return vm.numericStrings.addJSString(vm, doubleValue);

    if (!std::isfinite(doubleValue))
        return jsNontrivialString(vm, String::number(doubleValue));

    RadixBuffer buffer;
    return jsString(vm, String::fromLatin1(toStringWithRadixInternal(buffer, doubleValue, radix)));
}

JSString* int32ToString(VM& vm, int32_t value, int32_t radix)
{
    return int32ToStringInternal(vm, value, radix);
}

JSString* int52ToString(VM& vm, int64_t value, int32_t radix)
{
    ASSERT(!(radix < 2 || radix > 36));
    // A negative value casted to unsigned would be bigger than 36 (the max radix).
    if (static_cast<uint64_t>(value) < static_cast<uint64_t>(radix)) {
        ASSERT(value <= 36);
        ASSERT(value >= 0);
        return vm.smallStrings.singleCharacterString(radixDigits[value]);
    }

    if (isInRange<int64_t>(value, INT32_MIN, INT32_MAX))
        return int32ToString(vm, static_cast<int32_t>(value), radix);

    if (radix == 10)
        return jsNontrivialString(vm, vm.numericStrings.add(static_cast<double>(value)));

    // Position the decimal point at the center of the string, set
    // the startOfResultString pointer to point at the decimal point.
    RadixBuffer buffer;
    char* decimalPoint = buffer + sizeof(buffer) / 2;
    char* startOfResultString = decimalPoint;
    *decimalPoint = '\0';

    return jsNontrivialString(vm, String::fromLatin1(int52ToStringWithRadix(startOfResultString, value, radix)));
}

JSString* numberToString(VM& vm, double doubleValue, int32_t radix)
{
    return numberToStringInternal(vm, doubleValue, radix);
}

JSC_DEFINE_HOST_FUNCTION(numberProtoFuncToString, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double doubleValue;
    if (!toThisNumber(callFrame->thisValue(), doubleValue))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());

    auto radix = extractToStringRadixArgument(globalObject, callFrame->argument(0), scope);
    RETURN_IF_EXCEPTION(scope, encodedJSValue());

    return JSValue::encode(numberToStringInternal(vm, doubleValue, radix));
}

JSC_DEFINE_HOST_FUNCTION(numberProtoFuncToLocaleString, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double x;
    if (!toThisNumber(callFrame->thisValue(), x))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());

    JSValue locales = callFrame->argument(0);
    JSValue options = callFrame->argument(1);
    IntlNumberFormat* numberFormat = nullptr;
    if (locales.isUndefined() && options.isUndefined())
        numberFormat = globalObject->defaultNumberFormat();
    else {
        numberFormat = IntlNumberFormat::create(vm, globalObject->numberFormatStructure());
        numberFormat->initializeNumberFormat(globalObject, locales, options);
    }
    RETURN_IF_EXCEPTION(scope, { });

    RELEASE_AND_RETURN(scope, JSValue::encode(numberFormat->format(globalObject, x)));
}

JSC_DEFINE_HOST_FUNCTION(numberProtoFuncValueOf, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    double x;
    JSValue thisValue = callFrame->thisValue();
    if (!toThisNumber(thisValue, x))
        return throwVMToThisNumberError(globalObject, scope, callFrame->thisValue());
    return JSValue::encode(jsNumber(x));
}

int32_t extractToStringRadixArgument(JSGlobalObject* globalObject, JSValue radixValue, ThrowScope& throwScope)
{
    if (radixValue.isUndefined())
        return 10;

    if (radixValue.isInt32()) {
        int32_t radix = radixValue.asInt32();
        if (radix >= 2 && radix <= 36)
            return radix;
    } else {
        double radixDouble = radixValue.toIntegerOrInfinity(globalObject);
        RETURN_IF_EXCEPTION(throwScope, 0);
        if (radixDouble >= 2 && radixDouble <= 36)
            return static_cast<int32_t>(radixDouble);   
    }

    throwRangeError(globalObject, throwScope, "toString() radix argument must be between 2 and 36"_s);
    return 0;
}

void NumericStrings::initializeDoubleCache()
{
    ASSERT(!m_doubleCache);
    m_doubleCache = makeUnique<DoubleCache>();
}

} // namespace JSC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
