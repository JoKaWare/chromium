// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.support.annotation.CallSuper;
import android.support.annotation.Nullable;

import org.chromium.chrome.browser.ntp.cards.NewTabPageViewHolder.PartialBindCallback;

/**
 * A node in the tree that has a parent and can notify it about changes.
 *
 * This class mostly serves as a convenience base class for implementations of {@link TreeNode}.
 */
public abstract class ChildNode implements TreeNode {
    private NodeParent mParent;
    private int mNumItems = 0;

    @Override
    public final void setParent(NodeParent parent) {
        assert mParent == null;
        assert parent != null;
        mParent = parent;
    }

    @Override
    @CallSuper
    public void detach() {
        assert mParent != null;
        mParent = null;
    }

    @Override
    public final int getItemCount() {
        assert mNumItems == getItemCountForDebugging();
        return mNumItems;
    }

    protected void notifyItemRangeChanged(
            int index, int count, @Nullable PartialBindCallback callback) {
        if (mParent != null) mParent.onItemRangeChanged(this, index, count, callback);
    }

    protected void notifyItemRangeChanged(int index, int count) {
        notifyItemRangeChanged(index, count, null);
    }

    protected void notifyItemRangeInserted(int index, int count) {
        mNumItems += count;
        assert mNumItems == getItemCountForDebugging();
        if (mParent != null) mParent.onItemRangeInserted(this, index, count);
    }

    protected void notifyItemRangeRemoved(int index, int count) {
        mNumItems -= count;
        assert mNumItems == getItemCountForDebugging();
        if (mParent != null) mParent.onItemRangeRemoved(this, index, count);
    }

    protected void notifyItemChanged(int index, @Nullable PartialBindCallback callback) {
        notifyItemRangeChanged(index, 1, callback);
    }

    protected void notifyItemChanged(int index) {
        notifyItemRangeChanged(index, 1);
    }

    protected void notifyItemInserted(int index) {
        notifyItemRangeInserted(index, 1);
    }

    protected void notifyItemRemoved(int index) {
        notifyItemRangeRemoved(index, 1);
    }

    protected void checkIndex(int position) {
        if (position < 0 || position >= getItemCount()) {
            throw new IndexOutOfBoundsException(position + "/" + getItemCount());
        }
    }

    /**
     * @return The actual (non-cached) number of items under this node. The implementation of this
     * method should not rely on {@link #getItemCount}, but instead derive the number of items
     * directly from the underlying data model. Any time this value changes, an appropriate
     * notification should be sent.
     */
    protected abstract int getItemCountForDebugging();
}
