// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/bluetooth/NavigatorBluetooth.h"

#include "core/frame/Navigator.h"
#include "modules/bluetooth/Bluetooth.h"

namespace blink {

NavigatorBluetooth& NavigatorBluetooth::from(Navigator& navigator) {
  NavigatorBluetooth* supplement = static_cast<NavigatorBluetooth*>(
      Supplement<Navigator>::from(navigator, supplementName()));
  if (!supplement) {
    supplement = new NavigatorBluetooth(navigator);
    provideTo(navigator, supplementName(), supplement);
  }
  return *supplement;
}

Bluetooth* NavigatorBluetooth::bluetooth(Navigator& navigator) {
  return NavigatorBluetooth::from(navigator).bluetooth();
}

Bluetooth* NavigatorBluetooth::bluetooth() {
  if (!m_bluetooth)
    m_bluetooth = Bluetooth::create();
  return m_bluetooth.get();
}

DEFINE_TRACE(NavigatorBluetooth) {
  visitor->trace(m_bluetooth);
  Supplement<Navigator>::trace(visitor);
}

NavigatorBluetooth::NavigatorBluetooth(Navigator& navigator)
    : Supplement<Navigator>(navigator) {}

const char* NavigatorBluetooth::supplementName() {
  return "NavigatorBluetooth";
}

}  // namespace blink
