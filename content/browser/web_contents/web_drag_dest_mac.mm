// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "content/browser/web_contents/web_drag_dest_mac.h"

#import <Carbon/Carbon.h>

#include "base/strings/sys_string_conversions.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_drag_dest_delegate.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/drop_data.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"
#include "ui/base/clipboard/custom_data_helper.h"
#include "ui/base/cocoa/cocoa_base_utils.h"
#import "ui/base/dragdrop/cocoa_dnd_util.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/point.h"

using blink::WebDragOperationsMask;
using content::DropData;
using content::OpenURLParams;
using content::Referrer;
using content::WebContentsImpl;

namespace {

int GetModifierFlags() {
  int modifier_state = 0;
  UInt32 currentModifiers = GetCurrentKeyModifiers();
  if (currentModifiers & ::shiftKey)
    modifier_state |= blink::WebInputEvent::ShiftKey;
  if (currentModifiers & ::controlKey)
    modifier_state |= blink::WebInputEvent::ControlKey;
  if (currentModifiers & ::optionKey)
    modifier_state |= blink::WebInputEvent::AltKey;
  if (currentModifiers & ::cmdKey)
      modifier_state |= blink::WebInputEvent::MetaKey;

  // The return value of 1 << 0 corresponds to the left mouse button,
  // 1 << 1 corresponds to the right mouse button,
  // 1 << n, n >= 2 correspond to other mouse buttons.
  NSUInteger pressedButtons = [NSEvent pressedMouseButtons];

  if (pressedButtons & (1 << 0))
      modifier_state |= blink::WebInputEvent::LeftButtonDown;
  if (pressedButtons & (1 << 1))
      modifier_state |= blink::WebInputEvent::RightButtonDown;
  if (pressedButtons & (1 << 2))
      modifier_state |= blink::WebInputEvent::MiddleButtonDown;

  return modifier_state;
}

content::GlobalRoutingID GetRenderViewHostID(content::RenderViewHost* rvh) {
  return content::GlobalRoutingID(rvh->GetProcess()->GetID(),
                                  rvh->GetRoutingID());
}

}  // namespace

@implementation WebDragDest

// |contents| is the WebContentsImpl representing this tab, used to communicate
// drag&drop messages to WebCore and handle navigation on a successful drop
// (if necessary).
- (id)initWithWebContentsImpl:(WebContentsImpl*)contents {
  if ((self = [super init])) {
    webContents_ = contents;
    canceled_ = false;
    dragStartProcessID_ = content::ChildProcessHost::kInvalidUniqueID;
    dragStartViewID_ = content::GlobalRoutingID(
        content::ChildProcessHost::kInvalidUniqueID, MSG_ROUTING_NONE);
  }
  return self;
}

- (DropData*)currentDropData {
  return dropData_.get();
}

- (void)setDragDelegate:(content::WebDragDestDelegate*)delegate {
  delegate_ = delegate;
}

// Call to set whether or not we should allow the drop. Takes effect the
// next time |-draggingUpdated:| is called.
- (void)setCurrentOperation:(NSDragOperation)operation {
  currentOperation_ = operation;
}

// Given a point in window coordinates and a view in that window, return a
// flipped point in the coordinate system of |view|.
- (NSPoint)flipWindowPointToView:(const NSPoint&)windowPoint
                            view:(NSView*)view {
  DCHECK(view);
  NSPoint viewPoint =  [view convertPoint:windowPoint fromView:nil];
  NSRect viewFrame = [view frame];
  viewPoint.y = viewFrame.size.height - viewPoint.y;
  return viewPoint;
}

// Given a point in window coordinates and a view in that window, return a
// flipped point in screen coordinates.
- (NSPoint)flipWindowPointToScreen:(const NSPoint&)windowPoint
                              view:(NSView*)view {
  DCHECK(view);
  NSPoint screenPoint =
      ui::ConvertPointFromWindowToScreen([view window], windowPoint);
  NSScreen* screen = [[view window] screen];
  NSRect screenFrame = [screen frame];
  screenPoint.y = screenFrame.size.height - screenPoint.y;
  return screenPoint;
}

// Return YES if the drop site only allows drops that would navigate.  If this
// is the case, we don't want to pass messages to the renderer because there's
// really no point (i.e., there's nothing that cares about the mouse position or
// entering and exiting).  One example is an interstitial page (e.g., safe
// browsing warning).
- (BOOL)onlyAllowsNavigation {
  return webContents_->ShowingInterstitialPage();
}

// Messages to send during the tracking of a drag, usually upon receiving
// calls from the view system. Communicates the drag messages to WebCore.

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info
                              view:(NSView*)view {
  // Save off the RVH so we can tell if it changes during a drag. If it does,
  // we need to send a new enter message in draggingUpdated:.
  currentRVH_ = webContents_->GetRenderViewHost();

  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates. Both need to be flipped.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  gfx::Point transformedPt;
  if (!webContents_->GetRenderWidgetHostView()) {
    // TODO(ekaramad, paulmeyer): Find a better way than toggling |canceled_|.
    // This could happen when the renderer process for the top-level RWH crashes
    // (see https://crbug.com/670645).
    canceled_ = true;
    return NSDragOperationNone;
  }

  content::RenderWidgetHostImpl* targetRWH =
      [self GetRenderWidgetHostAtPoint:viewPoint transformedPt:&transformedPt];
  if (![self isValidDragTarget:targetRWH])
    return NSDragOperationNone;

  currentRWHForDrag_ = targetRWH->GetWeakPtr();

  // Fill out a DropData from pasteboard.
  std::unique_ptr<DropData> dropData;
  dropData.reset(new DropData());
  [self populateDropData:dropData.get()
             fromPasteboard:[info draggingPasteboard]];
  // TODO(paulmeyer): Data may be pulled from the pasteboard multiple times per
  // drag. Ideally, this should only be done once, and filtered as needed.
  currentRWHForDrag_->FilterDropData(dropData.get());

  NSDragOperation mask = [info draggingSourceOperationMask];

  // Give the delegate an opportunity to cancel the drag.
  canceled_ = !webContents_->GetDelegate()->CanDragEnter(
      webContents_,
      *dropData,
      static_cast<WebDragOperationsMask>(mask));
  if (canceled_)
    return NSDragOperationNone;

  if ([self onlyAllowsNavigation]) {
    if ([[info draggingPasteboard] containsURLDataConvertingTextToURL:YES])
      return NSDragOperationCopy;
    return NSDragOperationNone;
  }

  if (delegate_) {
    delegate_->DragInitialize(webContents_);
    delegate_->OnDragEnter();
  }

  dropData_.swap(dropData);

  currentRWHForDrag_->DragTargetDragEnter(
      *dropData_, transformedPt, gfx::Point(screenPoint.x, screenPoint.y),
      static_cast<WebDragOperationsMask>(mask), GetModifierFlags());

  // We won't know the true operation (whether the drag is allowed) until we
  // hear back from the renderer. For now, be optimistic:
  currentOperation_ = NSDragOperationCopy;
  return currentOperation_;
}

- (void)draggingExited:(id<NSDraggingInfo>)info {
  DCHECK(currentRVH_);
  if (currentRVH_ != webContents_->GetRenderViewHost())
    return;

  if (canceled_)
    return;

  if ([self onlyAllowsNavigation])
    return;

  if (delegate_)
    delegate_->OnDragLeave();

  if (currentRWHForDrag_) {
    currentRWHForDrag_->DragTargetDragLeave(gfx::Point(), gfx::Point());
    currentRWHForDrag_.reset();
  }
  dropData_.reset();
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info view:(NSView*)view {
  if (canceled_) {
    // TODO(ekaramad,paulmeyer): We probably shouldn't be checking for
    // |canceled_| twice in this method.
    return NSDragOperationNone;
  }

  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates. Both need to be flipped.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  gfx::Point transformedPt;
  content::RenderWidgetHostImpl* targetRWH =
      [self GetRenderWidgetHostAtPoint:viewPoint transformedPt:&transformedPt];

  if (![self isValidDragTarget:targetRWH])
    return NSDragOperationNone;

  // TODO(paulmeyer): The dragging delegates may now by invoked multiple times
  // per drag, even without the drag ever leaving the window.
  if (targetRWH != currentRWHForDrag_.get()) {
    if (currentRWHForDrag_) {
      gfx::Point transformedLeavePoint = gfx::Point(viewPoint.x, viewPoint.y);
      gfx::Point transformedScreenPoint =
          gfx::Point(screenPoint.x, screenPoint.y);
      content::RenderWidgetHostViewBase* rootView =
          static_cast<content::RenderWidgetHostViewBase*>(
              webContents_->GetRenderWidgetHostView());
      content::RenderWidgetHostViewBase* currentDragView =
          static_cast<content::RenderWidgetHostViewBase*>(
              currentRWHForDrag_->GetView());
      rootView->TransformPointToCoordSpaceForView(
          transformedLeavePoint, currentDragView, &transformedLeavePoint);
      rootView->TransformPointToCoordSpaceForView(
          transformedScreenPoint, currentDragView, &transformedScreenPoint);
      currentRWHForDrag_->DragTargetDragLeave(transformedLeavePoint,
                                              transformedScreenPoint);
    }
    [self draggingEntered:info view:view];
  }

  if (canceled_)
    return NSDragOperationNone;

  if ([self onlyAllowsNavigation]) {
    if ([[info draggingPasteboard] containsURLDataConvertingTextToURL:YES])
      return NSDragOperationCopy;
    return NSDragOperationNone;
  }

  NSDragOperation mask = [info draggingSourceOperationMask];
  targetRWH->DragTargetDragOver(
      transformedPt, gfx::Point(screenPoint.x, screenPoint.y),
      static_cast<WebDragOperationsMask>(mask), GetModifierFlags());

  if (delegate_)
    delegate_->OnDragOver();

  return currentOperation_;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
                              view:(NSView*)view {
  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates. Both need to be flipped.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  gfx::Point transformedPt;
  content::RenderWidgetHostImpl* targetRWH =
      [self GetRenderWidgetHostAtPoint:viewPoint transformedPt:&transformedPt];

  if (![self isValidDragTarget:targetRWH])
    return NO;

  if (targetRWH != currentRWHForDrag_.get()) {
    if (currentRWHForDrag_)
      currentRWHForDrag_->DragTargetDragLeave(
          transformedPt, gfx::Point(screenPoint.x, screenPoint.y));
    [self draggingEntered:info view:view];
  }

  // Check if we only allow navigation and navigate to a url on the pasteboard.
  if ([self onlyAllowsNavigation]) {
    NSPasteboard* pboard = [info draggingPasteboard];
    if ([pboard containsURLDataConvertingTextToURL:YES]) {
      GURL url;
      ui::PopulateURLAndTitleFromPasteboard(&url, NULL, pboard, YES);
      webContents_->OpenURL(
          OpenURLParams(url, Referrer(), WindowOpenDisposition::CURRENT_TAB,
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK, false));
      return YES;
    } else {
      return NO;
    }
  }

  if (delegate_)
    delegate_->OnDrop();

  currentRVH_ = NULL;

  targetRWH->DragTargetDrop(*dropData_, transformedPt,
                            gfx::Point(screenPoint.x, screenPoint.y),
                            GetModifierFlags());

  dropData_.reset();

  return YES;
}

- (content::RenderWidgetHostImpl*)
GetRenderWidgetHostAtPoint:(const NSPoint&)viewPoint
             transformedPt:(gfx::Point*)transformedPt {
  return webContents_->GetInputEventRouter()->GetRenderWidgetHostAtPoint(
      webContents_->GetRenderViewHost()->GetWidget()->GetView(),
      gfx::Point(viewPoint.x, viewPoint.y), transformedPt);
}

- (void)setDragStartTrackersForProcess:(int)processID {
  dragStartProcessID_ = processID;
  dragStartViewID_ = GetRenderViewHostID(webContents_->GetRenderViewHost());
}

- (bool)isValidDragTarget:(content::RenderWidgetHostImpl*)targetRWH {
  return targetRWH->GetProcess()->GetID() == dragStartProcessID_ ||
         GetRenderViewHostID(webContents_->GetRenderViewHost()) !=
             dragStartViewID_;
}

// Given |data|, which should not be nil, fill it in using the contents of the
// given pasteboard. The types handled by this method should be kept in sync
// with [WebContentsViewCocoa registerDragTypes].
- (void)populateDropData:(DropData*)data
          fromPasteboard:(NSPasteboard*)pboard {
  DCHECK(data);
  DCHECK(pboard);
  NSArray* types = [pboard types];

  data->did_originate_from_renderer =
      [types containsObject:ui::kChromeDragDummyPboardType];

  // Get URL if possible. To avoid exposing file system paths to web content,
  // filenames in the drag are not converted to file URLs.
  ui::PopulateURLAndTitleFromPasteboard(&data->url,
                                        &data->url_title,
                                        pboard,
                                        NO);

  // Get plain text.
  if ([types containsObject:NSStringPboardType]) {
    data->text = base::NullableString16(
        base::SysNSStringToUTF16([pboard stringForType:NSStringPboardType]),
        false);
  }

  // Get HTML. If there's no HTML, try RTF.
  if ([types containsObject:NSHTMLPboardType]) {
    NSString* html = [pboard stringForType:NSHTMLPboardType];
    data->html = base::NullableString16(base::SysNSStringToUTF16(html), false);
  } else if ([types containsObject:ui::kChromeDragImageHTMLPboardType]) {
    NSString* html = [pboard stringForType:ui::kChromeDragImageHTMLPboardType];
    data->html = base::NullableString16(base::SysNSStringToUTF16(html), false);
  } else if ([types containsObject:NSRTFPboardType]) {
    NSString* html = [pboard htmlFromRtf];
    data->html = base::NullableString16(base::SysNSStringToUTF16(html), false);
  }

  // Get files.
  if ([types containsObject:NSFilenamesPboardType]) {
    NSArray* files = [pboard propertyListForType:NSFilenamesPboardType];
    if ([files isKindOfClass:[NSArray class]] && [files count]) {
      for (NSUInteger i = 0; i < [files count]; i++) {
        NSString* filename = [files objectAtIndex:i];
        BOOL exists = [[NSFileManager defaultManager]
                           fileExistsAtPath:filename];
        if (exists) {
          data->filenames.push_back(ui::FileInfo(
              base::FilePath::FromUTF8Unsafe(base::SysNSStringToUTF8(filename)),
              base::FilePath()));
        }
      }
    }
  }

  // TODO(pinkerton): Get file contents. http://crbug.com/34661

  // Get custom MIME data.
  if ([types containsObject:ui::kWebCustomDataPboardType]) {
    NSData* customData = [pboard dataForType:ui::kWebCustomDataPboardType];
    ui::ReadCustomDataIntoMap([customData bytes],
                              [customData length],
                              &data->custom_data);
  }
}

@end
