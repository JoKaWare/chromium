// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/media_resource.h"

namespace media {

MediaResource::MediaResource() {}

MediaResource::~MediaResource() {}

MediaUrlParams MediaResource::GetMediaUrlParams() const {
  NOTREACHED();
  return MediaUrlParams{GURL(), GURL()};
}

MediaResource::Type MediaResource::GetType() const {
  return STREAM;
}

}  // namespace media
