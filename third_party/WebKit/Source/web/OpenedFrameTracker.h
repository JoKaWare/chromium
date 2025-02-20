// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OpenedFrameTracker_h
#define OpenedFrameTracker_h

#include "wtf/HashSet.h"
#include "wtf/Noncopyable.h"

namespace blink {

class Visitor;
class WebFrame;

// Small helper class to track the set of frames that a WebFrame has opened.
// Due to layering restrictions, we need to hide the implementation, since
// public/web/ cannot depend on wtf/.
class OpenedFrameTracker {
  WTF_MAKE_NONCOPYABLE(OpenedFrameTracker);

 public:
  OpenedFrameTracker();
  ~OpenedFrameTracker();

  bool isEmpty() const;
  void add(WebFrame*);
  void remove(WebFrame*);

  // Helper used when swapping a frame into the frame tree: this updates the
  // opener for opened frames to point to the new frame being swapped in.
  void transferTo(WebFrame*);

  void traceFrames(Visitor*);

 private:
  WTF::HashSet<WebFrame*> m_openedFrames;
};

}  // namespace blink

#endif  // WebFramePrivate_h
