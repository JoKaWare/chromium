/*
 * Copyright (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2001 Tobias Anton (anton@stud.fbi.fh-darmstadt.de)
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2008 Apple Inc. All rights reserved.
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

#ifndef DataTransfer_h
#define DataTransfer_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "core/CoreExport.h"
#include "core/clipboard/DataTransferAccessPolicy.h"
#include "core/loader/resource/ImageResourceContent.h"
#include "core/page/DragActions.h"
#include "platform/geometry/IntPoint.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"
#include <memory>

namespace blink {

class DataObject;
class DataTransferItemList;
class DragImage;
class Element;
class FileList;
class FrameSelection;
class LocalFrame;
class Node;

// Used for drag and drop and copy/paste.
// Drag and Drop:
// http://www.whatwg.org/specs/web-apps/current-work/multipage/dnd.html
// Clipboard API (copy/paste):
// http://dev.w3.org/2006/webapi/clipops/clipops.html
class CORE_EXPORT DataTransfer final
    : public GarbageCollectedFinalized<DataTransfer>,
      public ScriptWrappable {
  DEFINE_WRAPPERTYPEINFO();

 public:
  // Whether this transfer is serving a drag-drop, copy-paste, spellcheck,
  // auto-correct or similar request.
  enum DataTransferType {
    CopyAndPaste,
    DragAndDrop,
    InsertReplacementText,
  };

  static DataTransfer* create(DataTransferType,
                              DataTransferAccessPolicy,
                              DataObject*);
  ~DataTransfer();

  bool isForCopyAndPaste() const { return m_transferType == CopyAndPaste; }
  bool isForDragAndDrop() const { return m_transferType == DragAndDrop; }

  String dropEffect() const {
    return dropEffectIsUninitialized() ? "none" : m_dropEffect;
  }
  void setDropEffect(const String&);
  bool dropEffectIsUninitialized() const {
    return m_dropEffect == "uninitialized";
  }
  String effectAllowed() const { return m_effectAllowed; }
  void setEffectAllowed(const String&);

  void clearData(const String& type = String());
  String getData(const String& type) const;
  void setData(const String& type, const String& data);

  // extensions beyond IE's API
  Vector<String> types() const;
  FileList* files() const;

  IntPoint dragLocation() const { return m_dragLoc; }
  void setDragImage(Element*, int x, int y);
  void clearDragImage();
  void setDragImageResource(ImageResourceContent*, const IntPoint&);
  void setDragImageElement(Node*, const IntPoint&);

  std::unique_ptr<DragImage> createDragImage(IntPoint& dragLocation,
                                             LocalFrame*) const;
  void declareAndWriteDragImage(Element*,
                                const KURL& linkURL,
                                const KURL& imageURL,
                                const String& title);
  void writeURL(Node*, const KURL&, const String&);
  void writeSelection(const FrameSelection&);

  void setAccessPolicy(DataTransferAccessPolicy);
  bool canReadTypes() const;
  bool canReadData() const;
  bool canWriteData() const;
  // Note that the spec doesn't actually allow drag image modification outside
  // the dragstart event. This capability is maintained for backwards
  // compatiblity for ports that have supported this in the past. On many ports,
  // attempting to set a drag image outside the dragstart operation is a no-op
  // anyway.
  bool canSetDragImage() const;

  DragOperation sourceOperation() const;
  DragOperation destinationOperation() const;
  void setSourceOperation(DragOperation);
  void setDestinationOperation(DragOperation);

  bool hasDropZoneType(const String&);

  DataTransferItemList* items();

  DataObject* dataObject() const;

  DECLARE_TRACE();

 private:
  DataTransfer(DataTransferType, DataTransferAccessPolicy, DataObject*);

  void setDragImage(ImageResourceContent*, Node*, const IntPoint&);

  bool hasFileOfType(const String&) const;
  bool hasStringOfType(const String&) const;

  // Instead of using this member directly, prefer to use the can*() methods
  // above.
  DataTransferAccessPolicy m_policy;
  String m_dropEffect;
  String m_effectAllowed;
  DataTransferType m_transferType;
  Member<DataObject> m_dataObject;

  IntPoint m_dragLoc;
  Member<ImageResourceContent> m_dragImage;
  Member<Node> m_dragImageElement;
};

DragOperation convertDropZoneOperationToDragOperation(
    const String& dragOperation);
String convertDragOperationToDropZoneOperation(DragOperation);

}  // namespace blink

#endif  // DataTransfer_h
