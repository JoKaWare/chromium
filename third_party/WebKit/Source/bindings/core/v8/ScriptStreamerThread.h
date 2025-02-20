// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ScriptStreamerThread_h
#define ScriptStreamerThread_h

#include "core/CoreExport.h"
#include "public/platform/WebThread.h"
#include "wtf/Functional.h"
#include "wtf/ThreadingPrimitives.h"
#include <memory>
#include <v8.h>

namespace blink {

class ScriptStreamer;

// A singleton thread for running background tasks for script streaming.
class CORE_EXPORT ScriptStreamerThread {
  USING_FAST_MALLOC(ScriptStreamerThread);
  WTF_MAKE_NONCOPYABLE(ScriptStreamerThread);

 public:
  static void init();
  static ScriptStreamerThread* shared();

  void postTask(std::unique_ptr<CrossThreadClosure>);

  bool isRunningTask() const {
    MutexLocker locker(m_mutex);
    return m_runningTask;
  }

  void taskDone();

  static void runScriptStreamingTask(
      std::unique_ptr<v8::ScriptCompiler::ScriptStreamingTask>,
      ScriptStreamer*);

 private:
  ScriptStreamerThread() : m_runningTask(false) {}

  bool isRunning() const { return !!m_thread; }

  WebThread& platformThread();

  // At the moment, we only use one thread, so we can only stream one script
  // at a time. FIXME: Use a thread pool and stream multiple scripts.
  std::unique_ptr<WebThread> m_thread;
  bool m_runningTask;
  mutable Mutex m_mutex;  // Guards m_runningTask.
};

}  // namespace blink

#endif  // ScriptStreamerThread_h
