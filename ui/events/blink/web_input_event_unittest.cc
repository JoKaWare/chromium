// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/events/blink/web_input_event.h"

#include <stddef.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/macros.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/blink/blink_event_util.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"

#if defined(USE_X11)
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include "ui/events/test/events_test_utils_x11.h"
#include "ui/gfx/x/x11_types.h"  // nogncheck
#endif

namespace ui {

namespace {

gfx::Point GetScreenLocationFromEvent(const LocatedEvent& event) {
  return event.root_location();
}

}  // namespace

// Checks that MakeWebKeyboardEvent makes a DOM3 spec compliant key event.
// crbug.com/127142
TEST(WebInputEventTest, TestMakeWebKeyboardEvent) {
  {
    // Press left Ctrl.
    KeyEvent event(ET_KEY_PRESSED, VKEY_CONTROL, DomCode::CONTROL_LEFT,
                   EF_CONTROL_DOWN);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| should be set.
    EXPECT_EQ(blink::WebInputEvent::ControlKey | blink::WebInputEvent::IsLeft,
              webkit_event.modifiers());
    EXPECT_EQ(static_cast<int>(DomCode::CONTROL_LEFT), webkit_event.domCode);
    EXPECT_EQ(static_cast<int>(DomKey::CONTROL), webkit_event.domKey);
  }
  {
    // Release left Ctrl.
    KeyEvent event(ET_KEY_RELEASED, VKEY_CONTROL, DomCode::CONTROL_LEFT,
                   EF_NONE);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| shouldn't be set.
    EXPECT_EQ(blink::WebInputEvent::IsLeft, webkit_event.modifiers());
    EXPECT_EQ(static_cast<int>(DomCode::CONTROL_LEFT), webkit_event.domCode);
    EXPECT_EQ(static_cast<int>(DomKey::CONTROL), webkit_event.domKey);
  }
  {
    // Press right Ctrl.
    KeyEvent event(ET_KEY_PRESSED, VKEY_CONTROL, DomCode::CONTROL_RIGHT,
                   EF_CONTROL_DOWN);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| should be set.
    EXPECT_EQ(blink::WebInputEvent::ControlKey | blink::WebInputEvent::IsRight,
              webkit_event.modifiers());
    EXPECT_EQ(static_cast<int>(DomCode::CONTROL_RIGHT), webkit_event.domCode);
  }
  {
    // Release right Ctrl.
    KeyEvent event(ET_KEY_RELEASED, VKEY_CONTROL, DomCode::CONTROL_RIGHT,
                   EF_NONE);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| shouldn't be set.
    EXPECT_EQ(blink::WebInputEvent::IsRight, webkit_event.modifiers());
    EXPECT_EQ(static_cast<int>(DomCode::CONTROL_RIGHT), webkit_event.domCode);
    EXPECT_EQ(static_cast<int>(DomKey::CONTROL), webkit_event.domKey);
  }
#if defined(USE_X11)
  const int kLocationModifiers =
      blink::WebInputEvent::IsLeft | blink::WebInputEvent::IsRight;
  ScopedXI2Event xev;
  {
    // Press Ctrl.
    xev.InitKeyEvent(ET_KEY_PRESSED, VKEY_CONTROL, 0);
    KeyEvent event(xev);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| should be set.
    EXPECT_EQ(blink::WebInputEvent::ControlKey,
              webkit_event.modifiers() & ~kLocationModifiers);
  }
  {
    // Release Ctrl.
    xev.InitKeyEvent(ET_KEY_RELEASED, VKEY_CONTROL, ControlMask);
    KeyEvent event(xev);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    // However, modifier bit for Control in |webkit_event| shouldn't be set.
    EXPECT_EQ(0, webkit_event.modifiers() & ~kLocationModifiers);
  }
#endif
}

TEST(WebInputEventTest, TestMakeWebKeyboardEventWindowsKeyCode) {
#if defined(USE_X11)
  ScopedXI2Event xev;
  {
    // Press left Ctrl.
    xev.InitKeyEvent(ET_KEY_PRESSED, VKEY_CONTROL, 0);
    XEvent* xevent = xev;
    xevent->xkey.keycode =
        KeycodeConverter::DomCodeToNativeKeycode(DomCode::CONTROL_LEFT);
    KeyEvent event(xev);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(VKEY_CONTROL, webkit_event.windowsKeyCode);
  }
  {
    // Press right Ctrl.
    xev.InitKeyEvent(ET_KEY_PRESSED, VKEY_CONTROL, 0);
    XEvent* xevent = xev;
    xevent->xkey.keycode =
        KeycodeConverter::DomCodeToNativeKeycode(DomCode::CONTROL_RIGHT);
    KeyEvent event(xev);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(VKEY_CONTROL, webkit_event.windowsKeyCode);
  }
#elif defined(OS_WIN)
// TODO(yusukes): Add tests for win_aura once keyboardEvent() in
// third_party/WebKit/Source/web/win/WebInputEventFactory.cpp is modified
// to return VKEY_[LR]XXX instead of VKEY_XXX.
// https://bugs.webkit.org/show_bug.cgi?id=86694
#endif
  {
    // Press left Ctrl.
    KeyEvent event(ET_KEY_PRESSED, VKEY_CONTROL, DomCode::CONTROL_LEFT,
                   EF_CONTROL_DOWN, DomKey::CONTROL, EventTimeForNow());
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(VKEY_CONTROL, webkit_event.windowsKeyCode);
  }
  {
    // Press right Ctrl.
    KeyEvent event(ET_KEY_PRESSED, VKEY_CONTROL, DomCode::CONTROL_RIGHT,
                   EF_CONTROL_DOWN, DomKey::CONTROL, EventTimeForNow());
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(VKEY_CONTROL, webkit_event.windowsKeyCode);
  }
}

// Checks that MakeWebKeyboardEvent fills a correct keypad modifier.
TEST(WebInputEventTest, TestMakeWebKeyboardEventKeyPadKeyCode) {
#if defined(USE_X11)
#define XK(x) XK_##x
#else
#define XK(x) 0
#endif
  struct TestCase {
    DomCode dom_code;         // The physical key (location).
    KeyboardCode ui_keycode;  // The virtual key code.
    uint32_t x_keysym;        // The X11 keysym.
    bool expected_result;     // true if the event has "isKeyPad" modifier.
  } kTesCases[] = {
      {DomCode::DIGIT0, VKEY_0, XK(0), false},
      {DomCode::DIGIT1, VKEY_1, XK(1), false},
      {DomCode::DIGIT2, VKEY_2, XK(2), false},
      {DomCode::DIGIT3, VKEY_3, XK(3), false},
      {DomCode::DIGIT4, VKEY_4, XK(4), false},
      {DomCode::DIGIT5, VKEY_5, XK(5), false},
      {DomCode::DIGIT6, VKEY_6, XK(6), false},
      {DomCode::DIGIT7, VKEY_7, XK(7), false},
      {DomCode::DIGIT8, VKEY_8, XK(8), false},
      {DomCode::DIGIT9, VKEY_9, XK(9), false},

      {DomCode::NUMPAD0, VKEY_NUMPAD0, XK(KP_0), true},
      {DomCode::NUMPAD1, VKEY_NUMPAD1, XK(KP_1), true},
      {DomCode::NUMPAD2, VKEY_NUMPAD2, XK(KP_2), true},
      {DomCode::NUMPAD3, VKEY_NUMPAD3, XK(KP_3), true},
      {DomCode::NUMPAD4, VKEY_NUMPAD4, XK(KP_4), true},
      {DomCode::NUMPAD5, VKEY_NUMPAD5, XK(KP_5), true},
      {DomCode::NUMPAD6, VKEY_NUMPAD6, XK(KP_6), true},
      {DomCode::NUMPAD7, VKEY_NUMPAD7, XK(KP_7), true},
      {DomCode::NUMPAD8, VKEY_NUMPAD8, XK(KP_8), true},
      {DomCode::NUMPAD9, VKEY_NUMPAD9, XK(KP_9), true},

      {DomCode::NUMPAD_MULTIPLY, VKEY_MULTIPLY, XK(KP_Multiply), true},
      {DomCode::NUMPAD_SUBTRACT, VKEY_SUBTRACT, XK(KP_Subtract), true},
      {DomCode::NUMPAD_ADD, VKEY_ADD, XK(KP_Add), true},
      {DomCode::NUMPAD_DIVIDE, VKEY_DIVIDE, XK(KP_Divide), true},
      {DomCode::NUMPAD_DECIMAL, VKEY_DECIMAL, XK(KP_Decimal), true},
      {DomCode::NUMPAD_DECIMAL, VKEY_DELETE, XK(KP_Delete), true},
      {DomCode::NUMPAD0, VKEY_INSERT, XK(KP_Insert), true},
      {DomCode::NUMPAD1, VKEY_END, XK(KP_End), true},
      {DomCode::NUMPAD2, VKEY_DOWN, XK(KP_Down), true},
      {DomCode::NUMPAD3, VKEY_NEXT, XK(KP_Page_Down), true},
      {DomCode::NUMPAD4, VKEY_LEFT, XK(KP_Left), true},
      {DomCode::NUMPAD5, VKEY_CLEAR, XK(KP_Begin), true},
      {DomCode::NUMPAD6, VKEY_RIGHT, XK(KP_Right), true},
      {DomCode::NUMPAD7, VKEY_HOME, XK(KP_Home), true},
      {DomCode::NUMPAD8, VKEY_UP, XK(KP_Up), true},
      {DomCode::NUMPAD9, VKEY_PRIOR, XK(KP_Page_Up), true},
  };
  for (const auto& test_case : kTesCases) {
    KeyEvent event(ET_KEY_PRESSED, test_case.ui_keycode, test_case.dom_code,
                   EF_NONE);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(test_case.expected_result,
              (webkit_event.modifiers() & blink::WebInputEvent::IsKeyPad) != 0)
        << "Failed in "
        << "{dom_code:"
        << KeycodeConverter::DomCodeToCodeString(test_case.dom_code)
        << ", ui_keycode:" << test_case.ui_keycode
        << "}, expect: " << test_case.expected_result;
  }
#if defined(USE_X11)
  ScopedXI2Event xev;
  for (size_t i = 0; i < arraysize(kTesCases); ++i) {
    const TestCase& test_case = kTesCases[i];

    // TODO: re-enable the two cases excluded here once all trybots
    // are sufficiently up to date to round-trip the associated keys.
    if ((test_case.x_keysym == XK_KP_Divide) ||
        (test_case.x_keysym == XK_KP_Decimal))
      continue;

    xev.InitKeyEvent(ET_KEY_PRESSED, test_case.ui_keycode, EF_NONE);
    XEvent* xevent = xev;
    xevent->xkey.keycode =
        XKeysymToKeycode(gfx::GetXDisplay(), test_case.x_keysym);
    if (!xevent->xkey.keycode)
      continue;
    KeyEvent event(xev);
    blink::WebKeyboardEvent webkit_event = MakeWebKeyboardEvent(event);
    EXPECT_EQ(test_case.expected_result,
              (webkit_event.modifiers() & blink::WebInputEvent::IsKeyPad) != 0)
        << "Failed in " << i << "th test case: "
        << "{dom_code:"
        << KeycodeConverter::DomCodeToCodeString(test_case.dom_code)
        << ", ui_keycode:" << test_case.ui_keycode
        << ", x_keysym:" << test_case.x_keysym
        << "}, expect: " << test_case.expected_result;
  }
#endif
}

TEST(WebInputEventTest, TestMakeWebMouseEvent) {
  {
    // Left pressed.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_PRESSED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_LEFT_MOUSE_BUTTON,
                        EF_LEFT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Left, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseDown, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Left released.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_RELEASED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, 0,
                        EF_LEFT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Left, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseUp, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Middle pressed.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_PRESSED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_MIDDLE_MOUSE_BUTTON,
                        EF_MIDDLE_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Middle, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseDown, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Middle released.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_RELEASED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, 0,
                        EF_MIDDLE_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Middle, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseUp, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Right pressed.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_PRESSED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_RIGHT_MOUSE_BUTTON,
                        EF_RIGHT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Right, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseDown, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Right released.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_RELEASED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, 0,
                        EF_RIGHT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Right, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseUp, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Moved
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_MOVED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, 0, 0);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::NoButton, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseMove, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Moved with left down
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_MOVED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_LEFT_MOUSE_BUTTON,
                        0);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Left, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseMove, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Left with shift pressed.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(
        ET_MOUSE_PRESSED, gfx::Point(123, 321), gfx::Point(123, 321), timestamp,
        EF_LEFT_MOUSE_BUTTON | EF_SHIFT_DOWN, EF_LEFT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::Left, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseDown, webkit_event.type());
    EXPECT_EQ(ui_event.GetClickCount(), webkit_event.clickCount);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Default values for PointerDetails.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_PRESSED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_LEFT_MOUSE_BUTTON,
                        EF_LEFT_MOUSE_BUTTON);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));

    EXPECT_EQ(blink::WebPointerProperties::PointerType::Mouse,
              webkit_event.pointerType);
    EXPECT_EQ(0, webkit_event.tiltX);
    EXPECT_EQ(0, webkit_event.tiltY);
    EXPECT_TRUE(std::isnan(webkit_event.force));
    EXPECT_EQ(0.0f, webkit_event.tangentialPressure);
    EXPECT_EQ(0, webkit_event.twist);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
  {
    // Stylus values for PointerDetails.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseEvent ui_event(ET_MOUSE_PRESSED, gfx::Point(123, 321),
                        gfx::Point(123, 321), timestamp, EF_LEFT_MOUSE_BUTTON,
                        EF_LEFT_MOUSE_BUTTON);
    PointerDetails pointer_details(EventPointerType::POINTER_TYPE_PEN,
                                   /* radius_x */ 0.0f,
                                   /* radius_y */ 0.0f,
                                   /* force */ 0.8f,
                                   /* tilt_x */ 89.5f,
                                   /* tilt_y */ -89.5f,
                                   /* tangential_pressure */ 0.6f,
                                   /* twist */ 269);
    ui_event.set_pointer_details(pointer_details);
    blink::WebMouseEvent webkit_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));

    EXPECT_EQ(blink::WebPointerProperties::PointerType::Pen,
              webkit_event.pointerType);
    EXPECT_EQ(90, webkit_event.tiltX);
    EXPECT_EQ(-90, webkit_event.tiltY);
    EXPECT_FLOAT_EQ(0.8f, webkit_event.force);
    EXPECT_FLOAT_EQ(0.6f, webkit_event.tangentialPressure);
    EXPECT_EQ(269, webkit_event.twist);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
}

TEST(WebInputEventTest, TestMakeWebMouseWheelEvent) {
  {
    // Mouse wheel.
    base::TimeTicks timestamp = EventTimeForNow();
    MouseWheelEvent ui_event(gfx::Vector2d(MouseWheelEvent::kWheelDelta * 2,
                                           -MouseWheelEvent::kWheelDelta * 2),
                             gfx::Point(123, 321), gfx::Point(123, 321),
                             timestamp, 0, 0);
    blink::WebMouseWheelEvent webkit_event = MakeWebMouseWheelEvent(
        ui_event, base::Bind(&GetScreenLocationFromEvent));
    EXPECT_EQ(EventFlagsToWebEventModifiers(ui_event.flags()),
              webkit_event.modifiers());
    EXPECT_FLOAT_EQ(EventTimeStampToSeconds(timestamp),
                    webkit_event.timeStampSeconds());
    EXPECT_EQ(blink::WebMouseEvent::Button::NoButton, webkit_event.button);
    EXPECT_EQ(blink::WebInputEvent::MouseWheel, webkit_event.type());
    EXPECT_FLOAT_EQ(ui_event.x_offset() / MouseWheelEvent::kWheelDelta,
                    webkit_event.wheelTicksX);
    EXPECT_FLOAT_EQ(ui_event.y_offset() / MouseWheelEvent::kWheelDelta,
                    webkit_event.wheelTicksY);
    EXPECT_EQ(blink::WebPointerProperties::PointerType::Mouse,
              webkit_event.pointerType);
    EXPECT_EQ(0, webkit_event.tiltX);
    EXPECT_EQ(0, webkit_event.tiltY);
    EXPECT_TRUE(std::isnan(webkit_event.force));
    EXPECT_EQ(0.0f, webkit_event.tangentialPressure);
    EXPECT_EQ(0, webkit_event.twist);
    EXPECT_EQ(123, webkit_event.x);
    EXPECT_EQ(123, webkit_event.windowX);
    EXPECT_EQ(321, webkit_event.y);
    EXPECT_EQ(321, webkit_event.windowY);
  }
}

TEST(WebInputEventTest, KeyEvent) {
  struct {
    ui::KeyEvent event;
    blink::WebInputEvent::Type web_type;
    int web_modifiers;
  } tests[] = {
      {ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_A, ui::EF_NONE),
       blink::WebInputEvent::RawKeyDown, 0x0},
      {ui::KeyEvent(L'B', ui::VKEY_B, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN),
       blink::WebInputEvent::Char,
       blink::WebInputEvent::ShiftKey | blink::WebInputEvent::ControlKey},
      {ui::KeyEvent(ui::ET_KEY_RELEASED, ui::VKEY_C, ui::EF_ALT_DOWN),
       blink::WebInputEvent::KeyUp, blink::WebInputEvent::AltKey}};

  for (size_t i = 0; i < arraysize(tests); i++) {
    blink::WebKeyboardEvent web_event = MakeWebKeyboardEvent(tests[i].event);
    ASSERT_TRUE(blink::WebInputEvent::isKeyboardEventType(web_event.type()));
    ASSERT_EQ(tests[i].web_type, web_event.type());
    ASSERT_EQ(tests[i].web_modifiers, web_event.modifiers());
    ASSERT_EQ(static_cast<int>(tests[i].event.GetLocatedWindowsKeyboardCode()),
              web_event.windowsKeyCode);
  }
}

TEST(WebInputEventTest, WheelEvent) {
  const int kDeltaX = 14;
  const int kDeltaY = -3;
  ui::MouseWheelEvent ui_event(
      ui::MouseEvent(ui::ET_UNKNOWN, gfx::Point(), gfx::Point(),
                     base::TimeTicks(), 0, 0),
      kDeltaX, kDeltaY);
  blink::WebMouseWheelEvent web_event =
      MakeWebMouseWheelEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
  ASSERT_EQ(blink::WebInputEvent::MouseWheel, web_event.type());
  ASSERT_EQ(0, web_event.modifiers());
  ASSERT_EQ(kDeltaX, web_event.deltaX);
  ASSERT_EQ(kDeltaY, web_event.deltaY);
}

TEST(WebInputEventTest, MousePointerEvent) {
  struct {
    ui::EventType ui_type;
    blink::WebInputEvent::Type web_type;
    int ui_modifiers;
    int web_modifiers;
    gfx::Point location;
    gfx::Point screen_location;
  } tests[] = {
      {ui::ET_MOUSE_PRESSED, blink::WebInputEvent::MouseDown, 0x0, 0x0,
       gfx::Point(3, 5), gfx::Point(113, 125)},
      {ui::ET_MOUSE_RELEASED, blink::WebInputEvent::MouseUp,
       ui::EF_LEFT_MOUSE_BUTTON, blink::WebInputEvent::LeftButtonDown,
       gfx::Point(100, 1), gfx::Point(50, 1)},
      {ui::ET_MOUSE_MOVED, blink::WebInputEvent::MouseMove,
       ui::EF_MIDDLE_MOUSE_BUTTON | ui::EF_RIGHT_MOUSE_BUTTON,
       blink::WebInputEvent::MiddleButtonDown |
           blink::WebInputEvent::RightButtonDown,
       gfx::Point(13, 3), gfx::Point(53, 3)},
  };

  for (size_t i = 0; i < arraysize(tests); i++) {
    ui::MouseEvent ui_event(tests[i].ui_type, tests[i].location,
                            tests[i].screen_location, base::TimeTicks(),
                            tests[i].ui_modifiers, 0);
    blink::WebMouseEvent web_event =
        MakeWebMouseEvent(ui_event, base::Bind(&GetScreenLocationFromEvent));
    ASSERT_TRUE(blink::WebInputEvent::isMouseEventType(web_event.type()));
    ASSERT_EQ(tests[i].web_type, web_event.type());
    ASSERT_EQ(tests[i].web_modifiers, web_event.modifiers());
    ASSERT_EQ(tests[i].location.x(), web_event.x);
    ASSERT_EQ(tests[i].location.y(), web_event.y);
    ASSERT_EQ(tests[i].screen_location.x(), web_event.globalX);
    ASSERT_EQ(tests[i].screen_location.y(), web_event.globalY);
  }
}

}  // namespace ui
