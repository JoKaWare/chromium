// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/image_decoder.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/common/service_manager_connection.h"
#include "ipc/ipc_channel.h"
#include "services/image_decoder/public/cpp/decode.h"
#include "services/service_manager/public/cpp/connector.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace {

const int64_t kMaxImageSizeInBytes =
    static_cast<int64_t>(IPC::Channel::kMaximumMessageSize);

// Note that this is always called on the thread which initiated the
// corresponding image_decoder::Decode request.
void OnDecodeImageDone(
    base::Callback<void(int)> fail_callback,
    base::Callback<void(const SkBitmap&, int)> success_callback,
    int request_id,
    const SkBitmap& image) {
  if (!image.isNull() && !image.empty())
    success_callback.Run(image, request_id);
  else
    fail_callback.Run(request_id);
}

void BindToBrowserConnector(service_manager::mojom::ConnectorRequest request) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&BindToBrowserConnector, base::Passed(&request)));
    return;
  }

  content::ServiceManagerConnection::GetForProcess()->GetConnector()
      ->BindConnectorRequest(std::move(request));
}

void RunDecodeCallbackOnTaskRunner(
    const image_decoder::mojom::ImageDecoder::DecodeImageCallback& callback,
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    const SkBitmap& image) {
  task_runner->PostTask(FROM_HERE, base::Bind(callback, image));
}

void DecodeImage(
    std::vector<uint8_t> image_data,
    image_decoder::mojom::ImageCodec codec,
    bool shrink_to_fit,
    const image_decoder::mojom::ImageDecoder::DecodeImageCallback& callback,
    scoped_refptr<base::SequencedTaskRunner> callback_task_runner) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  service_manager::mojom::ConnectorRequest connector_request;
  std::unique_ptr<service_manager::Connector> connector =
      service_manager::Connector::Create(&connector_request);
  BindToBrowserConnector(std::move(connector_request));

  image_decoder::Decode(connector.get(), image_data, codec, shrink_to_fit,
                        kMaxImageSizeInBytes,
                        base::Bind(&RunDecodeCallbackOnTaskRunner,
                                   callback, callback_task_runner));
}

}  // namespace

ImageDecoder::ImageRequest::ImageRequest()
    : task_runner_(base::ThreadTaskRunnerHandle::Get()) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
}

ImageDecoder::ImageRequest::ImageRequest(
    const scoped_refptr<base::SequencedTaskRunner>& task_runner)
    : task_runner_(task_runner) {
  DCHECK(sequence_checker_.CalledOnValidSequence());
}

ImageDecoder::ImageRequest::~ImageRequest() {
  DCHECK(sequence_checker_.CalledOnValidSequence());
  ImageDecoder::Cancel(this);
}

// static
ImageDecoder* ImageDecoder::GetInstance() {
  static auto* image_decoder = new ImageDecoder();
  return image_decoder;
}

// static
void ImageDecoder::Start(ImageRequest* image_request,
                         std::vector<uint8_t> image_data) {
  StartWithOptions(image_request, std::move(image_data), DEFAULT_CODEC, false);
}

// static
void ImageDecoder::Start(ImageRequest* image_request,
                         const std::string& image_data) {
  Start(image_request,
        std::vector<uint8_t>(image_data.begin(), image_data.end()));
}

// static
void ImageDecoder::StartWithOptions(ImageRequest* image_request,
                                    std::vector<uint8_t> image_data,
                                    ImageCodec image_codec,
                                    bool shrink_to_fit) {
  ImageDecoder::GetInstance()->StartWithOptionsImpl(
      image_request, std::move(image_data), image_codec, shrink_to_fit);
}

// static
void ImageDecoder::StartWithOptions(ImageRequest* image_request,
                                    const std::string& image_data,
                                    ImageCodec image_codec,
                                    bool shrink_to_fit) {
  StartWithOptions(image_request,
                   std::vector<uint8_t>(image_data.begin(), image_data.end()),
                   image_codec, shrink_to_fit);
}

ImageDecoder::ImageDecoder() : image_request_id_counter_(0) {}

void ImageDecoder::StartWithOptionsImpl(ImageRequest* image_request,
                                        std::vector<uint8_t> image_data,
                                        ImageCodec image_codec,
                                        bool shrink_to_fit) {
  DCHECK(image_request);
  DCHECK(image_request->task_runner());

  int request_id;
  {
    base::AutoLock lock(map_lock_);
    request_id = image_request_id_counter_++;
    image_request_id_map_.insert(std::make_pair(request_id, image_request));
  }

  image_decoder::mojom::ImageCodec codec =
      image_decoder::mojom::ImageCodec::DEFAULT;
#if defined(OS_CHROMEOS)
  if (image_codec == ROBUST_JPEG_CODEC)
    codec = image_decoder::mojom::ImageCodec::ROBUST_JPEG;
  if (image_codec == ROBUST_PNG_CODEC)
    codec = image_decoder::mojom::ImageCodec::ROBUST_PNG;
#endif  // defined(OS_CHROMEOS)

  auto callback = base::Bind(
      &OnDecodeImageDone,
      base::Bind(&ImageDecoder::OnDecodeImageFailed, base::Unretained(this)),
      base::Bind(&ImageDecoder::OnDecodeImageSucceeded, base::Unretained(this)),
      request_id);

  // NOTE: There exist ImageDecoder consumers which implicitly rely on this
  // operation happening on a thread which always has a ThreadTaskRunnerHandle.
  // We arbitrarily use the IO thread here to match details of the legacy
  // implementation.
  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&DecodeImage, base::Passed(&image_data), codec, shrink_to_fit,
                 callback, make_scoped_refptr(image_request->task_runner())));
}

// static
void ImageDecoder::Cancel(ImageRequest* image_request) {
  DCHECK(image_request);
  ImageDecoder::GetInstance()->CancelImpl(image_request);
}

void ImageDecoder::CancelImpl(ImageRequest* image_request) {
  base::AutoLock lock(map_lock_);
  for (auto it = image_request_id_map_.begin();
       it != image_request_id_map_.end();) {
    if (it->second == image_request) {
      image_request_id_map_.erase(it++);
    } else {
      ++it;
    }
  }
}

void ImageDecoder::OnDecodeImageSucceeded(
    const SkBitmap& decoded_image,
    int request_id) {
  ImageRequest* image_request;
  {
    base::AutoLock lock(map_lock_);
    auto it = image_request_id_map_.find(request_id);
    if (it == image_request_id_map_.end())
      return;
    image_request = it->second;
    image_request_id_map_.erase(it);
  }

  DCHECK(image_request->task_runner()->RunsTasksOnCurrentThread());
  image_request->OnImageDecoded(decoded_image);
}

void ImageDecoder::OnDecodeImageFailed(int request_id) {
  ImageRequest* image_request;
  {
    base::AutoLock lock(map_lock_);
    auto it = image_request_id_map_.find(request_id);
    if (it == image_request_id_map_.end())
      return;
    image_request = it->second;
    image_request_id_map_.erase(it);
  }

  DCHECK(image_request->task_runner()->RunsTasksOnCurrentThread());
  image_request->OnDecodeImageFailed();
}
