// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/media/service/cast_mojo_media_client.h"

#include "base/memory/ptr_util.h"
#include "chromecast/media/service/cast_renderer.h"
#include "media/base/audio_renderer_sink.h"
#include "media/base/cdm_factory.h"
#include "media/base/media_log.h"
#include "media/base/renderer_factory.h"

namespace chromecast {
namespace media {

namespace {
// CastRenderer does not use a ::media::AudioRendererSink.
// CastAudioRendererSink is only used to hold audio-device-id.
class CastAudioRendererSink : public ::media::AudioRendererSink {
 public:
  explicit CastAudioRendererSink(const std::string& device_id)
      : device_id_(device_id) {}

  // ::media::AudioRendererSink implementation.
  void Initialize(const ::media::AudioParameters& params,
                  RenderCallback* callback) final {
    NOTREACHED();
  }
  void Start() final { NOTREACHED(); }
  void Stop() final { NOTREACHED(); }
  void Pause() final { NOTREACHED(); }
  void Play() final { NOTREACHED(); }
  bool SetVolume(double volume) final {
    NOTREACHED();
    return false;
  }
  ::media::OutputDeviceInfo GetOutputDeviceInfo() final {
    return ::media::OutputDeviceInfo(device_id_,
                                     ::media::OUTPUT_DEVICE_STATUS_OK,
                                     ::media::AudioParameters());
  }
  bool CurrentThreadIsRenderingThread() final {
    NOTREACHED();
    return false;
  }

 private:
  ~CastAudioRendererSink() final {}

  std::string device_id_;
  DISALLOW_COPY_AND_ASSIGN(CastAudioRendererSink);
};

class CastRendererFactory : public ::media::RendererFactory {
 public:
  CastRendererFactory(const CreateMediaPipelineBackendCB& create_backend_cb,
                      const scoped_refptr<::media::MediaLog>& media_log,
                      VideoModeSwitcher* video_mode_switcher,
                      VideoResolutionPolicy* video_resolution_policy,
                      MediaResourceTracker* media_resource_tracker)
      : create_backend_cb_(create_backend_cb),
        media_log_(media_log),
        video_mode_switcher_(video_mode_switcher),
        video_resolution_policy_(video_resolution_policy),
        media_resource_tracker_(media_resource_tracker) {}
  ~CastRendererFactory() final {}

  std::unique_ptr<::media::Renderer> CreateRenderer(
      const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
      const scoped_refptr<base::TaskRunner>& worker_task_runner,
      ::media::AudioRendererSink* audio_renderer_sink,
      ::media::VideoRendererSink* video_renderer_sink,
      const ::media::RequestSurfaceCB& request_surface_cb) final {
    DCHECK(audio_renderer_sink);
    DCHECK(!video_renderer_sink);
    return base::MakeUnique<CastRenderer>(
        create_backend_cb_, media_task_runner,
        audio_renderer_sink->GetOutputDeviceInfo().device_id(),
        video_mode_switcher_, video_resolution_policy_,
        media_resource_tracker_);
  }

 private:
  const CreateMediaPipelineBackendCB create_backend_cb_;
  scoped_refptr<::media::MediaLog> media_log_;
  VideoModeSwitcher* video_mode_switcher_;
  VideoResolutionPolicy* video_resolution_policy_;
  MediaResourceTracker* media_resource_tracker_;
  DISALLOW_COPY_AND_ASSIGN(CastRendererFactory);
};
}  // namespace

CastMojoMediaClient::CastMojoMediaClient(
    const CreateMediaPipelineBackendCB& create_backend_cb,
    const CreateCdmFactoryCB& create_cdm_factory_cb,
    VideoModeSwitcher* video_mode_switcher,
    VideoResolutionPolicy* video_resolution_policy,
    MediaResourceTracker* media_resource_tracker)
    : connector_(nullptr),
      create_backend_cb_(create_backend_cb),
      create_cdm_factory_cb_(create_cdm_factory_cb),
      video_mode_switcher_(video_mode_switcher),
      video_resolution_policy_(video_resolution_policy),
      media_resource_tracker_(media_resource_tracker) {}

CastMojoMediaClient::~CastMojoMediaClient() {}

void CastMojoMediaClient::Initialize(service_manager::Connector* connector) {
  DCHECK(!connector_);
  DCHECK(connector);
  connector_ = connector;
}

scoped_refptr<::media::AudioRendererSink>
CastMojoMediaClient::CreateAudioRendererSink(
    const std::string& audio_device_id) {
  return new CastAudioRendererSink(audio_device_id);
}

std::unique_ptr<::media::RendererFactory>
CastMojoMediaClient::CreateRendererFactory(
    const scoped_refptr<::media::MediaLog>& media_log) {
  return base::MakeUnique<CastRendererFactory>(
      create_backend_cb_, media_log, video_mode_switcher_,
      video_resolution_policy_, media_resource_tracker_);
}

std::unique_ptr<::media::CdmFactory> CastMojoMediaClient::CreateCdmFactory(
    service_manager::mojom::InterfaceProvider* /* host_interfaces */) {
  return create_cdm_factory_cb_.Run();
}

}  // namespace media
}  // namespace chromecast
