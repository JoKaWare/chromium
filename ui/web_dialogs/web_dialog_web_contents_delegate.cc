// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/web_dialogs/web_dialog_web_contents_delegate.h"

#include "base/logging.h"
#include "content/public/browser/web_contents.h"
#include "third_party/WebKit/public/platform/WebGestureEvent.h"

using content::BrowserContext;
using content::OpenURLParams;
using content::WebContents;

namespace ui {

// Incognito profiles are not long-lived, so we always want to store a
// non-incognito profile.
//
// TODO(akalin): Should we make it so that we have a default incognito
// profile that's long-lived?  Of course, we'd still have to clear it out
// when all incognito browsers close.
WebDialogWebContentsDelegate::WebDialogWebContentsDelegate(
    content::BrowserContext* browser_context,
    WebContentsHandler* handler)
    : browser_context_(browser_context),
      handler_(handler) {
  CHECK(handler_.get());
}

WebDialogWebContentsDelegate::~WebDialogWebContentsDelegate() {
}

void WebDialogWebContentsDelegate::Detach() {
  browser_context_ = NULL;
}

WebContents* WebDialogWebContentsDelegate::OpenURLFromTab(
    WebContents* source, const OpenURLParams& params) {
  return handler_->OpenURLFromTab(browser_context_, source, params);
}

void WebDialogWebContentsDelegate::AddNewContents(
    WebContents* source, WebContents* new_contents,
    WindowOpenDisposition disposition, const gfx::Rect& initial_rect,
    bool user_gesture,
    bool* was_blocked) {
  handler_->AddNewContents(browser_context_, source, new_contents, disposition,
                           initial_rect, user_gesture);
}

bool WebDialogWebContentsDelegate::IsPopupOrPanel(
    const WebContents* source) const {
  // This needs to return true so that we are allowed to be resized by our
  // contents.
  return true;
}

bool WebDialogWebContentsDelegate::PreHandleGestureEvent(
    WebContents* source,
    const blink::WebGestureEvent& event) {
  // Disable pinch zooming.
  return event.type() == blink::WebGestureEvent::GesturePinchBegin ||
         event.type() == blink::WebGestureEvent::GesturePinchUpdate ||
         event.type() == blink::WebGestureEvent::GesturePinchEnd;
}

}  // namespace ui
