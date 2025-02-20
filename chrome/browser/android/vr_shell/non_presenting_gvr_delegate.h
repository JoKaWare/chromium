// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_NON_PRESENTING_GVR_DELEGATE_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_NON_PRESENTING_GVR_DELEGATE_H_

#include <jni.h>

#include "base/cancelable_callback.h"
#include "base/macros.h"
#include "device/vr/android/gvr/gvr_delegate.h"
#include "device/vr/vr_service.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "third_party/gvr-android-sdk/src/libraries/headers/vr/gvr/capi/include/gvr_types.h"

namespace vr_shell {

// A non presenting delegate for magic window mode.
class NonPresentingGvrDelegate : public device::GvrDelegate,
                                 public device::mojom::VRVSyncProvider {
 public:
  explicit NonPresentingGvrDelegate(gvr_context* context);

  ~NonPresentingGvrDelegate() override;

  // GvrDelegate implementation
  void SetWebVRSecureOrigin(bool secure_origin) override {}
  void SubmitWebVRFrame() override {}
  void UpdateWebVRTextureBounds(int16_t frame_index,
                                const gvr::Rectf& left_bounds,
                                const gvr::Rectf& right_bounds) override {}
  void OnVRVsyncProviderRequest(
      device::mojom::VRVSyncProviderRequest request) override;
  void UpdateVSyncInterval(long timebase_nanos,
                           double interval_seconds) override;
  bool SupportsPresentation() override;
  void ResetPose() override;
  void CreateVRDisplayInfo(
      const base::Callback<void(device::mojom::VRDisplayInfoPtr)>& callback,
      uint32_t device_id) override;

  void Pause();
  void Resume();
  device::mojom::VRVSyncProviderRequest OnSwitchToPresentingDelegate();

 private:
  void StopVSyncLoop();
  void StartVSyncLoop();
  void OnVSync();
  void SendVSync(base::TimeDelta time, const GetVSyncCallback& callback);

  // VRVSyncProvider implementation
  void GetVSync(const GetVSyncCallback& callback) override;

  std::unique_ptr<gvr::GvrApi> gvr_api_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  base::CancelableClosure vsync_task_;
  base::TimeTicks vsync_timebase_;
  base::TimeDelta vsync_interval_;

  // Whether the vsync loop is paused, but not stopped.
  bool vsync_paused_ = false;
  base::TimeDelta pending_time_;
  bool pending_vsync_ = false;
  GetVSyncCallback callback_;
  mojo::Binding<device::mojom::VRVSyncProvider> binding_;
  base::WeakPtrFactory<NonPresentingGvrDelegate> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NonPresentingGvrDelegate);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_NON_PRESENTING_GVR_DELEGATE_H_
