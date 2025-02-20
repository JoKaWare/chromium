/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "core/html/shadow/DateTimeNumericFieldElement.h"

#include "core/CSSPropertyNames.h"
#include "core/CSSValueKeywords.h"
#include "core/events/KeyboardEvent.h"
#include "platform/fonts/Font.h"
#include "platform/text/PlatformLocale.h"
#include "platform/text/TextRun.h"

using namespace WTF::Unicode;

namespace blink {

int DateTimeNumericFieldElement::Range::clampValue(int value) const {
  return std::min(std::max(value, minimum), maximum);
}

bool DateTimeNumericFieldElement::Range::isInRange(int value) const {
  return value >= minimum && value <= maximum;
}

// ----------------------------

DateTimeNumericFieldElement::DateTimeNumericFieldElement(
    Document& document,
    FieldOwner& fieldOwner,
    const Range& range,
    const Range& hardLimits,
    const String& placeholder,
    const DateTimeNumericFieldElement::Step& step)
    : DateTimeFieldElement(document, fieldOwner),
      m_placeholder(placeholder),
      m_range(range),
      m_hardLimits(hardLimits),
      m_step(step),
      m_value(0),
      m_hasValue(false) {
  DCHECK_NE(m_step.step, 0);
  DCHECK_LE(m_range.minimum, m_range.maximum);
  DCHECK_LE(m_hardLimits.minimum, m_hardLimits.maximum);

  // We show a direction-neutral string such as "--" as a placeholder. It
  // should follow the direction of numeric values.
  if (localeForOwner().isRTL()) {
    CharDirection dir = direction(formatValue(this->maximum())[0]);
    if (dir == LeftToRight || dir == EuropeanNumber || dir == ArabicNumber) {
      setInlineStyleProperty(CSSPropertyUnicodeBidi, CSSValueBidiOverride);
      setInlineStyleProperty(CSSPropertyDirection, CSSValueLtr);
    }
  }
}

float DateTimeNumericFieldElement::maximumWidth(const ComputedStyle& style) {
  float maximumWidth = computeTextWidth(style, m_placeholder);
  maximumWidth =
      std::max(maximumWidth, computeTextWidth(style, formatValue(maximum())));
  maximumWidth = std::max(maximumWidth, computeTextWidth(style, value()));
  return maximumWidth + DateTimeFieldElement::maximumWidth(style);
}

int DateTimeNumericFieldElement::defaultValueForStepDown() const {
  return m_range.maximum;
}

int DateTimeNumericFieldElement::defaultValueForStepUp() const {
  return m_range.minimum;
}

void DateTimeNumericFieldElement::setFocused(bool value) {
  if (!value) {
    int value = typeAheadValue();
    m_typeAheadBuffer.clear();
    if (value >= 0)
      setValueAsInteger(value, DispatchEvent);
  }
  DateTimeFieldElement::setFocused(value);
}

String DateTimeNumericFieldElement::formatValue(int value) const {
  Locale& locale = localeForOwner();
  if (m_hardLimits.maximum > 999)
    return locale.convertToLocalizedNumber(String::format("%04d", value));
  if (m_hardLimits.maximum > 99)
    return locale.convertToLocalizedNumber(String::format("%03d", value));
  return locale.convertToLocalizedNumber(String::format("%02d", value));
}

void DateTimeNumericFieldElement::handleKeyboardEvent(
    KeyboardEvent* keyboardEvent) {
  DCHECK(!isDisabled());
  if (keyboardEvent->type() != EventTypeNames::keypress)
    return;

  UChar charCode = static_cast<UChar>(keyboardEvent->charCode());
  String number =
      localeForOwner().convertFromLocalizedNumber(String(&charCode, 1));
  const int digit = number[0] - '0';
  if (digit < 0 || digit > 9)
    return;

  unsigned maximumLength =
      DateTimeNumericFieldElement::formatValue(m_range.maximum).length();
  if (m_typeAheadBuffer.length() >= maximumLength) {
    String current = m_typeAheadBuffer.toString();
    m_typeAheadBuffer.clear();
    unsigned desiredLength = maximumLength - 1;
    m_typeAheadBuffer.append(current, current.length() - desiredLength,
                             desiredLength);
  }
  m_typeAheadBuffer.append(number);
  int newValue = typeAheadValue();
  if (newValue >= m_hardLimits.minimum) {
    setValueAsInteger(newValue, DispatchEvent);
  } else {
    m_hasValue = false;
    updateVisibleValue(DispatchEvent);
  }

  if (m_typeAheadBuffer.length() >= maximumLength ||
      newValue * 10 > m_range.maximum)
    focusOnNextField();

  keyboardEvent->setDefaultHandled();
}

bool DateTimeNumericFieldElement::hasValue() const {
  return m_hasValue;
}

void DateTimeNumericFieldElement::initialize(const AtomicString& pseudo,
                                             const String& axHelpText) {
  DateTimeFieldElement::initialize(pseudo, axHelpText, m_range.minimum,
                                   m_range.maximum);
}

int DateTimeNumericFieldElement::maximum() const {
  return m_range.maximum;
}

void DateTimeNumericFieldElement::setEmptyValue(EventBehavior eventBehavior) {
  if (isDisabled())
    return;

  m_hasValue = false;
  m_value = 0;
  m_typeAheadBuffer.clear();
  updateVisibleValue(eventBehavior);
}

void DateTimeNumericFieldElement::setValueAsInteger(
    int value,
    EventBehavior eventBehavior) {
  m_value = m_hardLimits.clampValue(value);
  m_hasValue = true;
  updateVisibleValue(eventBehavior);
}

void DateTimeNumericFieldElement::stepDown() {
  int newValue =
      roundDown(m_hasValue ? m_value - 1 : defaultValueForStepDown());
  if (!m_range.isInRange(newValue))
    newValue = roundDown(m_range.maximum);
  m_typeAheadBuffer.clear();
  setValueAsInteger(newValue, DispatchEvent);
}

void DateTimeNumericFieldElement::stepUp() {
  int newValue = roundUp(m_hasValue ? m_value + 1 : defaultValueForStepUp());
  if (!m_range.isInRange(newValue))
    newValue = roundUp(m_range.minimum);
  m_typeAheadBuffer.clear();
  setValueAsInteger(newValue, DispatchEvent);
}

String DateTimeNumericFieldElement::value() const {
  return m_hasValue ? formatValue(m_value) : emptyString;
}

int DateTimeNumericFieldElement::valueAsInteger() const {
  return m_hasValue ? m_value : -1;
}

int DateTimeNumericFieldElement::typeAheadValue() const {
  if (m_typeAheadBuffer.length())
    return m_typeAheadBuffer.toString().toInt();
  return -1;
}

String DateTimeNumericFieldElement::visibleValue() const {
  if (m_typeAheadBuffer.length())
    return formatValue(typeAheadValue());
  return m_hasValue ? value() : m_placeholder;
}

int DateTimeNumericFieldElement::roundDown(int n) const {
  n -= m_step.stepBase;
  if (n >= 0)
    n = n / m_step.step * m_step.step;
  else
    n = -((-n + m_step.step - 1) / m_step.step * m_step.step);
  return n + m_step.stepBase;
}

int DateTimeNumericFieldElement::roundUp(int n) const {
  n -= m_step.stepBase;
  if (n >= 0)
    n = (n + m_step.step - 1) / m_step.step * m_step.step;
  else
    n = -(-n / m_step.step * m_step.step);
  return n + m_step.stepBase;
}

}  // namespace blink
