/*
 * Copyright (C) 2016-2023 Apple Inc. All rights reserved.
 * Copyright (C) 2024 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CSSPropertyParserConsumer+Content.h"

#include "CSSCounterValue.h"
#include "CSSParserContext.h"
#include "CSSParserTokenRange.h"
#include "CSSPrimitiveValue.h"
#include "CSSPropertyParserConsumer+Attr.h"
#include "CSSPropertyParserConsumer+CounterStyles.h"
#include "CSSPropertyParserConsumer+Ident.h"
#include "CSSPropertyParserConsumer+Image.h"
#include "CSSPropertyParserConsumer+Lists.h"
#include "CSSPropertyParserConsumer+Primitives.h"
#include "CSSPropertyParserConsumer+String.h"
#include "CSSPropertyParserState.h"
#include "CSSPropertyParsing.h"
#include "CSSValueKeywords.h"
#include "CSSValueList.h"
#include "CSSValuePair.h"

namespace WebCore {
namespace CSSPropertyParserHelpers {

RefPtr<CSSValue> consumeQuotes(CSSParserTokenRange& range, CSS::PropertyParserState&)
{
    // <'quotes'> = auto | none | match-parent | [ <string> <string> ]+
    // https://drafts.csswg.org/css-content-3/#propdef-quotes

    // FIXME: Support `match-parent`.

    auto id = range.peek().id();
    if (id == CSSValueNone || id == CSSValueAuto)
        return consumeIdent(range);

    CSSValueListBuilder values;
    while (!range.atEnd()) {
        auto parsedValue = consumeString(range);
        if (!parsedValue)
            return nullptr;
        values.append(parsedValue.releaseNonNull());
    }
    if (values.size() && !(values.size() % 2))
        return CSSValueList::createSpaceSeparated(WTFMove(values));
    return nullptr;
}

static RefPtr<CSSValue> consumeCounterContent(CSSParserTokenRange args, CSS::PropertyParserState& state)
{
    // counter()  =  counter( <counter-name>, <counter-style>? )
    // https://www.w3.org/TR/css-lists-3/#funcdef-counter

    AtomString identifier { consumeCustomIdentRaw(args) };
    if (identifier.isNull())
        return nullptr;

    RefPtr<CSSValue> counterStyle;
    if (consumeCommaIncludingWhitespace(args)) {
        counterStyle = consumeCounterStyle(args, state);
        if (!counterStyle)
            return nullptr;
    }
    if (!counterStyle)
        counterStyle = CSSPrimitiveValue::create(CSSValueDecimal);

    if (!args.atEnd())
        return nullptr;

    return CSSCounterValue::create(WTFMove(identifier), AtomString { nullAtom() }, counterStyle.releaseNonNull());
}

static RefPtr<CSSValue> consumeCountersContent(CSSParserTokenRange args, CSS::PropertyParserState& state)
{
    // counters() = counters( <counter-name>, <string>, <counter-style>? )
    // https://www.w3.org/TR/css-lists-3/#funcdef-counters

    AtomString identifier { consumeCustomIdentRaw(args) };
    if (identifier.isNull())
        return nullptr;

    if (!consumeCommaIncludingWhitespace(args) || args.peek().type() != StringToken)
        return nullptr;
    auto separator = args.consumeIncludingWhitespace().value().toAtomString();

    RefPtr<CSSValue> counterStyle;
    if (consumeCommaIncludingWhitespace(args)) {
        counterStyle = consumeCounterStyle(args, state);
        if (!counterStyle)
            return nullptr;
    }
    if (!counterStyle)
        counterStyle = CSSPrimitiveValue::create(CSSValueDecimal);

    if (!args.atEnd())
        return nullptr;

    return CSSCounterValue::create(WTFMove(identifier), WTFMove(separator), counterStyle.releaseNonNull());
}

RefPtr<CSSValue> consumeContent(CSSParserTokenRange& range, CSS::PropertyParserState& state)
{
    // Standard says this should be:
    //
    // <'content'> = normal | none | [ <content-replacement> | <content-list> ] [/ [ <string> | <counter> | <attr()> ]+ ]?
    // https://drafts.csswg.org/css-content-3/#propdef-content

    if (identMatches<CSSValueNone, CSSValueNormal>(range.peek().id()))
        return consumeIdent(range);

    enum class ContentListType : bool { VisibleContent, AltText };
    auto consumeContentList = [&](CSSValueListBuilder& values, ContentListType type) -> bool {
        bool shouldEnd = false;
        do {
            RefPtr<CSSValue> parsedValue = consumeString(range);
            if (type == ContentListType::VisibleContent) {
                if (!parsedValue)
                    parsedValue = consumeImage(range, state);
                if (!parsedValue)
                    parsedValue = consumeIdent<CSSValueOpenQuote, CSSValueCloseQuote, CSSValueNoOpenQuote, CSSValueNoCloseQuote>(range);
            }
            if (!parsedValue) {
                if (range.peek().functionId() == CSSValueAttr)
                    parsedValue = consumeAttr(consumeFunction(range), state);
                // FIXME: Alt-text should support counters.
                else if (type == ContentListType::VisibleContent) {
                    if (range.peek().functionId() == CSSValueCounter)
                        parsedValue = consumeCounterContent(consumeFunction(range), state);
                    else if (range.peek().functionId() == CSSValueCounters)
                        parsedValue = consumeCountersContent(consumeFunction(range), state);
                }
                if (!parsedValue)
                    return false;
            }
            values.append(parsedValue.releaseNonNull());

            // Visible content parsing ends at '/' or end of range.
            if (type == ContentListType::VisibleContent && !range.atEnd()) {
                CSSParserToken value = range.peek();
                if (value.type() == DelimiterToken && value.delimiter() == '/')
                    shouldEnd = true;
            }
            shouldEnd = shouldEnd || range.atEnd();
        } while (!shouldEnd);
        return true;
    };

    CSSValueListBuilder visibleContent;
    if (!consumeContentList(visibleContent, ContentListType::VisibleContent))
        return nullptr;

    // Consume alt-text content if there is any.
    if (consumeSlashIncludingWhitespace(range)) {
        CSSValueListBuilder altText;
        if (!consumeContentList(altText, ContentListType::AltText))
            return nullptr;
        return CSSValuePair::createSlashSeparated(
            CSSValueList::createSpaceSeparated(WTFMove(visibleContent)),
            CSSValueList::createSpaceSeparated(WTFMove(altText))
        );
    }

    return CSSValueList::createSpaceSeparated(WTFMove(visibleContent));
}

} // namespace CSSPropertyParserHelpers
} // namespace WebCore
