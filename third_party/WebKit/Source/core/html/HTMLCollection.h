/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2011, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef HTMLCollection_h
#define HTMLCollection_h

#include "core/CoreExport.h"
#include "core/dom/LiveNodeListBase.h"
#include "core/html/CollectionItemsCache.h"
#include "core/html/CollectionType.h"
#include "wtf/Forward.h"

namespace blink {

// A simple iterator based on an index number in an HTMLCollection.
// This doesn't work if the HTMLCollection is updated during iteration.
template <class CollectionType, class NodeType>
class HTMLCollectionIterator {
  STACK_ALLOCATED();

 public:
  explicit HTMLCollectionIterator(const CollectionType* collection)
      : m_collection(collection) {}
  NodeType* operator*() { return m_collection->item(m_index); }

  void operator++() {
    if (m_index < m_collection->length())
      ++m_index;
  }

  bool operator!=(const HTMLCollectionIterator& other) const {
    return m_collection != other.m_collection || m_index != other.m_index;
  }

  static HTMLCollectionIterator createEnd(const CollectionType* collection) {
    HTMLCollectionIterator iterator(collection);
    iterator.m_index = collection->length();
    return iterator;
  }

 private:
  Member<const CollectionType> m_collection;
  unsigned m_index = 0;
};

// blink::HTMLCollection implements HTMLCollection IDL interface.
class CORE_EXPORT HTMLCollection
    : public GarbageCollectedFinalized<HTMLCollection>,
      public ScriptWrappable,
      public LiveNodeListBase {
  DEFINE_WRAPPERTYPEINFO();
  USING_GARBAGE_COLLECTED_MIXIN(HTMLCollection);

 public:
  enum ItemAfterOverrideType {
    OverridesItemAfter,
    DoesNotOverrideItemAfter,
  };

  static HTMLCollection* create(ContainerNode& base, CollectionType);
  virtual ~HTMLCollection();
  void invalidateCache(Document* oldDocument = 0) const override;
  void invalidateCacheForAttribute(const QualifiedName*) const;

  // DOM API
  unsigned length() const;
  Element* item(unsigned offset) const;
  virtual Element* namedItem(const AtomicString& name) const;
  bool namedPropertyQuery(const AtomicString&, ExceptionState&);
  void namedPropertyEnumerator(Vector<String>& names, ExceptionState&);

  // Non-DOM API
  void namedItems(const AtomicString& name, HeapVector<Member<Element>>&) const;
  bool isEmpty() const { return m_collectionItemsCache.isEmpty(*this); }
  bool hasExactlyOneItem() const {
    return m_collectionItemsCache.hasExactlyOneNode(*this);
  }
  bool elementMatches(const Element&) const;

  // CollectionIndexCache API.
  bool canTraverseBackward() const { return !overridesItemAfter(); }
  Element* traverseToFirst() const;
  Element* traverseToLast() const;
  Element* traverseForwardToOffset(unsigned offset,
                                   Element& currentElement,
                                   unsigned& currentOffset) const;
  Element* traverseBackwardToOffset(unsigned offset,
                                    Element& currentElement,
                                    unsigned& currentOffset) const;

  using Iterator = HTMLCollectionIterator<HTMLCollection, Element>;
  Iterator begin() const { return Iterator(this); }
  Iterator end() const { return Iterator::createEnd(this); }

  DECLARE_VIRTUAL_TRACE();

 protected:
  HTMLCollection(ContainerNode& base, CollectionType, ItemAfterOverrideType);

  class NamedItemCache final : public GarbageCollected<NamedItemCache> {
   public:
    static NamedItemCache* create() { return new NamedItemCache; }

    HeapVector<Member<Element>>* getElementsById(const AtomicString& id) const {
      return m_idCache.get(id.impl());
    }
    HeapVector<Member<Element>>* getElementsByName(
        const AtomicString& name) const {
      return m_nameCache.get(name.impl());
    }
    void addElementWithId(const AtomicString& id, Element* element) {
      addElementToMap(m_idCache, id, element);
    }
    void addElementWithName(const AtomicString& name, Element* element) {
      addElementToMap(m_nameCache, name, element);
    }

    DEFINE_INLINE_TRACE() {
      visitor->trace(m_idCache);
      visitor->trace(m_nameCache);
    }

   private:
    NamedItemCache();
    typedef HeapHashMap<StringImpl*, Member<HeapVector<Member<Element>>>>
        StringToElementsMap;
    static void addElementToMap(StringToElementsMap& map,
                                const AtomicString& key,
                                Element* element) {
      Member<HeapVector<Member<Element>>>& vector =
          map.insert(key.impl(), nullptr).storedValue->value;
      if (!vector)
        vector = new HeapVector<Member<Element>>;
      vector->push_back(element);
    }

    StringToElementsMap m_idCache;
    StringToElementsMap m_nameCache;
  };

  bool overridesItemAfter() const { return m_overridesItemAfter; }
  virtual Element* virtualItemAfter(Element*) const;
  bool shouldOnlyIncludeDirectChildren() const {
    return m_shouldOnlyIncludeDirectChildren;
  }
  virtual void supportedPropertyNames(Vector<String>& names);

  virtual void updateIdNameCache() const;
  bool hasValidIdNameCache() const { return m_namedItemCache; }

  void setNamedItemCache(NamedItemCache* cache) const {
    DCHECK(!m_namedItemCache);
    // Do not repeat registration for the same invalidation type.
    if (invalidationType() != InvalidateOnIdNameAttrChange)
      document().registerNodeListWithIdNameCache(this);
    m_namedItemCache = std::move(cache);
  }

  NamedItemCache& namedItemCache() const {
    DCHECK(m_namedItemCache);
    return *m_namedItemCache;
  }

 private:
  void invalidateIdNameCacheMaps(Document* oldDocument = 0) const {
    if (!hasValidIdNameCache())
      return;

    // Make sure we decrement the NodeListWithIdNameCache count from
    // the old document instead of the new one in the case the collection
    // is moved to a new document.
    unregisterIdNameCacheFromDocument(oldDocument ? *oldDocument : document());

    m_namedItemCache.clear();
  }

  void unregisterIdNameCacheFromDocument(Document& document) const {
    DCHECK(hasValidIdNameCache());
    // Do not repeat unregistration for the same invalidation type.
    if (invalidationType() != InvalidateOnIdNameAttrChange)
      document.unregisterNodeListWithIdNameCache(this);
  }

  const unsigned m_overridesItemAfter : 1;
  const unsigned m_shouldOnlyIncludeDirectChildren : 1;
  mutable Member<NamedItemCache> m_namedItemCache;
  mutable CollectionItemsCache<HTMLCollection, Element> m_collectionItemsCache;
};

DEFINE_TYPE_CASTS(HTMLCollection,
                  LiveNodeListBase,
                  collection,
                  isHTMLCollectionType(collection->type()),
                  isHTMLCollectionType(collection.type()));

DISABLE_CFI_PERF
inline void HTMLCollection::invalidateCacheForAttribute(
    const QualifiedName* attrName) const {
  if (!attrName ||
      shouldInvalidateTypeOnAttributeChange(invalidationType(), *attrName))
    invalidateCache();
  else if (*attrName == HTMLNames::idAttr || *attrName == HTMLNames::nameAttr)
    invalidateIdNameCacheMaps();
}

}  // namespace blink

#endif  // HTMLCollection_h
