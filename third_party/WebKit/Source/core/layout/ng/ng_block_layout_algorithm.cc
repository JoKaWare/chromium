// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_block_layout_algorithm.h"

#include "core/layout/ng/ng_absolute_utils.h"
#include "core/layout/ng/ng_block_break_token.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_column_mapper.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_fragment.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_inline_node.h"
#include "core/layout/ng/ng_layout_opportunity_iterator.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_line_builder.h"
#include "core/layout/ng/ng_out_of_flow_layout_part.h"
#include "core/layout/ng/ng_units.h"
#include "core/style/ComputedStyle.h"
#include "platform/LengthFunctions.h"
#include "wtf/Optional.h"

namespace blink {
namespace {

// Whether child's constraint space should shrink to its intrinsic width.
// This is needed for buttons, select, input, floats and orthogonal children.
// See LayoutBox::sizesLogicalWidthToFitContent for the rationale behind this.
bool ShouldShrinkToFit(const NGConstraintSpace& parent_space,
                       const ComputedStyle& child_style) {
  NGWritingMode child_writing_mode =
      FromPlatformWritingMode(child_style.getWritingMode());
  // Whether the child and the containing block are parallel to each other.
  // Example: vertical-rl and vertical-lr
  bool is_in_parallel_flow =
      (parent_space.WritingMode() == kHorizontalTopBottom) ==
      (child_writing_mode == kHorizontalTopBottom);

  return child_style.display() == EDisplay::InlineBlock ||
         child_style.isFloating() || !is_in_parallel_flow;
}

// Updates the fragment's BFC offset if it's not already set.
void UpdateFragmentBfcOffset(const NGLogicalOffset& offset,
                             NGFragmentBuilder* builder) {
  if (!builder->BfcOffset())
    builder->SetBfcOffset(offset);
}

// Adjusts content_size to respect the CSS "clear" property.
// Picks up the maximum between left/right exclusions and content_size depending
// on the value of style.clear() property.
void AdjustToClearance(const std::shared_ptr<NGExclusions>& exclusions,
                       const ComputedStyle& style,
                       const NGLogicalOffset& from_offset,
                       LayoutUnit* content_size) {
  DCHECK(content_size) << "content_size cannot be null here";
  const NGExclusion* right_exclusion = exclusions->last_right_float;
  const NGExclusion* left_exclusion = exclusions->last_left_float;

  LayoutUnit left_block_end_offset = *content_size;
  if (left_exclusion) {
    left_block_end_offset = std::max(
        left_exclusion->rect.BlockEndOffset() - from_offset.block_offset,
        *content_size);
  }
  LayoutUnit right_block_end_offset = *content_size;
  if (right_exclusion) {
    right_block_end_offset = std::max(
        right_exclusion->rect.BlockEndOffset() - from_offset.block_offset,
        *content_size);
  }

  switch (style.clear()) {
    case EClear::kNone:
      return;  // nothing to do here.
    case EClear::kLeft:
      *content_size = left_block_end_offset;
      break;
    case EClear::kRight:
      *content_size = right_block_end_offset;
      break;
    case EClear::kBoth:
      *content_size = std::max(left_block_end_offset, right_block_end_offset);
      break;
    default:
      ASSERT_NOT_REACHED();
  }
}

// Creates an exclusion from the fragment that will be placed in the provided
// layout opportunity.
NGExclusion CreateExclusion(const NGFragment& fragment,
                            const NGLayoutOpportunity& opportunity,
                            const LayoutUnit float_offset,
                            const NGBoxStrut& margins,
                            NGExclusion::Type exclusion_type) {
  NGExclusion exclusion;
  exclusion.type = exclusion_type;
  NGLogicalRect& rect = exclusion.rect;
  rect.offset = opportunity.offset;
  rect.offset.inline_offset += float_offset;

  rect.size.inline_size = fragment.InlineSize() + margins.InlineSum();
  rect.size.block_size = fragment.BlockSize() + margins.BlockSum();
  return exclusion;
}

// Adjusts the provided offset to the top edge alignment rule.
// Top edge alignment rule: the outer top of a floating box may not be higher
// than the outer top of any block or floated box generated by an element
// earlier in the source document.
NGLogicalOffset AdjustToTopEdgeAlignmentRule(const NGConstraintSpace& space,
                                             const NGLogicalOffset& offset) {
  NGLogicalOffset adjusted_offset = offset;
  LayoutUnit& adjusted_block_offset = adjusted_offset.block_offset;
  if (space.Exclusions()->last_left_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_left_float->rect.BlockStartOffset());
  if (space.Exclusions()->last_right_float)
    adjusted_block_offset =
        std::max(adjusted_block_offset,
                 space.Exclusions()->last_right_float->rect.BlockStartOffset());
  return adjusted_offset;
}

// Finds a layout opportunity for the fragment.
// It iterates over all layout opportunities in the constraint space and returns
// the first layout opportunity that is wider than the fragment or returns the
// last one which is always the widest.
//
// @param space Constraint space that is used to find layout opportunity for
//              the fragment.
// @param fragment Fragment that needs to be placed.
// @param origin_point {@code space}'s offset relative to the space that
//                     establishes a new formatting context that we're currently
//                     in and where all our exclusions reside.
// @param margins Margins of the fragment.
// @return Layout opportunity for the fragment.
const NGLayoutOpportunity FindLayoutOpportunityForFragment(
    NGConstraintSpace* space,
    const NGFragment& fragment,
    const NGLogicalOffset& origin_point,
    const NGBoxStrut& margins) {
  NGLogicalOffset adjusted_origin_point =
      AdjustToTopEdgeAlignmentRule(*space, origin_point);

  NGLayoutOpportunityIterator opportunity_iter(space, adjusted_origin_point);
  NGLayoutOpportunity opportunity;
  NGLayoutOpportunity opportunity_candidate = opportunity_iter.Next();

  while (!opportunity_candidate.IsEmpty()) {
    opportunity = opportunity_candidate;
    // Checking opportunity's block size is not necessary as a float cannot be
    // positioned on top of another float inside of the same constraint space.
    auto fragment_inline_size = fragment.InlineSize() + margins.InlineSum();
    if (opportunity.size.inline_size >= fragment_inline_size)
      break;

    opportunity_candidate = opportunity_iter.Next();
  }
  return opportunity;
}

// Calculates the logical offset for opportunity.
NGLogicalOffset CalculateLogicalOffsetForOpportunity(
    const NGLayoutOpportunity& opportunity,
    const LayoutUnit float_offset,
    const NGBoxStrut& margins,
    const NGLogicalOffset& space_offset) {
  // Adjust to child's margin.
  LayoutUnit inline_offset = margins.inline_start;
  LayoutUnit block_offset = margins.block_start;

  // Offset from the opportunity's block/inline start.
  inline_offset += opportunity.offset.inline_offset;
  block_offset += opportunity.offset.block_offset;

  inline_offset += float_offset;

  block_offset -= space_offset.block_offset;
  inline_offset -= space_offset.inline_offset;

  return NGLogicalOffset(inline_offset, block_offset);
}

// Calculates the relative position from {@code from_offset} of the
// floating object that is requested to be positioned from {@code origin_point}.
NGLogicalOffset PositionFloat(const NGLogicalOffset& origin_point,
                              const NGLogicalOffset& from_offset,
                              NGFloatingObject* floating_object) {
  NGConstraintSpace* float_space = floating_object->space;
  DCHECK(floating_object->fragment) << "Fragment cannot be null here";

  // TODO(ikilpatrick): The writing mode switching here looks wrong.
  NGBoxFragment float_fragment(
      float_space->WritingMode(),
      toNGPhysicalBoxFragment(floating_object->fragment.get()));

  // Find a layout opportunity that will fit our float.
  const NGLayoutOpportunity opportunity =
      FindLayoutOpportunityForFragment(floating_object->space, float_fragment,
                                       origin_point, floating_object->margins);
  DCHECK(!opportunity.IsEmpty()) << "Opportunity is empty but it shouldn't be";

  // Calculate the float offset if needed.
  LayoutUnit float_offset;
  if (floating_object->exclusion_type == NGExclusion::kFloatRight) {
    float_offset = opportunity.size.inline_size - float_fragment.InlineSize();
  }

  // Add the float as an exclusion.
  const NGExclusion exclusion = CreateExclusion(
      float_fragment, opportunity, float_offset, floating_object->margins,
      floating_object->exclusion_type);
  float_space->AddExclusion(exclusion);

  return CalculateLogicalOffsetForOpportunity(
      opportunity, float_offset, floating_object->margins, from_offset);
}

// Positions pending floats stored on the fragment builder starting from
// {@code origin_point}.
void PositionPendingFloats(const NGLogicalOffset& origin_point,
                           NGFragmentBuilder* builder) {
  DCHECK(builder->BfcOffset()) << "Parent BFC offset should be known here";
  NGLogicalOffset from_offset = builder->BfcOffset().value();

  for (auto& floating_object : builder->UnpositionedFloats()) {
    NGLogicalOffset float_fragment_offset =
        PositionFloat(origin_point, from_offset, floating_object);
    builder->AddFloatingObject(floating_object, float_fragment_offset);
  }
  builder->MutableUnpositionedFloats().clear();
}

// Whether an in-flow block-level child creates a new formatting context.
//
// This will *NOT* check the following cases:
//  - The child is out-of-flow, e.g. floating or abs-pos.
//  - The child is a inline-level, e.g. "display: inline-block".
//  - The child establishes a new formatting context, but should be a child of
//    another layout algorithm, e.g. "display: table-caption" or flex-item.
bool IsNewFormattingContextForInFlowBlockLevelChild(
    const NGConstraintSpace& space,
    const ComputedStyle& style) {
  // TODO(layout-dev): This doesn't capture a few cases which can't be computed
  // directly from style yet:
  //  - The child is a <fieldset>.
  //  - "column-span: all" is set on the child (requires knowledge that we are
  //    in a multi-col formatting context).
  //    (https://drafts.csswg.org/css-multicol-1/#valdef-column-span-all)

  if (style.specifiesColumns() || style.containsPaint() ||
      style.containsLayout())
    return true;

  if (!style.isOverflowVisible())
    return true;

  EDisplay display = style.display();
  if (display == EDisplay::Grid || display == EDisplay::Flex ||
      display == EDisplay::WebkitBox)
    return true;

  if (space.WritingMode() != FromPlatformWritingMode(style.getWritingMode()))
    return true;

  return false;
}

}  // namespace

NGBlockLayoutAlgorithm::NGBlockLayoutAlgorithm(
    LayoutObject* layout_object,
    PassRefPtr<const ComputedStyle> style,
    NGLayoutInputNode* first_child,
    NGConstraintSpace* constraint_space,
    NGBreakToken* break_token)
    : style_(style),
      first_child_(first_child),
      constraint_space_(constraint_space),
      break_token_(break_token),
      builder_(WTF::wrapUnique(
          new NGFragmentBuilder(NGPhysicalFragment::kFragmentBox,
                                layout_object))) {
  DCHECK(style_);
}

Optional<MinAndMaxContentSizes>
NGBlockLayoutAlgorithm::ComputeMinAndMaxContentSizes() const {
  MinAndMaxContentSizes sizes;

  // Size-contained elements don't consider their contents for intrinsic sizing.
  if (Style().containsSize())
    return sizes;

  // TODO: handle floats & orthogonal children.
  for (NGLayoutInputNode* node = first_child_; node;
       node = node->NextSibling()) {
    Optional<MinAndMaxContentSizes> child_minmax;
    if (node->Type() == NGLayoutInputNode::kLegacyInline) {
      // TODO(kojii): Implement when there are inline children.
      return child_minmax;
    }
    NGBlockNode* block_child = toNGBlockNode(node);
    if (NeedMinAndMaxContentSizesForContentContribution(block_child->Style())) {
      child_minmax = block_child->ComputeMinAndMaxContentSizes();
    }

    MinAndMaxContentSizes child_sizes =
        ComputeMinAndMaxContentContribution(block_child->Style(), child_minmax);

    sizes.min_content = std::max(sizes.min_content, child_sizes.min_content);
    sizes.max_content = std::max(sizes.max_content, child_sizes.max_content);
  }

  sizes.max_content = std::max(sizes.min_content, sizes.max_content);
  return sizes;
}

NGLogicalOffset NGBlockLayoutAlgorithm::CalculateLogicalOffset(
    const WTF::Optional<NGLogicalOffset>& known_fragment_offset) {
  LayoutUnit inline_offset =
      border_and_padding_.inline_start + curr_child_margins_.inline_start;
  LayoutUnit block_offset = content_size_;
  if (known_fragment_offset) {
    block_offset = known_fragment_offset.value().block_offset -
                   builder_->BfcOffset().value().block_offset;
  }
  return {inline_offset, block_offset};
}

RefPtr<NGPhysicalFragment> NGBlockLayoutAlgorithm::Layout() {
  WTF::Optional<MinAndMaxContentSizes> sizes;
  if (NeedMinAndMaxContentSizes(ConstraintSpace(), Style()))
    sizes = ComputeMinAndMaxContentSizes();

  border_and_padding_ =
      ComputeBorders(Style()) + ComputePadding(ConstraintSpace(), Style());

  LayoutUnit inline_size =
      ComputeInlineSizeForFragment(ConstraintSpace(), Style(), sizes);
  LayoutUnit adjusted_inline_size =
      inline_size - border_and_padding_.InlineSum();
  // TODO(layout-ng): For quirks mode, should we pass blockSize instead of
  // -1?
  LayoutUnit block_size =
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(), NGSizeIndefinite);
  LayoutUnit adjusted_block_size(block_size);
  // Our calculated block-axis size may be indefinite at this point.
  // If so, just leave the size as NGSizeIndefinite instead of subtracting
  // borders and padding.
  if (adjusted_block_size != NGSizeIndefinite)
    adjusted_block_size -= border_and_padding_.BlockSum();

  space_builder_ = new NGConstraintSpaceBuilder(constraint_space_);
  if (Style().specifiesColumns()) {
    space_builder_->SetFragmentationType(kFragmentColumn);
    adjusted_inline_size =
        ResolveUsedColumnInlineSize(adjusted_inline_size, Style());
    LayoutUnit inline_progression =
        adjusted_inline_size + ResolveUsedColumnGap(Style());
    fragmentainer_mapper_ =
        new NGColumnMapper(inline_progression, adjusted_block_size);
  }
  space_builder_->SetAvailableSize(
      NGLogicalSize(adjusted_inline_size, adjusted_block_size));
  space_builder_->SetPercentageResolutionSize(
      NGLogicalSize(adjusted_inline_size, adjusted_block_size));

  builder_->SetDirection(constraint_space_->Direction());
  builder_->SetWritingMode(constraint_space_->WritingMode());
  builder_->SetInlineSize(inline_size).SetBlockSize(block_size);

  // TODO(glebl): fix multicol after the new margin collapsing/floats algorithm
  // based on BFCOffset is checked in.
  if (NGBlockBreakToken* token = CurrentBlockBreakToken()) {
    // Resume after a previous break.
    content_size_ = token->BreakOffset();
    current_child_ = token->InputNode();
  } else {
    content_size_ = border_and_padding_.block_start;
    current_child_ = first_child_;
  }

  curr_margin_strut_ = ConstraintSpace().MarginStrut();
  curr_bfc_offset_ = ConstraintSpace().BfcOffset();

  // Margins collapsing:
  //   Do not collapse margins between parent and its child if there is
  //   border/padding between them.
  if (border_and_padding_.block_start ||
      ConstraintSpace().IsNewFormattingContext()) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    builder_->SetBfcOffset(curr_bfc_offset_);
    curr_margin_strut_ = NGMarginStrut();
  }
  curr_bfc_offset_.block_offset += content_size_;

  while (current_child_) {
    if (current_child_->Type() == NGLayoutInputNode::kLegacyBlock) {
      NGBlockNode* current_block_child = toNGBlockNode(current_child_);
      EPosition position = current_block_child->Style().position();
      if (position == AbsolutePosition || position == FixedPosition) {
        builder_->AddOutOfFlowChildCandidate(current_block_child,
                                             GetChildSpaceOffset());
        current_child_ = current_block_child->NextSibling();
        continue;
      }
    }

    DCHECK(!ConstraintSpace().HasBlockFragmentation() ||
           SpaceAvailableForCurrentChild() > LayoutUnit());
    space_for_current_child_ = CreateConstraintSpaceForCurrentChild();

    if (current_child_->Type() == NGLayoutInputNode::kLegacyInline) {
      LayoutInlineChildren(toNGInlineNode(current_child_));
      continue;
    }

    RefPtr<NGPhysicalFragment> physical_fragment =
        current_child_->Layout(space_for_current_child_);

    FinishCurrentChildLayout(toNGPhysicalBoxFragment(physical_fragment.get()));

    if (!ProceedToNextUnfinishedSibling(physical_fragment.get()))
      break;
  }

  // Margins collapsing:
  //   Bottom margins of an in-flow block box doesn't collapse with its last
  //   in-flow block-level child's bottom margin if the box has bottom
  //   border/padding.
  content_size_ += border_and_padding_.block_end;
  if (border_and_padding_.block_end ||
      ConstraintSpace().IsNewFormattingContext()) {
    content_size_ += curr_margin_strut_.Sum();
    curr_margin_strut_ = NGMarginStrut();
  }

  // Recompute the block-axis size now that we know our content size.
  block_size =
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(), content_size_);
  builder_->SetBlockSize(block_size);

  // Layout our absolute and fixed positioned children.
  NGOutOfFlowLayoutPart(Style(), builder_.get()).Run();

  // Non empty blocks always know their position in space:
  if (block_size) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    UpdateFragmentBfcOffset(curr_bfc_offset_, builder_.get());
    PositionPendingFloats(curr_bfc_offset_, builder_.get());
  }

  // Margins collapsing:
  //   Do not collapse margins between the last in-flow child and bottom margin
  //   of its parent if the parent has height != auto()
  if (!Style().logicalHeight().isAuto()) {
    // TODO(glebl): handle minLogicalHeight, maxLogicalHeight.
    curr_margin_strut_ = NGMarginStrut();
  }
  builder_->SetEndMarginStrut(curr_margin_strut_);

  builder_->SetInlineOverflow(max_inline_size_).SetBlockOverflow(content_size_);

  if (ConstraintSpace().HasBlockFragmentation())
    FinalizeForFragmentation();

  return builder_->ToBoxFragment();
}

void NGBlockLayoutAlgorithm::LayoutInlineChildren(NGInlineNode* current_child) {
  // TODO(kojii): This logic does not handle when children are mix of
  // inline/block. We need to detect the case and setup appropriately; e.g.,
  // constraint space, margin collapsing, next siblings, etc.
  NGLineBuilder line_builder(current_child, space_for_current_child_);
  current_child->LayoutInline(space_for_current_child_, &line_builder);
  // TODO(kojii): The wrapper fragment should not be needed.
  NGFragmentBuilder wrapper_fragment_builder(NGPhysicalFragment::kFragmentBox,
                                             current_child->GetLayoutObject());
  line_builder.CreateFragments(&wrapper_fragment_builder);
  RefPtr<NGPhysicalBoxFragment> child_fragment =
      wrapper_fragment_builder.ToBoxFragment();
  line_builder.CopyFragmentDataToLayoutBlockFlow();
  FinishCurrentChildLayout(child_fragment.get());
  current_child_ = nullptr;
}

void NGBlockLayoutAlgorithm::FinishCurrentChildLayout(
    RefPtr<NGPhysicalBoxFragment> physical_fragment) {
  NGBoxFragment fragment(ConstraintSpace().WritingMode(),
                         physical_fragment.get());

  if (!physical_fragment->UnpositionedFloats().isEmpty())
    DCHECK(!builder_->BfcOffset()) << "Parent BFC offset shouldn't be set here";
  // Pull out unpositioned floats to the current fragment. This may needed if
  // for example the child fragment could not position its floats because it's
  // empty and therefore couldn't determine its position in space.
  builder_->MutableUnpositionedFloats().appendVector(
      physical_fragment->UnpositionedFloats());

  if (current_child_->Type() == NGLayoutInputNode::kLegacyBlock &&
      CurrentChildStyle().isFloating()) {
    NGFloatingObject* floating_object =
        new NGFloatingObject(physical_fragment.get(), space_for_current_child_,
                             toNGBlockNode(current_child_), CurrentChildStyle(),
                             curr_child_margins_);
    builder_->AddUnpositionedFloat(floating_object);
    // No need to postpone the positioning if we know the correct offset.
    if (builder_->BfcOffset()) {
      NGLogicalOffset origin_point = curr_bfc_offset_;
      // Adjust origin point to the margins of the last child.
      // Example: <div style="margin-bottom: 20px"><float></div>
      //          <div style="margin-bottom: 30px"></div>
      origin_point.block_offset += curr_margin_strut_.Sum();
      PositionPendingFloats(origin_point, builder_.get());
    }
    return;
  }

  // Determine the fragment's position in the parent space either by using
  // content_size_ or known fragment's BFC offset.
  WTF::Optional<NGLogicalOffset> bfc_offset;
  if (CurrentChildConstraintSpace().IsNewFormattingContext()) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    bfc_offset = curr_bfc_offset_;
  } else if (fragment.BfcOffset()) {
    // Fragment that knows its offset can be used to set parent's BFC position.
    curr_bfc_offset_.block_offset = fragment.BfcOffset().value().block_offset;
    bfc_offset = curr_bfc_offset_;
  }
  if (bfc_offset) {
    UpdateFragmentBfcOffset(curr_bfc_offset_, builder_.get());
    PositionPendingFloats(curr_bfc_offset_, builder_.get());
  }
  NGLogicalOffset logical_offset = CalculateLogicalOffset(bfc_offset);

  if (fragmentainer_mapper_)
    fragmentainer_mapper_->ToVisualOffset(logical_offset);
  else
    logical_offset.block_offset -= PreviousBreakOffset();

  // Update margin strut.
  curr_margin_strut_ = fragment.EndMarginStrut();
  curr_margin_strut_.Append(curr_child_margins_.block_end);

  content_size_ = fragment.BlockSize() + logical_offset.block_offset;
  max_inline_size_ =
      std::max(max_inline_size_, fragment.InlineSize() +
                                     curr_child_margins_.InlineSum() +
                                     border_and_padding_.InlineSum());

  builder_->AddChild(std::move(physical_fragment), logical_offset);
}

bool NGBlockLayoutAlgorithm::ProceedToNextUnfinishedSibling(
    NGPhysicalFragment* child_fragment) {
  DCHECK(current_child_);
  NGBlockNode* finished_child = toNGBlockNode(current_child_);
  current_child_ = current_child_->NextSibling();
  if (!ConstraintSpace().HasBlockFragmentation() && !fragmentainer_mapper_)
    return true;
  // If we're resuming layout after a fragmentainer break, we need to skip
  // siblings that we're done with. We may have been able to fully lay out some
  // node(s) preceding a node that we had to break inside (and therefore were
  // not able to fully lay out). This happens when we have parallel flows [1],
  // which are caused by floats, overflow, etc.
  //
  // [1] https://drafts.csswg.org/css-break/#parallel-flows
  if (CurrentBlockBreakToken()) {
    // TODO(layout-ng): Figure out if we need a better way to determine if the
    // node is finished. Maybe something to encode in a break token?
    // TODO(kojii): Handle inline children.
    while (current_child_ &&
           current_child_->Type() == NGLayoutInputNode::kLegacyBlock &&
           toNGBlockNode(current_child_)->IsLayoutFinished()) {
      current_child_ = current_child_->NextSibling();
    }
  }
  LayoutUnit break_offset = NextBreakOffset();
  bool is_out_of_space = content_size_ - PreviousBreakOffset() >= break_offset;
  if (!HasPendingBreakToken()) {
    bool child_broke = child_fragment->BreakToken();
    // This block needs to break if the child broke, or if we're out of space
    // and there's more content waiting to be laid out. Otherwise, just bail
    // now.
    if (!child_broke && (!is_out_of_space || !current_child_))
      return true;
    // Prepare a break token for this block, so that we know where to resume
    // when the time comes for that. We may not be able to abort layout of this
    // block right away, due to the posibility of parallel flows. We can only
    // abort when we're out of space, or when there are no siblings left to
    // process.
    NGBlockBreakToken* token;
    if (child_broke) {
      // The child we just laid out was the first one to break. So that is
      // where we need to resume.
      token = new NGBlockBreakToken(finished_child, break_offset);
    } else {
      // Resume layout at the next sibling that needs layout.
      DCHECK(current_child_);
      token =
          new NGBlockBreakToken(toNGBlockNode(current_child_), break_offset);
    }
    SetPendingBreakToken(token);
  }

  if (!fragmentainer_mapper_) {
    if (!is_out_of_space)
      return true;
    // We have run out of space in this flow, so there's no work left to do for
    // this block in this fragmentainer. We should finalize the fragment and get
    // back to the remaining content when laying out the next fragmentainer(s).
    return false;
  }

  if (is_out_of_space || !current_child_) {
    NGBlockBreakToken* token = fragmentainer_mapper_->Advance();
    DCHECK(token || !is_out_of_space);
    if (token) {
      break_token_ = token;
      content_size_ = token->BreakOffset();
      current_child_ = token->InputNode();
    }
  }
  return true;
}

void NGBlockLayoutAlgorithm::SetPendingBreakToken(NGBlockBreakToken* token) {
  if (fragmentainer_mapper_)
    fragmentainer_mapper_->SetBreakToken(token);
  else
    builder_->SetBreakToken(token);
}

bool NGBlockLayoutAlgorithm::HasPendingBreakToken() const {
  if (fragmentainer_mapper_)
    return fragmentainer_mapper_->HasBreakToken();
  return builder_->HasBreakToken();
}

void NGBlockLayoutAlgorithm::FinalizeForFragmentation() {
  LayoutUnit block_size =
      ComputeBlockSizeForFragment(ConstraintSpace(), Style(), content_size_);
  LayoutUnit previous_break_offset = PreviousBreakOffset();
  block_size -= previous_break_offset;
  block_size = std::max(LayoutUnit(), block_size);
  LayoutUnit space_left = ConstraintSpace().FragmentainerSpaceAvailable();
  DCHECK_GE(space_left, LayoutUnit());
  if (builder_->HasBreakToken()) {
    // A break token is ready, which means that we're going to break
    // before or inside a block-level child.
    builder_->SetBlockSize(std::min(space_left, block_size));
    builder_->SetBlockOverflow(space_left);
    return;
  }
  if (block_size > space_left) {
    // Need a break inside this block.
    builder_->SetBreakToken(new NGBlockBreakToken(nullptr, NextBreakOffset()));
    builder_->SetBlockSize(space_left);
    builder_->SetBlockOverflow(space_left);
    return;
  }
  // The end of the block fits in the current fragmentainer.
  builder_->SetBlockSize(block_size);
  builder_->SetBlockOverflow(content_size_ - previous_break_offset);
}

NGBlockBreakToken* NGBlockLayoutAlgorithm::CurrentBlockBreakToken() const {
  NGBreakToken* token = break_token_;
  if (!token || token->Type() != NGBreakToken::kBlockBreakToken)
    return nullptr;
  return toNGBlockBreakToken(token);
}

LayoutUnit NGBlockLayoutAlgorithm::PreviousBreakOffset() const {
  const NGBlockBreakToken* token = CurrentBlockBreakToken();
  return token ? token->BreakOffset() : LayoutUnit();
}

LayoutUnit NGBlockLayoutAlgorithm::NextBreakOffset() const {
  if (fragmentainer_mapper_)
    return fragmentainer_mapper_->NextBreakOffset();
  DCHECK(ConstraintSpace().HasBlockFragmentation());
  return PreviousBreakOffset() +
         ConstraintSpace().FragmentainerSpaceAvailable();
}

LayoutUnit NGBlockLayoutAlgorithm::SpaceAvailableForCurrentChild() const {
  LayoutUnit space_left;
  if (fragmentainer_mapper_)
    space_left = fragmentainer_mapper_->BlockSize();
  else if (ConstraintSpace().HasBlockFragmentation())
    space_left = ConstraintSpace().FragmentainerSpaceAvailable();
  else
    return NGSizeIndefinite;
  space_left -= BorderEdgeForCurrentChild() - PreviousBreakOffset();
  return space_left;
}

NGBoxStrut NGBlockLayoutAlgorithm::CalculateMargins(
    const NGConstraintSpace& space,
    const ComputedStyle& style) {
  WTF::Optional<MinAndMaxContentSizes> sizes;
  if (NeedMinAndMaxContentSizes(space, style)) {
    // TODO(ikilpatrick): Change ComputeMinAndMaxContentSizes to return
    // MinAndMaxContentSizes.
    sizes = toNGBlockNode(current_child_)->ComputeMinAndMaxContentSizes();
  }
  LayoutUnit child_inline_size =
      ComputeInlineSizeForFragment(space, style, sizes);
  NGBoxStrut margins =
      ComputeMargins(space, style, space.WritingMode(), space.Direction());
  if (!style.isFloating()) {
    ApplyAutoMargins(space, style, child_inline_size, &margins);
  }
  return margins;
}

NGConstraintSpace*
NGBlockLayoutAlgorithm::CreateConstraintSpaceForCurrentChild() {
  DCHECK(current_child_);
  if (current_child_->Type() == NGLayoutInputNode::kLegacyInline) {
    // TODO(kojii): Setup space_builder_ appropriately for inline child.
    return space_builder_->ToConstraintSpace();
    // Calculate margins in parent's writing mode.
  }
  curr_child_margins_ = CalculateMargins(*space_builder_->ToConstraintSpace(),
                                         CurrentChildStyle());

  const ComputedStyle& current_child_style = CurrentChildStyle();
  bool is_new_bfc = IsNewFormattingContextForInFlowBlockLevelChild(
      ConstraintSpace(), current_child_style);
  space_builder_->SetIsNewFormattingContext(is_new_bfc)
      .SetIsShrinkToFit(
          ShouldShrinkToFit(ConstraintSpace(), CurrentChildStyle()))
      .SetWritingMode(
          FromPlatformWritingMode(current_child_style.getWritingMode()))
      .SetTextDirection(current_child_style.direction());
  LayoutUnit space_available = SpaceAvailableForCurrentChild();
  space_builder_->SetFragmentainerSpaceAvailable(space_available);

  // Clearance :
  // - Collapse margins
  // - Update curr_bfc_offset and parent BFC offset if needed.
  // - Position all pending floats as position is known now.
  // TODO(glebl): Fix the use case with clear: left and an intruding right.
  // https://software.hixie.ch/utilities/js/live-dom-viewer/saved/4847
  if (current_child_style.clear() != EClear::kNone) {
    curr_bfc_offset_.block_offset += curr_margin_strut_.Sum();
    UpdateFragmentBfcOffset(curr_bfc_offset_, builder_.get());
    // Only collapse margins if it's an adjoining block with clearance.
    if (!content_size_) {
      curr_margin_strut_ = NGMarginStrut();
      curr_child_margins_.block_start = LayoutUnit();
    }
    PositionPendingFloats(curr_bfc_offset_, builder_.get());
    AdjustToClearance(constraint_space_->Exclusions(), current_child_style,
                      builder_->BfcOffset().value(), &content_size_);
  }

  // Append the current margin strut with child's block start margin.
  // Non empty border/padding use cases are handled inside of the child's
  // layout.
  curr_margin_strut_.Append(curr_child_margins_.block_start);
  space_builder_->SetMarginStrut(curr_margin_strut_);

  // Set estimated BFC offset to the next child's constraint space.
  curr_bfc_offset_ = builder_->BfcOffset() ? builder_->BfcOffset().value()
                                           : ConstraintSpace().BfcOffset();
  curr_bfc_offset_.block_offset += content_size_;
  curr_bfc_offset_.inline_offset += border_and_padding_.inline_start;
  if (ConstraintSpace().IsNewFormattingContext()) {
    curr_bfc_offset_.inline_offset += curr_child_margins_.inline_start;
  }
  space_builder_->SetBfcOffset(curr_bfc_offset_);

  return space_builder_->ToConstraintSpace();
}

}  // namespace blink
