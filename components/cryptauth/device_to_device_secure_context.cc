// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cryptauth/device_to_device_secure_context.h"

#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "components/cryptauth/proto/cryptauth_api.pb.h"
#include "components/cryptauth/proto/securemessage.pb.h"
#include "components/cryptauth/secure_message_delegate.h"
#include "components/proximity_auth/logging/logging.h"

namespace cryptauth {

namespace {

// The version to put in the GcmMetadata field.
const int kGcmMetadataVersion = 1;

// The sequence number of the last message used during authentication. These
// messages are sent and received before the SecureContext is created.
const int kAuthenticationSequenceNumber = 2;

}  // namespace

DeviceToDeviceSecureContext::DeviceToDeviceSecureContext(
    std::unique_ptr<SecureMessageDelegate> secure_message_delegate,
    const std::string& symmetric_key,
    const std::string& responder_auth_message,
    ProtocolVersion protocol_version)
    : secure_message_delegate_(std::move(secure_message_delegate)),
      symmetric_key_(symmetric_key),
      responder_auth_message_(responder_auth_message),
      protocol_version_(protocol_version),
      last_sequence_number_(kAuthenticationSequenceNumber),
      weak_ptr_factory_(this) {}

DeviceToDeviceSecureContext::~DeviceToDeviceSecureContext() {}

void DeviceToDeviceSecureContext::Decode(const std::string& encoded_message,
                                         const MessageCallback& callback) {
  SecureMessageDelegate::UnwrapOptions unwrap_options;
  unwrap_options.encryption_scheme = securemessage::AES_256_CBC;
  unwrap_options.signature_scheme = securemessage::HMAC_SHA256;

  secure_message_delegate_->UnwrapSecureMessage(
      encoded_message, symmetric_key_, unwrap_options,
      base::Bind(&DeviceToDeviceSecureContext::HandleUnwrapResult,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void DeviceToDeviceSecureContext::Encode(const std::string& message,
                                         const MessageCallback& callback) {
  // Create a GcmMetadata field to put in the header.
  GcmMetadata gcm_metadata;
  gcm_metadata.set_type(DEVICE_TO_DEVICE_MESSAGE);
  gcm_metadata.set_version(kGcmMetadataVersion);

  // Wrap |message| inside a DeviceToDeviceMessage proto.
  securemessage::DeviceToDeviceMessage device_to_device_message;
  device_to_device_message.set_sequence_number(++last_sequence_number_);
  device_to_device_message.set_message(message);

  SecureMessageDelegate::CreateOptions create_options;
  create_options.encryption_scheme = securemessage::AES_256_CBC;
  create_options.signature_scheme = securemessage::HMAC_SHA256;
  gcm_metadata.SerializeToString(&create_options.public_metadata);

  secure_message_delegate_->CreateSecureMessage(
      device_to_device_message.SerializeAsString(), symmetric_key_,
      create_options, callback);
}

std::string DeviceToDeviceSecureContext::GetChannelBindingData() const {
  return responder_auth_message_;
}

SecureContext::ProtocolVersion DeviceToDeviceSecureContext::GetProtocolVersion()
    const {
  return protocol_version_;
}

void DeviceToDeviceSecureContext::HandleUnwrapResult(
    const DeviceToDeviceSecureContext::MessageCallback& callback,
    bool verified,
    const std::string& payload,
    const securemessage::Header& header) {
  // The payload should contain a DeviceToDeviceMessage proto.
  securemessage::DeviceToDeviceMessage device_to_device_message;
  if (!verified || !device_to_device_message.ParseFromString(payload)) {
    PA_LOG(ERROR) << "Failed to unwrap secure message.";
    callback.Run(std::string());
    return;
  }

  // Check that the sequence number matches the expected sequence number.
  if (device_to_device_message.sequence_number() != last_sequence_number_ + 1) {
    PA_LOG(ERROR) << "Expected sequence_number=" << last_sequence_number_ + 1
                  << ", but got " << device_to_device_message.sequence_number();
    callback.Run(std::string());
    return;
  }

  // Validate the GcmMetadata proto in the header.
  GcmMetadata gcm_metadata;
  if (!gcm_metadata.ParseFromString(header.public_metadata()) ||
      gcm_metadata.type() != DEVICE_TO_DEVICE_MESSAGE ||
      gcm_metadata.version() != kGcmMetadataVersion) {
    PA_LOG(ERROR) << "Failed to validate GcmMetadata.";
    callback.Run(std::string());
    return;
  }

  last_sequence_number_++;
  callback.Run(device_to_device_message.message());
}

}  // cryptauth
