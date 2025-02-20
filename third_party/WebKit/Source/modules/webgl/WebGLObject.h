/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebGLObject_h
#define WebGLObject_h

#include "bindings/core/v8/DOMWrapperWorld.h"
#include "bindings/core/v8/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "wtf/Assertions.h"

namespace gpu {
namespace gles2 {
class GLES2Interface;
}
}

namespace blink {

class WebGLContextGroup;
class WebGLRenderingContextBase;

template <typename T>
GLuint objectOrZero(const T* object) {
  return object ? object->object() : 0;
}

template <typename T>
GLuint objectNonZero(const T* object) {
  GLuint result = object->object();
  DCHECK(result);
  return result;
}

class WebGLObject : public GarbageCollectedFinalized<WebGLObject>,
                    public ScriptWrappable {
  WTF_MAKE_NONCOPYABLE(WebGLObject);

 public:
  // We can't call virtual functions like deleteObjectImpl in this class's
  // destructor; doing so results in a pure virtual function call. Further,
  // making this destructor non-virtual is complicated with respect to
  // Oilpan tracing. Therefore this destructor is declared virtual, but is
  // empty, and the code that would have gone into its body is called by
  // subclasses via runDestructor().
  virtual ~WebGLObject();

  // deleteObject may not always delete the OpenGL resource.  For programs and
  // shaders, deletion is delayed until they are no longer attached.
  // FIXME: revisit this when resource sharing between contexts are implemented.
  void deleteObject(gpu::gles2::GLES2Interface*);

  void onAttached() { ++m_attachmentCount; }
  void onDetached(gpu::gles2::GLES2Interface*);

  // This indicates whether the client side issue a delete call already, not
  // whether the OpenGL resource is deleted.
  // object()==0 indicates the OpenGL resource is deleted.
  bool isDeleted() { return m_deleted; }

  // True if this object belongs to the group or context.
  virtual bool validate(const WebGLContextGroup*,
                        const WebGLRenderingContextBase*) const = 0;
  virtual bool hasObject() const = 0;

  // WebGLObjects are eagerly finalized, and the WebGLRenderingContextBase
  // is specifically not. This is done in order to allow WebGLObjects to
  // refer back to their owning context in their destructor to delete their
  // resources if they are GC'd before the context is.
  EAGERLY_FINALIZE();

  DEFINE_INLINE_VIRTUAL_TRACE() {}

  DECLARE_VIRTUAL_TRACE_WRAPPERS();

 protected:
  explicit WebGLObject(WebGLRenderingContextBase*);

  // deleteObjectImpl should be only called once to delete the OpenGL resource.
  // After calling deleteObjectImpl, hasObject() should return false.
  virtual void deleteObjectImpl(gpu::gles2::GLES2Interface*) = 0;

  virtual bool hasGroupOrContext() const = 0;

  // Return the current number of context losses associated with this
  // object's context group (if it's a shared object), or its
  // context's context group (if it's a per-context object).
  virtual uint32_t currentNumberOfContextLosses() const = 0;

  uint32_t cachedNumberOfContextLosses() const;

  void detach();
  void detachAndDeleteObject();

  virtual gpu::gles2::GLES2Interface* getAGLInterface() const = 0;

  // Used by leaf subclasses to run the destruction sequence -- what would
  // be in the destructor of the base class, if it could be. Must be called
  // no more than once.
  void runDestructor();

  // Indicates to subclasses that the destructor is being run.
  bool destructionInProgress() const;

 private:
  // This was the number of context losses of the object's associated
  // WebGLContextGroup at the time this object was created. Contexts
  // no longer refer to all the objects that they ever created, so
  // it's necessary to check this count when validating each object.
  uint32_t m_cachedNumberOfContextLosses;

  unsigned m_attachmentCount;

  // Indicates whether the WebGL context's deletion function for this
  // object (deleteBuffer, deleteTexture, etc.) has been called.
  bool m_deleted;

  // Indicates whether the destructor has been entered and we therefore
  // need to be careful in subclasses to not touch other on-heap objects.
  bool m_destructionInProgress;
};

}  // namespace blink

#endif  // WebGLObject_h
