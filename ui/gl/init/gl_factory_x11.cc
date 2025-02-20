// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/init/gl_factory.h"

#include "base/trace_event/trace_event.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_context_egl.h"
#include "ui/gl/gl_context_glx.h"
#include "ui/gl/gl_context_osmesa.h"
#include "ui/gl/gl_context_stub.h"
#include "ui/gl/gl_egl_api_implementation.h"
#include "ui/gl/gl_glx_api_implementation.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/gl_surface_egl_x11.h"
#include "ui/gl/gl_surface_glx.h"
#include "ui/gl/gl_surface_glx_x11.h"
#include "ui/gl/gl_surface_osmesa.h"
#include "ui/gl/gl_surface_osmesa_x11.h"
#include "ui/gl/gl_surface_stub.h"

namespace gl {
namespace init {

std::vector<GLImplementation> GetAllowedGLImplementations() {
  std::vector<GLImplementation> impls;
  impls.push_back(kGLImplementationDesktopGL);
  impls.push_back(kGLImplementationEGLGLES2);
  impls.push_back(kGLImplementationOSMesaGL);
  return impls;
}

bool GetGLWindowSystemBindingInfo(GLWindowSystemBindingInfo* info) {
  switch (GetGLImplementation()) {
    case kGLImplementationDesktopGL:
      return GetGLWindowSystemBindingInfoGLX(info);
    case kGLImplementationEGLGLES2:
      return GetGLWindowSystemBindingInfoEGL(info);
    default:
      return false;
  }
}

scoped_refptr<GLContext> CreateGLContext(GLShareGroup* share_group,
                                         GLSurface* compatible_surface,
                                         const GLContextAttribs& attribs) {
  TRACE_EVENT0("gpu", "gl::init::CreateGLContext");
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
      return InitializeGLContext(new GLContextOSMesa(share_group),
                                 compatible_surface, attribs);
    case kGLImplementationDesktopGL:
      return InitializeGLContext(new GLContextGLX(share_group),
                                 compatible_surface, attribs);
    case kGLImplementationEGLGLES2:
      return InitializeGLContext(new GLContextEGL(share_group),
                                 compatible_surface, attribs);
    case kGLImplementationMockGL:
      return new GLContextStub(share_group);
    case kGLImplementationStubGL: {
      scoped_refptr<GLContextStub> stub_context =
          new GLContextStub(share_group);
      stub_context->SetUseStubApi(true);
      return stub_context;
    }
    default:
      NOTREACHED();
      return nullptr;
  }
}

scoped_refptr<GLSurface> CreateViewGLSurface(gfx::AcceleratedWidget window) {
  TRACE_EVENT0("gpu", "gl::init::CreateViewGLSurface");
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
      return InitializeGLSurface(new GLSurfaceOSMesaX11(window));
    case kGLImplementationDesktopGL:
      return InitializeGLSurface(new GLSurfaceGLXX11(window));
    case kGLImplementationEGLGLES2:
      DCHECK(window != gfx::kNullAcceleratedWidget);
      return InitializeGLSurface(new NativeViewGLSurfaceEGLX11(window));
    case kGLImplementationMockGL:
    case kGLImplementationStubGL:
      return new GLSurfaceStub;
    default:
      NOTREACHED();
      return nullptr;
  }
}

scoped_refptr<GLSurface> CreateOffscreenGLSurfaceWithFormat(
    const gfx::Size& size, GLSurfaceFormat format) {
  TRACE_EVENT0("gpu", "gl::init::CreateOffscreenGLSurface");
  switch (GetGLImplementation()) {
    case kGLImplementationOSMesaGL:
      format.SetDefaultPixelLayout(GLSurfaceFormat::PIXEL_LAYOUT_RGBA);
      return InitializeGLSurfaceWithFormat(
          new GLSurfaceOSMesa(format, size), format);
    case kGLImplementationDesktopGL:
      return InitializeGLSurfaceWithFormat(
          new UnmappedNativeViewGLSurfaceGLX(size), format);
    case kGLImplementationEGLGLES2:
      return InitializeGLSurfaceWithFormat(
          new PbufferGLSurfaceEGL(size), format);
    case kGLImplementationMockGL:
    case kGLImplementationStubGL:
      return new GLSurfaceStub;
    default:
      NOTREACHED();
      return nullptr;
  }
}

}  // namespace init
}  // namespace gl
