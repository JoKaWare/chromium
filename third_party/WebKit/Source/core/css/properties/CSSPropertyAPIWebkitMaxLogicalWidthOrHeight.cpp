// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIWebkitMaxLogicalWidthOrHeight.h"

#include "core/css/properties/CSSPropertyLengthUtils.h"

namespace blink {

const CSSValue* CSSPropertyAPIWebkitMaxLogicalWidthOrHeight::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext* context) {
  return CSSPropertyLengthUtils::consumeMaxWidthOrHeight(range, context);
}

}  // namespace blink
