// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/libaddressinput/chromium/chrome_storage_impl.h"

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "components/prefs/writeable_pref_store.h"
#include "third_party/libaddressinput/chromium/fallback_data_store.h"

namespace autofill {

ChromeStorageImpl::ChromeStorageImpl(WriteablePrefStore* store)
    : backing_store_(store),
      scoped_observer_(this) {
  scoped_observer_.Add(backing_store_);
}

ChromeStorageImpl::~ChromeStorageImpl() {}

void ChromeStorageImpl::Put(const std::string& key, std::string* data) {
  DCHECK(data);
  std::unique_ptr<std::string> owned_data(data);
  backing_store_->SetValue(
      key, base::MakeUnique<base::StringValue>(std::move(*owned_data)),
      WriteablePrefStore::DEFAULT_PREF_WRITE_FLAGS);
}

void ChromeStorageImpl::Get(const std::string& key,
                            const Storage::Callback& data_ready) const {
  // |Get()| should not be const, so this is just a thunk that fixes that.
  const_cast<ChromeStorageImpl*>(this)->DoGet(key, data_ready);
}

void ChromeStorageImpl::OnPrefValueChanged(const std::string& key) {}

void ChromeStorageImpl::OnInitializationCompleted(bool succeeded) {
  for (std::vector<Request*>::iterator iter = outstanding_requests_.begin();
       iter != outstanding_requests_.end(); ++iter) {
    DoGet((*iter)->key, (*iter)->callback);
  }

  outstanding_requests_.clear();
}

void ChromeStorageImpl::DoGet(const std::string& key,
                              const Storage::Callback& data_ready) {
  if (!backing_store_->IsInitializationComplete()) {
    outstanding_requests_.push_back(new Request(key, data_ready));
    return;
  }

  const base::Value* value = NULL;
  std::unique_ptr<std::string> data(new std::string);
  if (backing_store_->GetValue(key, &value) && value->GetAsString(data.get())) {
    data_ready(true, key, data.release());
  } else if (FallbackDataStore::Get(key, data.get())) {
    data_ready(true, key, data.release());
  } else {
    data_ready(false, key, NULL);
  }
}

ChromeStorageImpl::Request::Request(const std::string& key,
                                    const Callback& callback)
    : key(key),
      callback(callback) {}

}  // namespace autofill
