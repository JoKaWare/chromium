/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DOMWrapperWorld_h
#define DOMWrapperWorld_h

#include "bindings/core/v8/ScriptState.h"
#include "core/CoreExport.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "wtf/PassRefPtr.h"
#include "wtf/RefCounted.h"
#include "wtf/RefPtr.h"
#include <memory>
#include <v8.h>

namespace blink {

class DOMDataStore;
class DOMObjectHolderBase;

enum WorldIdConstants {
  MainWorldId = 0,
  // Embedder isolated worlds can use IDs in [1, 1<<29).
  EmbedderWorldIdLimit = (1 << 29),
  DocumentXMLTreeViewerWorldId,
  IsolatedWorldIdLimit,
  WorkerWorldId,
  TestingWorldId,
};

// This class represent a collection of DOM wrappers for a specific world.
class CORE_EXPORT DOMWrapperWorld : public RefCounted<DOMWrapperWorld> {
 public:
  static PassRefPtr<DOMWrapperWorld> create(v8::Isolate*, int worldId = -1);

  static PassRefPtr<DOMWrapperWorld> ensureIsolatedWorld(v8::Isolate*,
                                                         int worldId);
  ~DOMWrapperWorld();
  void dispose();

  static bool isolatedWorldsExist() { return isolatedWorldCount; }
  static void allWorldsInMainThread(Vector<RefPtr<DOMWrapperWorld>>& worlds);
  static void markWrappersInAllWorlds(ScriptWrappable*,
                                      const ScriptWrappableVisitor*);

  static DOMWrapperWorld& world(v8::Local<v8::Context> context) {
    return ScriptState::from(context)->world();
  }

  static DOMWrapperWorld& current(v8::Isolate* isolate) {
    return world(isolate->GetCurrentContext());
  }

  static DOMWrapperWorld*& workerWorld();
  static DOMWrapperWorld& mainWorld();
  static PassRefPtr<DOMWrapperWorld> fromWorldId(v8::Isolate*, int worldId);

  static void setIsolatedWorldHumanReadableName(int worldID, const String&);
  String isolatedWorldHumanReadableName();

  // Associates an isolated world (see above for description) with a security
  // origin. XMLHttpRequest instances used in that world will be considered
  // to come from that origin, not the frame's.
  static void setIsolatedWorldSecurityOrigin(int worldId,
                                             PassRefPtr<SecurityOrigin>);
  SecurityOrigin* isolatedWorldSecurityOrigin();

  // Associated an isolated world with a Content Security Policy. Resources
  // embedded into the main world's DOM from script executed in an isolated
  // world should be restricted based on the isolated world's DOM, not the
  // main world's.
  //
  // FIXME: Right now, resource injection simply bypasses the main world's
  // DOM. More work is necessary to allow the isolated world's policy to be
  // applied correctly.
  static void setIsolatedWorldContentSecurityPolicy(int worldId,
                                                    const String& policy);
  bool isolatedWorldHasContentSecurityPolicy();

  bool isMainWorld() const { return m_worldId == MainWorldId; }
  bool isWorkerWorld() const { return m_worldId == WorkerWorldId; }
  bool isIsolatedWorld() const {
    return MainWorldId < m_worldId && m_worldId < IsolatedWorldIdLimit;
  }

  int worldId() const { return m_worldId; }
  DOMDataStore& domDataStore() const { return *m_domDataStore; }

  template <typename T>
  void registerDOMObjectHolder(v8::Isolate*, T*, v8::Local<v8::Value>);

 private:
  DOMWrapperWorld(v8::Isolate*, int worldId);

  static void weakCallbackForDOMObjectHolder(
      const v8::WeakCallbackInfo<DOMObjectHolderBase>&);
  void registerDOMObjectHolderInternal(std::unique_ptr<DOMObjectHolderBase>);
  void unregisterDOMObjectHolder(DOMObjectHolderBase*);

  // Dissociates all wrappers in all worlds associated with |scriptWrappable|.
  //
  // Do not use this function except for DOMWindow.  Only DOMWindow needs to
  // dissociate wrappers from the ScriptWrappable because of the following two
  // reasons.
  //
  // Reason 1) Case of the main world
  // A DOMWindow may be collected by Blink GC *before* V8 GC collects the
  // wrapper because the wrapper object associated with a DOMWindow is a global
  // proxy object, which remains after navigations.  We don't want V8 GC
  // to reset the weak persistent handle within the DOMWindow *after* Blink GC
  // collects the DOMWindow because it's use-after-free.  Thus, we need to
  // dissociate the wrapper in advance.
  //
  // Reason 2) Case of isolated worlds
  // As same, a DOMWindow may be collected before the wrapper gets collected.
  // A DOMWrapperMap supports mapping from ScriptWrappable* to v8::Global<T>,
  // and we don't want to leave an entry of an already-dead DOMWindow* to the
  // persistent handle for the global proxy object, especially considering that
  // the address to the already-dead DOMWindow* may be re-used.
  friend class DOMWindow;
  static void dissociateDOMWindowWrappersInAllWorlds(ScriptWrappable*);

  static unsigned isolatedWorldCount;

  const int m_worldId;
  std::unique_ptr<DOMDataStore> m_domDataStore;
  HashSet<std::unique_ptr<DOMObjectHolderBase>> m_domObjectHolders;
};

}  // namespace blink

#endif  // DOMWrapperWorld_h
