// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_BLINK_WEB_DISPLAY_ITEM_LIST_IMPL_H_
#define CC_BLINK_WEB_DISPLAY_ITEM_LIST_IMPL_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/blink/cc_blink_export.h"
#include "cc/paint/paint_record.h"
#include "cc/playback/display_item_list.h"
#include "third_party/WebKit/public/platform/WebDisplayItemList.h"
#include "third_party/WebKit/public/platform/WebVector.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "ui/gfx/geometry/point_f.h"

class SkColorFilter;
class SkMatrix44;
class SkPath;
class SkRRect;

namespace blink {
struct WebFloatRect;
struct WebFloatPoint;
struct WebRect;
}

namespace cc {
class FilterOperations;
}

namespace cc_blink {

class WebDisplayItemListImpl : public blink::WebDisplayItemList {
 public:
  CC_BLINK_EXPORT WebDisplayItemListImpl();
  CC_BLINK_EXPORT explicit WebDisplayItemListImpl(
      cc::DisplayItemList* display_list);
  ~WebDisplayItemListImpl() override;

  // blink::WebDisplayItemList implementation.
  void appendDrawingItem(const blink::WebRect& visual_rect,
                         sk_sp<const cc::PaintRecord> record) override;
  void appendClipItem(
      const blink::WebRect& clip_rect,
      const blink::WebVector<SkRRect>& rounded_clip_rects) override;
  void appendEndClipItem() override;
  void appendClipPathItem(const SkPath& clip_path,
                          bool antialias) override;
  void appendEndClipPathItem() override;
  void appendFloatClipItem(const blink::WebFloatRect& clip_rect) override;
  void appendEndFloatClipItem() override;
  void appendTransformItem(const SkMatrix44& matrix) override;
  void appendEndTransformItem() override;
  void appendCompositingItem(float opacity,
                             SkBlendMode,
                             SkRect* bounds,
                             SkColorFilter*) override;
  void appendEndCompositingItem() override;
  void appendFilterItem(const cc::FilterOperations& filters,
                        const blink::WebFloatRect& filter_bounds,
                        const blink::WebFloatPoint& origin) override;
  void appendEndFilterItem() override;
  void appendScrollItem(const blink::WebSize& scrollOffset,
                        ScrollContainerId) override;
  void appendEndScrollItem() override;

  void setIsSuitableForGpuRasterization(bool isSuitable) override;

  void setImpliedColorSpace(const gfx::ColorSpace& color_space) override;

 private:
  scoped_refptr<cc::DisplayItemList> display_item_list_;

  DISALLOW_COPY_AND_ASSIGN(WebDisplayItemListImpl);
};

}  // namespace cc_blink

#endif  // CC_BLINK_WEB_DISPLAY_ITEM_LIST_IMPL_H_
