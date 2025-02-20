// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LinkPreloadResourceClients_h
#define LinkPreloadResourceClients_h

#include "core/loader/resource/CSSStyleSheetResource.h"
#include "core/loader/resource/FontResource.h"
#include "core/loader/resource/ImageResource.h"
#include "core/loader/resource/ScriptResource.h"
#include "core/loader/resource/StyleSheetResourceClient.h"
#include "platform/loader/fetch/RawResource.h"
#include "platform/loader/fetch/ResourceOwner.h"

namespace blink {

class LinkLoader;

class LinkPreloadResourceClient
    : public GarbageCollectedFinalized<LinkPreloadResourceClient> {
 public:
  virtual ~LinkPreloadResourceClient() {}

  void triggerEvents(const Resource*);
  virtual void clear() = 0;

  DEFINE_INLINE_VIRTUAL_TRACE() { visitor->trace(m_loader); }

 protected:
  explicit LinkPreloadResourceClient(LinkLoader* loader) : m_loader(loader) {
    DCHECK(loader);
  }

 private:
  Member<LinkLoader> m_loader;
};

class LinkPreloadScriptResourceClient
    : public LinkPreloadResourceClient,
      public ResourceOwner<ScriptResource, ScriptResourceClient> {
  USING_GARBAGE_COLLECTED_MIXIN(LinkPreloadScriptResourceClient);

 public:
  static LinkPreloadScriptResourceClient* create(LinkLoader* loader,
                                                 ScriptResource* resource) {
    return new LinkPreloadScriptResourceClient(loader, resource);
  }

  virtual String debugName() const { return "LinkPreloadScript"; }
  virtual ~LinkPreloadScriptResourceClient() {}

  void clear() override { clearResource(); }

  void notifyFinished(Resource* resource) override {
    DCHECK_EQ(this->resource(), resource);
    triggerEvents(resource);
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    LinkPreloadResourceClient::trace(visitor);
    ResourceOwner<ScriptResource, ScriptResourceClient>::trace(visitor);
  }

 private:
  LinkPreloadScriptResourceClient(LinkLoader* loader, ScriptResource* resource)
      : LinkPreloadResourceClient(loader) {
    setResource(resource, Resource::DontMarkAsReferenced);
  }
};

class LinkPreloadStyleResourceClient
    : public LinkPreloadResourceClient,
      public ResourceOwner<CSSStyleSheetResource, StyleSheetResourceClient> {
  USING_GARBAGE_COLLECTED_MIXIN(LinkPreloadStyleResourceClient);

 public:
  static LinkPreloadStyleResourceClient* create(
      LinkLoader* loader,
      CSSStyleSheetResource* resource) {
    return new LinkPreloadStyleResourceClient(loader, resource);
  }

  virtual String debugName() const { return "LinkPreloadStyle"; }
  virtual ~LinkPreloadStyleResourceClient() {}

  void clear() override { clearResource(); }

  void setCSSStyleSheet(const String&,
                        const KURL&,
                        const String&,
                        const CSSStyleSheetResource* resource) override {
    DCHECK_EQ(this->resource(), resource);
    triggerEvents(static_cast<const Resource*>(resource));
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    LinkPreloadResourceClient::trace(visitor);
    ResourceOwner<CSSStyleSheetResource, StyleSheetResourceClient>::trace(
        visitor);
  }

 private:
  LinkPreloadStyleResourceClient(LinkLoader* loader,
                                 CSSStyleSheetResource* resource)
      : LinkPreloadResourceClient(loader) {
    setResource(resource, Resource::DontMarkAsReferenced);
  }
};

class LinkPreloadImageResourceClient : public LinkPreloadResourceClient,
                                       public ResourceOwner<ImageResource> {
  USING_GARBAGE_COLLECTED_MIXIN(LinkPreloadImageResourceClient);

 public:
  static LinkPreloadImageResourceClient* create(LinkLoader* loader,
                                                ImageResource* resource) {
    return new LinkPreloadImageResourceClient(loader, resource);
  }

  virtual String debugName() const { return "LinkPreloadImage"; }
  virtual ~LinkPreloadImageResourceClient() {}

  void clear() override { clearResource(); }

  void notifyFinished(Resource* resource) override {
    DCHECK_EQ(this->resource(), toImageResource(resource));
    triggerEvents(resource);
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    LinkPreloadResourceClient::trace(visitor);
    ResourceOwner<ImageResource>::trace(visitor);
  }

 private:
  LinkPreloadImageResourceClient(LinkLoader* loader, ImageResource* resource)
      : LinkPreloadResourceClient(loader) {
    setResource(resource, Resource::DontMarkAsReferenced);
  }
};

class LinkPreloadFontResourceClient
    : public LinkPreloadResourceClient,
      public ResourceOwner<FontResource, FontResourceClient> {
  USING_GARBAGE_COLLECTED_MIXIN(LinkPreloadFontResourceClient);

 public:
  static LinkPreloadFontResourceClient* create(LinkLoader* loader,
                                               FontResource* resource) {
    return new LinkPreloadFontResourceClient(loader, resource);
  }

  virtual String debugName() const { return "LinkPreloadFont"; }
  virtual ~LinkPreloadFontResourceClient() {}

  void clear() override { clearResource(); }

  void notifyFinished(Resource* resource) override {
    DCHECK_EQ(this->resource(), toFontResource(resource));
    triggerEvents(resource);
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    LinkPreloadResourceClient::trace(visitor);
    ResourceOwner<FontResource, FontResourceClient>::trace(visitor);
  }

 private:
  LinkPreloadFontResourceClient(LinkLoader* loader, FontResource* resource)
      : LinkPreloadResourceClient(loader) {
    setResource(resource, Resource::DontMarkAsReferenced);
  }
};

class LinkPreloadRawResourceClient
    : public LinkPreloadResourceClient,
      public ResourceOwner<RawResource, RawResourceClient> {
  USING_GARBAGE_COLLECTED_MIXIN(LinkPreloadRawResourceClient);

 public:
  static LinkPreloadRawResourceClient* create(LinkLoader* loader,
                                              RawResource* resource) {
    return new LinkPreloadRawResourceClient(loader, resource);
  }

  virtual String debugName() const { return "LinkPreloadRaw"; }
  virtual ~LinkPreloadRawResourceClient() {}

  void clear() override { clearResource(); }

  void notifyFinished(Resource* resource) override {
    DCHECK_EQ(this->resource(), toRawResource(resource));
    triggerEvents(resource);
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    LinkPreloadResourceClient::trace(visitor);
    ResourceOwner<RawResource, RawResourceClient>::trace(visitor);
  }

 private:
  LinkPreloadRawResourceClient(LinkLoader* loader, RawResource* resource)
      : LinkPreloadResourceClient(loader) {
    setResource(resource, Resource::DontMarkAsReferenced);
  }
};

}  // namespace blink

#endif  // LinkPreloadResourceClients_h
