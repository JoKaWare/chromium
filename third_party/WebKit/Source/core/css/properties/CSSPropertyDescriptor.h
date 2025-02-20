// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/CSSPropertyNames.h"

namespace blink {

class CSSValue;
class CSSParserTokenRange;
class CSSParserContext;

#define FUNCTION_IMPLEMENTED_FOR_PROPERTY(function, descriptor) \
  descriptor.function != CSSPropertyAPI::function

// Stores function pointers matching those declared in CSSPropertyAPI.
struct CSSPropertyDescriptor {
  const CSSValue* (*parseSingleValue)(CSSParserTokenRange&,
                                      const CSSParserContext*);
  bool (*parseShorthand)(bool, CSSParserTokenRange&, const CSSParserContext*);

  // Returns the corresponding CSSPropertyDescriptor for a given CSSPropertyID.
  // Use this function to access the API for a property. Returns a descriptor
  // with isValid set to false if no descriptor exists for this ID.
  static const CSSPropertyDescriptor& get(CSSPropertyID);
};

}  // namespace blink
