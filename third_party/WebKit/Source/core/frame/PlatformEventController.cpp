// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/PlatformEventController.h"

#include "core/page/Page.h"

namespace blink {

PlatformEventController::PlatformEventController(LocalFrame* frame)
    : PageVisibilityObserver(frame ? frame->page() : nullptr),
      m_hasEventListener(false),
      m_isActive(false),
      m_timer(TaskRunnerHelper::get(TaskType::UnspecedTimer, frame),
              this,
              &PlatformEventController::oneShotCallback) {}

PlatformEventController::~PlatformEventController() {}

void PlatformEventController::oneShotCallback(TimerBase* timer) {
  DCHECK_EQ(timer, &m_timer);
  ASSERT(hasLastData());
  ASSERT(!m_timer.isActive());

  didUpdateData();
}

void PlatformEventController::startUpdating() {
  if (m_isActive)
    return;

  if (hasLastData() && !m_timer.isActive()) {
    // Make sure to fire the data as soon as possible.
    m_timer.startOneShot(0, BLINK_FROM_HERE);
  }

  registerWithDispatcher();
  m_isActive = true;
}

void PlatformEventController::stopUpdating() {
  if (!m_isActive)
    return;

  m_timer.stop();
  unregisterWithDispatcher();
  m_isActive = false;
}

void PlatformEventController::pageVisibilityChanged() {
  if (!m_hasEventListener)
    return;

  if (page()->isPageVisible())
    startUpdating();
  else
    stopUpdating();
}

}  // namespace blink
