// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/mus/mus_mouse_location_updater.h"

#include "ui/aura/env.h"
#include "ui/events/event.h"

namespace aura {
namespace {

bool IsMouseEventWithLocation(const ui::Event& event) {
  // All mouse events except exited, capture, and wheel which mus doesn't
  // include locations for.
  switch (event.type()) {
    case ui::ET_MOUSE_PRESSED:
    case ui::ET_MOUSE_DRAGGED:
    case ui::ET_MOUSE_RELEASED:
    case ui::ET_MOUSE_MOVED:
    case ui::ET_MOUSE_ENTERED:
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace

MusMouseLocationUpdater::MusMouseLocationUpdater() {
  base::MessageLoop::current()->AddNestingObserver(this);
}

MusMouseLocationUpdater::~MusMouseLocationUpdater() {
  base::MessageLoop::current()->RemoveNestingObserver(this);
}

void MusMouseLocationUpdater::OnEventProcessingStarted(const ui::Event& event) {
  if (!IsMouseEventWithLocation(event) ||
      Env::GetInstance()->always_use_last_mouse_location_) {
    return;
  }

  is_processing_trigger_event_ = true;
  Env::GetInstance()->set_last_mouse_location(
      event.AsMouseEvent()->root_location());
  Env::GetInstance()->get_last_mouse_location_from_mus_ = false;
}

void MusMouseLocationUpdater::OnEventProcessingFinished() {
  UseCursorScreenPoint();
}

void MusMouseLocationUpdater::UseCursorScreenPoint() {
  if (!is_processing_trigger_event_)
    return;

  is_processing_trigger_event_ = false;
  Env::GetInstance()->get_last_mouse_location_from_mus_ = true;
}

void MusMouseLocationUpdater::OnBeginNestedMessageLoop() {
  UseCursorScreenPoint();
}

}  // namespace aura
