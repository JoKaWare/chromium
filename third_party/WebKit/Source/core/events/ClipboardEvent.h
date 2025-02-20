/*
 * Copyright (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2001 Tobias Anton (anton@stud.fbi.fh-darmstadt.de)
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
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

#ifndef ClipboardEvent_h
#define ClipboardEvent_h

#include "core/clipboard/DataTransfer.h"
#include "core/events/ClipboardEventInit.h"
#include "core/events/Event.h"

namespace blink {

class ClipboardEvent final : public Event {
  DEFINE_WRAPPERTYPEINFO();

 public:
  ~ClipboardEvent() override;

  static ClipboardEvent* create(const AtomicString& type,
                                bool canBubble,
                                bool cancelable,
                                DataTransfer* dataTransfer) {
    return new ClipboardEvent(type, canBubble, cancelable, dataTransfer);
  }

  static ClipboardEvent* create(const AtomicString& type,
                                const ClipboardEventInit& initializer) {
    return new ClipboardEvent(type, initializer);
  }

  DataTransfer* clipboardData() const { return m_clipboardData.get(); }

  DECLARE_VIRTUAL_TRACE();

 private:
  ClipboardEvent(const AtomicString& type,
                 bool canBubble,
                 bool cancelable,
                 DataTransfer* clipboardData);
  ClipboardEvent(const AtomicString& type, const ClipboardEventInit&);

  const AtomicString& interfaceName() const override;
  bool isClipboardEvent() const override;

  Member<DataTransfer> m_clipboardData;
};

}  // namespace blink

#endif  // ClipboardEvent_h
