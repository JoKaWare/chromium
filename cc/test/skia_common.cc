// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/skia_common.h"

#include <stddef.h>

#include "cc/paint/paint_canvas.h"
#include "cc/playback/display_item_list.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"

namespace cc {

namespace {

class TestImageGenerator : public SkImageGenerator {
 public:
  explicit TestImageGenerator(const SkImageInfo& info)
      : SkImageGenerator(info),
        image_backing_memory_(info.getSafeSize(info.minRowBytes()), 0),
        image_pixmap_(info, image_backing_memory_.data(), info.minRowBytes()) {}

 protected:
  bool onGetPixels(const SkImageInfo& info,
                   void* pixels,
                   size_t rowBytes,
                   SkPMColor ctable[],
                   int* ctableCount) override {
    return image_pixmap_.readPixels(info, pixels, rowBytes, 0, 0);
  }

 private:
  std::vector<uint8_t> image_backing_memory_;
  SkPixmap image_pixmap_;
};

}  // anonymous namespace

void DrawDisplayList(unsigned char* buffer,
                     const gfx::Rect& layer_rect,
                     scoped_refptr<const DisplayItemList> list) {
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  SkBitmap bitmap;
  bitmap.installPixels(info, buffer, info.minRowBytes());
  PaintCanvas canvas(bitmap);
  canvas.clipRect(gfx::RectToSkRect(layer_rect));
  list->Raster(&canvas, NULL, layer_rect, 1.0f);
}

bool AreDisplayListDrawingResultsSame(const gfx::Rect& layer_rect,
                                      const DisplayItemList* list_a,
                                      const DisplayItemList* list_b) {
  const size_t pixel_size = 4 * layer_rect.size().GetArea();

  std::unique_ptr<unsigned char[]> pixels_a(new unsigned char[pixel_size]);
  std::unique_ptr<unsigned char[]> pixels_b(new unsigned char[pixel_size]);
  memset(pixels_a.get(), 0, pixel_size);
  memset(pixels_b.get(), 0, pixel_size);
  DrawDisplayList(pixels_a.get(), layer_rect, list_a);
  DrawDisplayList(pixels_b.get(), layer_rect, list_b);

  return !memcmp(pixels_a.get(), pixels_b.get(), pixel_size);
}

sk_sp<SkImage> CreateDiscardableImage(const gfx::Size& size) {
  return SkImage::MakeFromGenerator(new TestImageGenerator(
      SkImageInfo::MakeN32Premul(size.width(), size.height())));
}

}  // namespace cc
