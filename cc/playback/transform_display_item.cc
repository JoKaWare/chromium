// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/playback/transform_display_item.h"

#include <stddef.h>

#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event_argument.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace cc {

TransformDisplayItem::TransformDisplayItem(const gfx::Transform& transform)
    : DisplayItem(TRANSFORM), transform_(gfx::Transform::kSkipInitialization) {
  SetNew(transform);
}

TransformDisplayItem::~TransformDisplayItem() {
}

void TransformDisplayItem::SetNew(const gfx::Transform& transform) {
  transform_ = transform;
}

void TransformDisplayItem::Raster(SkCanvas* canvas,
                                  SkPicture::AbortCallback* callback) const {
  canvas->save();
  if (!transform_.IsIdentity())
    canvas->concat(transform_.matrix());
}

void TransformDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(base::StringPrintf(
      "TransformDisplayItem transform: [%s] visualRect: [%s]",
      transform_.ToString().c_str(), visual_rect.ToString().c_str()));
}

EndTransformDisplayItem::EndTransformDisplayItem()
    : DisplayItem(END_TRANSFORM) {}

EndTransformDisplayItem::~EndTransformDisplayItem() {
}

void EndTransformDisplayItem::Raster(
    SkCanvas* canvas,
    SkPicture::AbortCallback* callback) const {
  canvas->restore();
}

void EndTransformDisplayItem::AsValueInto(
    const gfx::Rect& visual_rect,
    base::trace_event::TracedValue* array) const {
  array->AppendString(
      base::StringPrintf("EndTransformDisplayItem visualRect: [%s]",
                         visual_rect.ToString().c_str()));
}

}  // namespace cc
