// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIWebkitMargin.h"

#include "core/css/parser/CSSParserContext.h"
#include "core/css/parser/CSSPropertyParserHelpers.h"
#include "core/css/properties/CSSPropertyMarginUtils.h"

namespace blink {

const CSSValue* CSSPropertyAPIWebkitMargin::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext* context) {
  return CSSPropertyMarginUtils::consumeMarginOrOffset(
      range, context->mode(), CSSPropertyParserHelpers::UnitlessQuirk::Forbid);
}

}  // namespace blink
