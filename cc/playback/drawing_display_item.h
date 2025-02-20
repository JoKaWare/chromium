// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PLAYBACK_DRAWING_DISPLAY_ITEM_H_
#define CC_PLAYBACK_DRAWING_DISPLAY_ITEM_H_

#include <stddef.h>

#include <memory>
#include <vector>

#include "cc/base/cc_export.h"
#include "cc/paint/paint_record.h"
#include "cc/playback/display_item.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/gfx/geometry/point_f.h"

class SkCanvas;

namespace cc {

class CC_EXPORT DrawingDisplayItem : public DisplayItem {
 public:
  DrawingDisplayItem();
  explicit DrawingDisplayItem(sk_sp<const PaintRecord> record);
  explicit DrawingDisplayItem(const DrawingDisplayItem& item);
  ~DrawingDisplayItem() override;

  void Raster(SkCanvas* canvas,
              SkPicture::AbortCallback* callback) const override;
  void AsValueInto(const gfx::Rect& visual_rect,
                   base::trace_event::TracedValue* array) const override;

  size_t ExternalMemoryUsage() const;
  int ApproximateOpCount() const;

  void CloneTo(DrawingDisplayItem* item) const;

 private:
  void SetNew(sk_sp<const PaintRecord> record);

  sk_sp<const PaintRecord> picture_;
};

}  // namespace cc

#endif  // CC_PLAYBACK_DRAWING_DISPLAY_ITEM_H_
