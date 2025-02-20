// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/storage/DOMWindowStorageController.h"

#include "core/dom/Document.h"
#include "core/events/Event.h"
#include "core/page/Page.h"
#include "modules/storage/DOMWindowStorage.h"

namespace blink {

DOMWindowStorageController::DOMWindowStorageController(Document& document)
    : Supplement<Document>(document) {
  document.domWindow()->registerEventListenerObserver(this);
}

DEFINE_TRACE(DOMWindowStorageController) {
  Supplement<Document>::trace(visitor);
}

// static
const char* DOMWindowStorageController::supplementName() {
  return "DOMWindowStorageController";
}

// static
DOMWindowStorageController& DOMWindowStorageController::from(
    Document& document) {
  DOMWindowStorageController* controller =
      static_cast<DOMWindowStorageController*>(
          Supplement<Document>::from(document, supplementName()));
  if (!controller) {
    controller = new DOMWindowStorageController(document);
    Supplement<Document>::provideTo(document, supplementName(), controller);
  }
  return *controller;
}

void DOMWindowStorageController::didAddEventListener(
    LocalDOMWindow* window,
    const AtomicString& eventType) {
  if (eventType == EventTypeNames::storage) {
    // Creating these blink::Storage objects informs the system that we'd like
    // to receive notifications about storage events that might be triggered in
    // other processes. Rather than subscribe to these notifications explicitly,
    // we subscribe to them implicitly to simplify the work done by the system.
    DOMWindowStorage::from(*window).localStorage(IGNORE_EXCEPTION_FOR_TESTING);
    DOMWindowStorage::from(*window).sessionStorage(
        IGNORE_EXCEPTION_FOR_TESTING);
  }
}

}  // namespace blink
