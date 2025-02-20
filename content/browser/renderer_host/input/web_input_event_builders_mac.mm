// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Copyright (C) 2004, 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2006-2009 Google Inc.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "content/browser/renderer_host/input/web_input_event_builders_mac.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>

#include <stdint.h>

#include "base/mac/sdk_forward_declarations.h"
#include "base/strings/string_util.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/base/cocoa/cocoa_base_utils.h"
#include "ui/events/blink/blink_event_util.h"
#import "ui/events/cocoa/cocoa_event_utils.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/keyboard_code_conversion_mac.h"

namespace content {

namespace {

inline NSString* FilterSpecialCharacter(NSString* str) {
  if ([str length] != 1)
    return str;
  unichar c = [str characterAtIndex:0];
  NSString* result = str;
  if (c == 0x7F) {
    // Backspace should be 8
    result = @"\x8";
  } else if (c >= 0xF700 && c <= 0xF7FF) {
    // Mac private use characters should be @"\0" (@"" won't work)
    // NSDeleteFunctionKey will also go into here
    // Use the range 0xF700~0xF7FF to match
    // http://www.opensource.apple.com/source/WebCore/WebCore-7601.1.55/platform/mac/KeyEventMac.mm
    result = @"\0";
  }
  return result;
}

inline NSString* TextFromEvent(NSEvent* event) {
  if ([event type] == NSFlagsChanged)
    return @"";
  return FilterSpecialCharacter([event characters]);
}

inline NSString* UnmodifiedTextFromEvent(NSEvent* event) {
  if ([event type] == NSFlagsChanged)
    return @"";
  return FilterSpecialCharacter([event charactersIgnoringModifiers]);
}

// End Apple code.
// ----------------------------------------------------------------------------

int ModifiersFromEvent(NSEvent* event) {
  int modifiers = 0;

  if ([event modifierFlags] & NSControlKeyMask)
    modifiers |= blink::WebInputEvent::ControlKey;
  if ([event modifierFlags] & NSShiftKeyMask)
    modifiers |= blink::WebInputEvent::ShiftKey;
  if ([event modifierFlags] & NSAlternateKeyMask)
    modifiers |= blink::WebInputEvent::AltKey;
  if ([event modifierFlags] & NSCommandKeyMask)
    modifiers |= blink::WebInputEvent::MetaKey;
  if ([event modifierFlags] & NSAlphaShiftKeyMask)
    modifiers |= blink::WebInputEvent::CapsLockOn;

  // The return value of 1 << 0 corresponds to the left mouse button,
  // 1 << 1 corresponds to the right mouse button,
  // 1 << n, n >= 2 correspond to other mouse buttons.
  NSUInteger pressed_buttons = [NSEvent pressedMouseButtons];

  if (pressed_buttons & (1 << 0))
    modifiers |= blink::WebInputEvent::LeftButtonDown;
  if (pressed_buttons & (1 << 1))
    modifiers |= blink::WebInputEvent::RightButtonDown;
  if (pressed_buttons & (1 << 2))
    modifiers |= blink::WebInputEvent::MiddleButtonDown;

  return modifiers;
}

void SetWebEventLocationFromEventInView(blink::WebMouseEvent* result,
                                        NSEvent* event,
                                        NSView* view) {
  NSPoint screen_local = ui::ConvertPointFromWindowToScreen(
      [view window], [event locationInWindow]);
  result->globalX = screen_local.x;
  // Flip y.
  NSScreen* primary_screen = ([[NSScreen screens] count] > 0)
                                 ? [[NSScreen screens] firstObject]
                                 : nil;
  if (primary_screen)
    result->globalY = [primary_screen frame].size.height - screen_local.y;
  else
    result->globalY = screen_local.y;

  NSPoint content_local =
      [view convertPoint:[event locationInWindow] fromView:nil];
  result->x = content_local.x;
  result->y = [view frame].size.height - content_local.y;  // Flip y.

  result->windowX = result->x;
  result->windowY = result->y;

  result->movementX = [event deltaX];
  result->movementY = [event deltaY];
}

bool IsSystemKeyEvent(const blink::WebKeyboardEvent& event) {
  // Windows and Linux set |isSystemKey| if alt is down. Blink looks at this
  // flag to decide if it should handle a key or not. E.g. alt-left/right
  // shouldn't be used by Blink to scroll the current page, because we want
  // to get that key back for it to do history navigation. Hence, the
  // corresponding situation on OS X is to set this for cmd key presses.

  // cmd-b and and cmd-i are system wide key bindings that OS X doesn't
  // handle for us, so the editor handles them.
  int modifiers = event.modifiers() & blink::WebInputEvent::InputModifiers;
  if (modifiers == blink::WebInputEvent::MetaKey &&
      event.windowsKeyCode == ui::VKEY_B)
    return false;
  if (modifiers == blink::WebInputEvent::MetaKey &&
      event.windowsKeyCode == ui::VKEY_I)
    return false;

  return event.modifiers() & blink::WebInputEvent::MetaKey;
}

blink::WebMouseWheelEvent::Phase PhaseForNSEventPhase(
    NSEventPhase event_phase) {
  uint32_t phase = blink::WebMouseWheelEvent::PhaseNone;
  if (event_phase & NSEventPhaseBegan)
    phase |= blink::WebMouseWheelEvent::PhaseBegan;
  if (event_phase & NSEventPhaseStationary)
    phase |= blink::WebMouseWheelEvent::PhaseStationary;
  if (event_phase & NSEventPhaseChanged)
    phase |= blink::WebMouseWheelEvent::PhaseChanged;
  if (event_phase & NSEventPhaseEnded)
    phase |= blink::WebMouseWheelEvent::PhaseEnded;
  if (event_phase & NSEventPhaseCancelled)
    phase |= blink::WebMouseWheelEvent::PhaseCancelled;
  if (event_phase & NSEventPhaseMayBegin)
    phase |= blink::WebMouseWheelEvent::PhaseMayBegin;
  return static_cast<blink::WebMouseWheelEvent::Phase>(phase);
}

blink::WebMouseWheelEvent::Phase PhaseForEvent(NSEvent* event) {
  if (![event respondsToSelector:@selector(phase)])
    return blink::WebMouseWheelEvent::PhaseNone;

  NSEventPhase event_phase = [event phase];
  return PhaseForNSEventPhase(event_phase);
}

blink::WebMouseWheelEvent::Phase MomentumPhaseForEvent(NSEvent* event) {
  if (![event respondsToSelector:@selector(momentumPhase)])
    return blink::WebMouseWheelEvent::PhaseNone;

  NSEventPhase event_momentum_phase = [event momentumPhase];
  return PhaseForNSEventPhase(event_momentum_phase);
}

ui::DomKey DomKeyFromEvent(NSEvent* event) {
  ui::DomKey key = ui::DomKeyFromNSEvent(event);
  if (key != ui::DomKey::NONE)
    return key;
  return ui::DomKey::UNIDENTIFIED;
}

}  // namespace

blink::WebKeyboardEvent WebKeyboardEventBuilder::Build(NSEvent* event) {
  ui::DomCode dom_code = ui::DomCodeFromNSEvent(event);
  int modifiers =
      ModifiersFromEvent(event) | ui::DomCodeToWebInputEventModifiers(dom_code);

  if (([event type] != NSFlagsChanged) && [event isARepeat])
    modifiers |= blink::WebInputEvent::IsAutoRepeat;

  blink::WebKeyboardEvent result(ui::IsKeyUpEvent(event)
                                     ? blink::WebInputEvent::KeyUp
                                     : blink::WebInputEvent::RawKeyDown,
                                 modifiers, [event timestamp]);
  result.windowsKeyCode =
      ui::LocatedToNonLocatedKeyboardCode(ui::KeyboardCodeFromNSEvent(event));
  result.nativeKeyCode = [event keyCode];
  result.domCode = static_cast<int>(dom_code);
  result.domKey = DomKeyFromEvent(event);
  NSString* text_str = TextFromEvent(event);
  NSString* unmodified_str = UnmodifiedTextFromEvent(event);

  // Begin Apple code, copied from KeyEventMac.mm

  // Always use 13 for Enter/Return -- we don't want to use AppKit's
  // different character for Enter.
  if (result.windowsKeyCode == '\r') {
    text_str = @"\r";
    unmodified_str = @"\r";
  }

  // Always use 9 for tab -- we don't want to use AppKit's different character
  // for shift-tab.
  if (result.windowsKeyCode == 9) {
    text_str = @"\x9";
    unmodified_str = @"\x9";
  }

  // End Apple code.

  if ([text_str length] < blink::WebKeyboardEvent::textLengthCap &&
      [unmodified_str length] < blink::WebKeyboardEvent::textLengthCap) {
    [text_str getCharacters:&result.text[0]];
    [unmodified_str getCharacters:&result.unmodifiedText[0]];
  } else
    NOTIMPLEMENTED();

  result.isSystemKey = IsSystemKeyEvent(result);

  return result;
}

// WebMouseEvent --------------------------------------------------------------

blink::WebMouseEvent WebMouseEventBuilder::Build(
    NSEvent* event,
    NSView* view,
    blink::WebPointerProperties::PointerType pointerType) {
  blink::WebInputEvent::Type event_type = blink::WebInputEvent::Type::Undefined;
  int click_count = 0;
  blink::WebMouseEvent::Button button = blink::WebMouseEvent::Button::NoButton;

  NSEventType type = [event type];
  switch (type) {
    case NSMouseExited:
      event_type = blink::WebInputEvent::MouseLeave;
      break;
    case NSLeftMouseDown:
      event_type = blink::WebInputEvent::MouseDown;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Left;
      break;
    case NSOtherMouseDown:
      event_type = blink::WebInputEvent::MouseDown;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Middle;
      break;
    case NSRightMouseDown:
      event_type = blink::WebInputEvent::MouseDown;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Right;
      break;
    case NSLeftMouseUp:
      event_type = blink::WebInputEvent::MouseUp;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Left;
      break;
    case NSOtherMouseUp:
      event_type = blink::WebInputEvent::MouseUp;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Middle;
      break;
    case NSRightMouseUp:
      event_type = blink::WebInputEvent::MouseUp;
      click_count = [event clickCount];
      button = blink::WebMouseEvent::Button::Right;
      break;
    case NSMouseMoved:
    case NSMouseEntered:
      event_type = blink::WebInputEvent::MouseMove;
      break;
    case NSLeftMouseDragged:
      event_type = blink::WebInputEvent::MouseMove;
      button = blink::WebMouseEvent::Button::Left;
      break;
    case NSOtherMouseDragged:
      event_type = blink::WebInputEvent::MouseMove;
      button = blink::WebMouseEvent::Button::Middle;
      break;
    case NSRightMouseDragged:
      event_type = blink::WebInputEvent::MouseMove;
      button = blink::WebMouseEvent::Button::Right;
      break;
    default:
      NOTIMPLEMENTED();
  }

  blink::WebMouseEvent result(event_type, ModifiersFromEvent(event),
                              [event timestamp]);
  result.clickCount = click_count;
  result.button = button;
  SetWebEventLocationFromEventInView(&result, event, view);

  // For NSMouseExited and NSMouseEntered events, they do not have a subtype.
  // We decide their pointer types by checking if we recevied a
  // NSTabletProximity event.
  if (type == NSMouseExited || type == NSMouseEntered) {
    result.pointerType = pointerType;
    return result;
  }

  // For other mouse events and touchpad events, the pointer type is mouse.
  // For all other tablet events, the pointer type will be just pen.
  NSEventSubtype subtype = [event subtype];
  if (subtype != NSTabletPointEventSubtype &&
      subtype != NSTabletProximityEventSubtype) {
    result.pointerType = blink::WebPointerProperties::PointerType::Mouse;
    return result;
  }

  // Set stylus properties for events with a subtype of
  // NSTabletPointEventSubtype.
  result.pointerType = blink::WebPointerProperties::PointerType::Pen;
  result.id = [event deviceID];
  if (subtype == NSTabletPointEventSubtype) {
    result.force = [event pressure];
    NSPoint tilt = [event tilt];
    result.tiltX = lround(tilt.x * 90);
    result.tiltY = lround(tilt.y * 90);
    result.tangentialPressure = [event tangentialPressure];
    // NSEvent spec doesn't specify the range of rotation, we make sure that
    // this value is in the range of [0,359].
    int twist = (int)[event rotation];
    twist = twist % 360;
    if (twist < 0)
      twist += 360;
    result.twist = twist;
  } else {
    event_type = [event isEnteringProximity]
                      ? blink::WebInputEvent::MouseMove
                      : blink::WebInputEvent::MouseLeave;
    result.setType(event_type);
  }
  return result;
}

// WebMouseWheelEvent ---------------------------------------------------------

blink::WebMouseWheelEvent WebMouseWheelEventBuilder::Build(
    NSEvent* event,
    NSView* view) {
  blink::WebMouseWheelEvent result(blink::WebInputEvent::MouseWheel,
                                   ModifiersFromEvent(event),
                                   [event timestamp]);
  result.button = blink::WebMouseEvent::Button::NoButton;

  SetWebEventLocationFromEventInView(&result, event, view);

  // Of Mice and Men
  // ---------------
  //
  // There are three types of scroll data available on a scroll wheel CGEvent.
  // Apple's documentation ([1]) is rather vague in their differences, and not
  // terribly helpful in deciding which to use. This is what's really going on.
  //
  // First, these events behave very differently depending on whether a standard
  // wheel mouse is used (one that scrolls in discrete units) or a
  // trackpad/Mighty Mouse is used (which both provide continuous scrolling).
  // You must check to see which was used for the event by testing the
  // kCGScrollWheelEventIsContinuous field.
  //
  // Second, these events refer to "axes". Axis 1 is the y-axis, and axis 2 is
  // the x-axis.
  //
  // Third, there is a concept of mouse acceleration. Scrolling the same amount
  // of physical distance will give you different results logically depending on
  // whether you scrolled a little at a time or in one continuous motion. Some
  // fields account for this while others do not.
  //
  // Fourth, for trackpads there is a concept of chunkiness. When scrolling
  // continuously, events can be delivered in chunks. That is to say, lots of
  // scroll events with delta 0 will be delivered, and every so often an event
  // with a non-zero delta will be delivered, containing the accumulated deltas
  // from all the intermediate moves. [2]
  //
  // For notchy wheel mice (kCGScrollWheelEventIsContinuous == 0)
  // ------------------------------------------------------------
  //
  // kCGScrollWheelEventDeltaAxis*
  //   This is the rawest of raw events. For each mouse notch you get a value of
  //   +1/-1. This does not take acceleration into account and thus is less
  //   useful for building UIs.
  //
  // kCGScrollWheelEventPointDeltaAxis*
  //   This is smarter. In general, for each mouse notch you get a value of
  //   +1/-1, but this _does_ take acceleration into account, so you will get
  //   larger values on longer scrolls. This field would be ideal for building
  //   UIs except for one nasty bug: when the shift key is pressed, this set of
  //   fields fails to move the value into the axis2 field (the other two types
  //   of data do). This wouldn't be so bad except for the fact that while the
  //   number of axes is used in the creation of a CGScrollWheelEvent, there is
  //   no way to get that information out of the event once created.
  //
  // kCGScrollWheelEventFixedPtDeltaAxis*
  //   This is a fixed value, and for each mouse notch you get a value of
  //   +0.1/-0.1 (but, like above, scaled appropriately for acceleration). This
  //   value takes acceleration into account, and in fact is identical to the
  //   results you get from -[NSEvent delta*]. (That is, if you linked on Tiger
  //   or greater; see [2] for details.)
  //
  // A note about continuous devices
  // -------------------------------
  //
  // There are two devices that provide continuous scrolling events (trackpads
  // and Mighty Mouses) and they behave rather differently. The Mighty Mouse
  // behaves a lot like a regular mouse. There is no chunking, and the
  // FixedPtDelta values are the PointDelta values multiplied by 0.1. With the
  // trackpad, though, there is chunking. While the FixedPtDelta values are
  // reasonable (they occur about every fifth event but have values five times
  // larger than usual) the Delta values are unreasonable. They don't appear to
  // accumulate properly.
  //
  // For continuous devices (kCGScrollWheelEventIsContinuous != 0)
  // -------------------------------------------------------------
  //
  // kCGScrollWheelEventDeltaAxis*
  //   This provides values with no acceleration. With a trackpad, these values
  //   are chunked but each non-zero value does not appear to be cumulative.
  //   This seems to be a bug.
  //
  // kCGScrollWheelEventPointDeltaAxis*
  //   This provides values with acceleration. With a trackpad, these values are
  //   not chunked and are highly accurate.
  //
  // kCGScrollWheelEventFixedPtDeltaAxis*
  //   This provides values with acceleration. With a trackpad, these values are
  //   chunked but unlike Delta events are properly cumulative.
  //
  // Summary
  // -------
  //
  // In general the best approach to take is: determine if the event is
  // continuous. If it is not, then use the FixedPtDelta events (or just stick
  // with Cocoa events). They provide both acceleration and proper horizontal
  // scrolling. If the event is continuous, then doing pixel scrolling with the
  // PointDelta is the way to go. In general, avoid the Delta events. They're
  // the oldest (dating back to 10.4, before CGEvents were public) but they lack
  // acceleration and precision, making them useful only in specific edge cases.
  //
  // References
  // ----------
  //
  // [1]
  // <http://developer.apple.com/documentation/Carbon/Reference/QuartzEventServicesRef/Reference/reference.html>
  // [2] <http://developer.apple.com/releasenotes/Cocoa/AppKitOlderNotes.html>
  //     Scroll to the section headed "NSScrollWheel events".
  //
  // P.S. The "smooth scrolling" option in the system preferences is utterly
  // unrelated to any of this.

  CGEventRef cg_event = [event CGEvent];
  DCHECK(cg_event);

  // Wheel ticks are supposed to be raw, unaccelerated values, one per physical
  // mouse wheel notch. The delta event is perfect for this (being a good
  // "specific edge case" as mentioned above). Trackpads, unfortunately, do
  // event chunking, and sending mousewheel events with 0 ticks causes some
  // websites to malfunction. Therefore, for all continuous input devices we use
  // the point delta data instead, since we cannot distinguish trackpad data
  // from data from any other continuous device.

  if (CGEventGetIntegerValueField(cg_event, kCGScrollWheelEventIsContinuous)) {
    result.deltaX = CGEventGetIntegerValueField(
        cg_event, kCGScrollWheelEventPointDeltaAxis2);
    result.deltaY = CGEventGetIntegerValueField(
        cg_event, kCGScrollWheelEventPointDeltaAxis1);
    result.wheelTicksX = result.deltaX / ui::kScrollbarPixelsPerCocoaTick;
    result.wheelTicksY = result.deltaY / ui::kScrollbarPixelsPerCocoaTick;
    result.hasPreciseScrollingDeltas = true;
  } else {
    result.deltaX = [event deltaX] * ui::kScrollbarPixelsPerCocoaTick;
    result.deltaY = [event deltaY] * ui::kScrollbarPixelsPerCocoaTick;
    result.wheelTicksY =
        CGEventGetIntegerValueField(cg_event, kCGScrollWheelEventDeltaAxis1);
    result.wheelTicksX =
        CGEventGetIntegerValueField(cg_event, kCGScrollWheelEventDeltaAxis2);
  }

  result.phase = PhaseForEvent(event);
  result.momentumPhase = MomentumPhaseForEvent(event);

  return result;
}

blink::WebGestureEvent WebGestureEventBuilder::Build(NSEvent* event,
                                                     NSView* view) {
  blink::WebGestureEvent result;

  // Use a temporary WebMouseEvent to get the location.
  blink::WebMouseEvent temp;

  SetWebEventLocationFromEventInView(&temp, event, view);
  result.x = temp.x;
  result.y = temp.y;
  result.globalX = temp.globalX;
  result.globalY = temp.globalY;

  result.setModifiers(ModifiersFromEvent(event));
  result.setTimeStampSeconds([event timestamp]);

  result.sourceDevice = blink::WebGestureDeviceTouchpad;
  switch ([event type]) {
    case NSEventTypeMagnify:
      result.setType(blink::WebInputEvent::GesturePinchUpdate);
      result.data.pinchUpdate.scale = [event magnification] + 1.0;
      break;
    case NSEventTypeSmartMagnify:
      // Map the Cocoa "double-tap with two fingers" zoom gesture to regular
      // GestureDoubleTap, because the effect is similar to single-finger
      // double-tap zoom on mobile platforms. Note that tapCount is set to 1
      // because the gesture type already encodes that information.
      result.setType(blink::WebInputEvent::GestureDoubleTap);
      result.data.tap.tapCount = 1;
      break;
    case NSEventTypeBeginGesture:
    case NSEventTypeEndGesture:
      // The specific type of a gesture is not defined when the gesture begin
      // and end NSEvents come in. Leave them undefined. The caller will need
      // to specify them when the gesture is differentiated.
      break;
    default:
      NOTIMPLEMENTED();
  }

  return result;
}

}  // namespace content
