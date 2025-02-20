// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.ui.base;

import android.content.ClipData;
import android.content.Intent;
import android.graphics.Bitmap;
import android.os.Build;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout.LayoutParams;
import android.widget.ImageView;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

import java.net.URISyntaxException;

/**
 * Class to acquire, position, and remove anchor views from the implementing View.
 */
@JNINamespace("ui")
public abstract class ViewAndroidDelegate {

    private static final String TAG = "ViewAndroidDelegate";

    // TODO(hush): use View#DRAG_FLAG_GLOBAL when Chromium starts to build with API 24.
    private static final int DRAG_FLAG_GLOBAL = 1 << 8;

    private static final String GEO_SCHEME = "geo";
    private static final String TEL_SCHEME = "tel";
    private static final String MAILTO_SCHEME = "mailto";

    /**
     * @return An anchor view that can be used to anchor decoration views like Autofill popup.
     */
    @CalledByNative
    public View acquireView() {
        ViewGroup containerView = getContainerView();
        if (containerView == null || containerView.getParent() == null) return null;
        View anchorView = new View(containerView.getContext());
        containerView.addView(anchorView);
        return anchorView;
    }

    /**
     * Release given anchor view.
     * @param anchorView The anchor view that needs to be released.
     */
    @CalledByNative
    public void removeView(View anchorView) {
        ViewGroup containerView = getContainerView();
        if (containerView == null) return;
        containerView.removeView(anchorView);
    }

    /**
     * Set the anchor view to specified position and size (all units in dp).
     * @param view The anchor view that needs to be positioned.
     * @param x X coordinate of the top left corner of the anchor view.
     * @param y Y coordinate of the top left corner of the anchor view.
     * @param width The width of the anchor view.
     * @param height The height of the anchor view.
     */
    @CalledByNative
    public void setViewPosition(View view, float x, float y,
            float width, float height, float scale, int leftMargin, int topMargin) {
        ViewGroup containerView = getContainerView();
        if (containerView == null) return;

        int scaledWidth = Math.round(width * scale);
        int scaledHeight = Math.round(height * scale);
        int startMargin;

        if (ApiCompatibilityUtils.isLayoutRtl(containerView)) {
            startMargin = containerView.getMeasuredWidth() - Math.round((width + x) * scale);
        } else {
            startMargin = leftMargin;
        }
        if (scaledWidth + startMargin > containerView.getWidth()) {
            scaledWidth = containerView.getWidth() - startMargin;
        }
        LayoutParams lp = new LayoutParams(scaledWidth, scaledHeight);
        ApiCompatibilityUtils.setMarginStart(lp, startMargin);
        lp.topMargin = topMargin;
        view.setLayoutParams(lp);
    }

    /**
     * Drag the text out of current view.
     * @param text The dragged text.
     * @param shadowImage The shadow image for the dragged text.
     */
    @SuppressWarnings("deprecation")
    // TODO(hush): uncomment below when we build with API 24.
    // @TargetApi(Build.VERSION_CODES.N)
    @CalledByNative
    private boolean startDragAndDrop(String text, Bitmap shadowImage) {
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.M) return false;

        ViewGroup containerView = getContainerView();
        if (containerView == null) return false;

        ImageView imageView = new ImageView(containerView.getContext());
        imageView.setImageBitmap(shadowImage);
        imageView.layout(0, 0, shadowImage.getWidth(), shadowImage.getHeight());

        // TODO(hush): use View#startDragAndDrop when Chromium starts to build with API 24.
        return containerView.startDrag(ClipData.newPlainText(null, text),
                new View.DragShadowBuilder(imageView), null, DRAG_FLAG_GLOBAL);
    }

    /**
     * Called whenever the background color of the page changes as notified by Blink.
     * @param color The new ARGB color of the page background.
     */
    @CalledByNative
    public void onBackgroundColorChanged(int color) {}

    /**
     * Notify the client of the position of the top controls.
     * @param topControlsOffsetY The Y offset of the top controls in physical pixels.
     * @param topContentOffsetY The Y offset of the content in physical pixels.
     */
    @CalledByNative
    public void onTopControlsChanged(float topControlsOffsetY, float topContentOffsetY) {}

    /**
     * Notify the client of the position of the bottom controls.
     * @param bottomControlsOffsetY The Y offset of the bottom controls in physical pixels.
     * @param bottomContentOffsetY The Y offset of the content in physical pixels.
     */
    @CalledByNative
    public void onBottomControlsChanged(float bottomControlsOffsetY, float bottomContentOffsetY) {}

    /**
     * Called when a new content intent is requested to be started.
     * Invokes {@link #startContentIntent(Intent, String, boolean)} only if the parsed
     * intent is valid and the scheme is acceptable.
     */
    @CalledByNative
    private void onStartContentIntent(String intentUrl, boolean isMainFrame) {
        Intent intent;
        try {
            intent = Intent.parseUri(intentUrl, Intent.URI_INTENT_SCHEME);
        } catch (URISyntaxException e) {
            Log.d(TAG, "Bad URI %s", intentUrl, e);
            return;
        }
        String scheme = intent.getScheme();
        if (!(GEO_SCHEME.equals(scheme) || TEL_SCHEME.equals(scheme)
                || MAILTO_SCHEME.equals(scheme))) {
            Log.d(TAG, "Invalid scheme for URI %s", intentUrl);
            return;
        }
        startContentIntent(intent, intentUrl, isMainFrame);
    }

    /**
     * Start a new content intent.
     */
    public void startContentIntent(Intent intent, String intentUrl, boolean isMainFrame) {}

    /**
     * @return container view that the anchor views are added to. May be null.
     */
    @CalledByNative
    public abstract ViewGroup getContainerView();

    /**
     * Create and return a basic implementation of {@link ViewAndroidDelegate} where
     * the container view is not allowed to be changed after initialization.
     * @param containerView {@link ViewGroup} to be used as a container view.
     * @return a new instance of {@link ViewAndroidDelegate}.
     */
    public static ViewAndroidDelegate createBasicDelegate(ViewGroup containerView) {
        return new ViewAndroidDelegate() {
            private ViewGroup mContainerView;

            private ViewAndroidDelegate init(ViewGroup containerView) {
                mContainerView = containerView;
                return this;
            }

            @Override
            public ViewGroup getContainerView() {
                return mContainerView;
            }
        }.init(containerView);
    }
}
