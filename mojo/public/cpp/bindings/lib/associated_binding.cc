// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/associated_binding.h"

namespace mojo {

AssociatedBindingBase::AssociatedBindingBase() {}

AssociatedBindingBase::~AssociatedBindingBase() {}

void AssociatedBindingBase::AddFilter(std::unique_ptr<MessageReceiver> filter) {
  DCHECK(endpoint_client_);
  endpoint_client_->AddFilter(std::move(filter));
}

void AssociatedBindingBase::Close() {
  endpoint_client_.reset();
}

void AssociatedBindingBase::CloseWithReason(uint32_t custom_reason,
                                            const std::string& description) {
  if (endpoint_client_)
    endpoint_client_->CloseWithReason(custom_reason, description);
  Close();
}

void AssociatedBindingBase::set_connection_error_handler(
    const base::Closure& error_handler) {
  DCHECK(is_bound());
  endpoint_client_->set_connection_error_handler(error_handler);
}

void AssociatedBindingBase::set_connection_error_with_reason_handler(
    const ConnectionErrorWithReasonCallback& error_handler) {
  DCHECK(is_bound());
  endpoint_client_->set_connection_error_with_reason_handler(error_handler);
}

void AssociatedBindingBase::FlushForTesting() {
  endpoint_client_->control_message_proxy()->FlushForTesting();
}

void AssociatedBindingBase::BindImpl(
    ScopedInterfaceEndpointHandle handle,
    MessageReceiverWithResponderStatus* receiver,
    std::unique_ptr<MessageReceiver> payload_validator,
    bool expect_sync_requests,
    scoped_refptr<base::SingleThreadTaskRunner> runner,
    uint32_t interface_version) {
  DCHECK(handle.is_local())
      << "The AssociatedInterfaceRequest is supposed to be used at the "
      << "other side of the message pipe.";

  if (!handle.is_valid() || !handle.is_local()) {
    endpoint_client_.reset();
    return;
  }

  endpoint_client_.reset(new InterfaceEndpointClient(
      std::move(handle), receiver, std::move(payload_validator),
      expect_sync_requests, std::move(runner), interface_version));
}

}  // namespace mojo
