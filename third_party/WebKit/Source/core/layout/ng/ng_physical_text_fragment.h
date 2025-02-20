// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NGPhysicalTextFragment_h
#define NGPhysicalTextFragment_h

#include "core/CoreExport.h"
#include "core/layout/ng/ng_block_node.h"
#include "core/layout/ng/ng_inline_node.h"
#include "core/layout/ng/ng_floating_object.h"
#include "core/layout/ng/ng_physical_fragment.h"
#include "platform/heap/Handle.h"

namespace blink {

class CORE_EXPORT NGPhysicalTextFragment final : public NGPhysicalFragment {
 public:
  NGPhysicalTextFragment(
      LayoutObject* layout_object,
      const NGInlineNode* node,
      unsigned start_index,
      unsigned end_index,
      NGPhysicalSize size,
      NGPhysicalSize overflow,
      PersistentHeapLinkedHashSet<WeakMember<NGBlockNode>>&
          out_of_flow_descendants,
      Vector<NGStaticPosition> out_of_flow_positions,
      Vector<Persistent<NGFloatingObject>>& unpositioned_floats,
      Vector<Persistent<NGFloatingObject>>& positioned_floats)
      : NGPhysicalFragment(layout_object,
                           size,
                           overflow,
                           kFragmentText,
                           out_of_flow_descendants,
                           out_of_flow_positions,
                           unpositioned_floats,
                           positioned_floats),
        node_(node),
        start_index_(start_index),
        end_index_(end_index) {}

  const NGInlineNode* Node() const { return node_; }

  // The range of NGLayoutInlineItem.
  // |StartIndex| shows the lower logical index, so the visual order iteration
  // for RTL should be done from |EndIndex - 1| to |StartIndex|.
  unsigned StartIndex() const { return start_index_; }
  unsigned EndIndex() const { return end_index_; }

 private:
  // TODO(kojii): NGInlineNode is to access text content and NGLayoutInlineItem.
  // Review if it's better to point them.
  Persistent<const NGInlineNode> node_;
  unsigned start_index_;
  unsigned end_index_;
};

DEFINE_TYPE_CASTS(NGPhysicalTextFragment,
                  NGPhysicalFragment,
                  text,
                  text->Type() == NGPhysicalFragment::kFragmentText,
                  text.Type() == NGPhysicalFragment::kFragmentText);

}  // namespace blink

#endif  // NGPhysicalTextFragment_h
