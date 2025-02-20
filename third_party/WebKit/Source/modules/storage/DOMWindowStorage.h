// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DOMWindowStorage_h
#define DOMWindowStorage_h

#include "core/frame/LocalDOMWindow.h"
#include "platform/Supplementable.h"
#include "platform/heap/Handle.h"

namespace blink {

class DOMWindow;
class ExceptionState;
class LocalDOMWindow;
class Storage;

class DOMWindowStorage final : public GarbageCollected<DOMWindowStorage>,
                               public Supplement<LocalDOMWindow> {
  USING_GARBAGE_COLLECTED_MIXIN(DOMWindowStorage);

 public:
  static DOMWindowStorage& from(LocalDOMWindow&);
  static Storage* sessionStorage(DOMWindow&, ExceptionState&);
  static Storage* localStorage(DOMWindow&, ExceptionState&);

  Storage* sessionStorage(ExceptionState&) const;
  Storage* localStorage(ExceptionState&) const;
  Storage* optionalSessionStorage() const { return m_sessionStorage.get(); }
  Storage* optionalLocalStorage() const { return m_localStorage.get(); }

  DECLARE_TRACE();

 private:
  explicit DOMWindowStorage(LocalDOMWindow&);
  static const char* supplementName();

  mutable Member<Storage> m_sessionStorage;
  mutable Member<Storage> m_localStorage;
};

}  // namespace blink

#endif  // DOMWindowStorage_h
