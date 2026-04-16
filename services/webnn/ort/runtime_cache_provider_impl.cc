// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/webnn/ort/runtime_cache_provider_impl.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/logging.h"

namespace webnn::ort {

RuntimeCacheProviderImpl::RuntimeCacheProviderImpl(
    mojo::SharedRemote<mojom::RuntimeCacheHost> remote)
    : remote_(std::move(remote)) {
  CHECK(remote_.is_bound());
}

RuntimeCacheProviderImpl::~RuntimeCacheProviderImpl() = default;

bool RuntimeCacheProviderImpl::LoadCallback(void* context,
                                            const char* cache_key,
                                            void** data,
                                            size_t* size) {
  CHECK(context);
  CHECK(data);
  CHECK(size);

  *data = nullptr;
  *size = 0;

  auto bytes =
      static_cast<RuntimeCacheProviderImpl*>(context)->LoadCache(cache_key);
  if (bytes.empty()) {
    return false;
  }

  void* buffer = std::malloc(bytes.size());
  if (!buffer) {
    LOG(ERROR) << "[WebNN RuntimeCache] Failed to allocate " << bytes.size()
               << " bytes for key: " << cache_key;
    return false;
  }

  std::memcpy(buffer, bytes.data(), bytes.size());
  *data = buffer;
  *size = bytes.size();
  return true;
}

bool RuntimeCacheProviderImpl::SaveCallback(void* context,
                                            const char* cache_key,
                                            const void* data,
                                            size_t size) {
  CHECK(context);
  return static_cast<RuntimeCacheProviderImpl*>(context)->SaveCache(cache_key,
                                                                    data, size);
}

void RuntimeCacheProviderImpl::ReleaseCallback(void* context,
                                               void* data,
                                               size_t size) {
  CHECK(context);
  static_cast<void>(size);
  std::free(data);
}

std::vector<uint8_t> RuntimeCacheProviderImpl::LoadCache(
    const char* cache_key) {
  if (!remote_.is_bound()) {
    LOG(WARNING) << "[WebNN RuntimeCache] Remote not bound, cannot load cache "
                    "for key: "
                 << cache_key;
    return {};
  }

  // [Sync] Mojo call: blocks until the browser process responds.
  // This is acceptable because LoadCache is only called during ORT session
  // creation (not on the inference hot path), and session creation is
  // already a blocking operation from the web page's perspective.
  std::optional<std::vector<uint8_t>> result;
  bool mojo_ok = remote_->LoadCache(std::string(cache_key), &result);
  if (!mojo_ok) {
    LOG(WARNING) << "[WebNN RuntimeCache] Mojo call failed for key: "
                 << cache_key;
    return {};
  }

  if (!result.has_value()) {
    VLOG(1) << "[WebNN RuntimeCache] Cache miss for key: " << cache_key;
    return {};
  }

  VLOG(1) << "[WebNN RuntimeCache] Cache hit for key: " << cache_key
          << " (" << result->size() << " bytes)";
  return std::move(*result);
}

bool RuntimeCacheProviderImpl::SaveCache(const char* cache_key,
                                         const void* data,
                                         size_t size) {
  if (!remote_.is_bound()) {
    LOG(WARNING) << "[WebNN RuntimeCache] Remote not bound, cannot save cache "
                    "for key: "
                  << cache_key;
    return false;
  }

  std::vector<uint8_t> blob(static_cast<const uint8_t*>(data),
                            static_cast<const uint8_t*>(data) + size);

  VLOG(1) << "[WebNN RuntimeCache] Saving cache for key: " << cache_key
          << " (" << size << " bytes)";

  remote_->SaveCache(std::string(cache_key), std::move(blob), base::DoNothing());
  return true;
}

}  // namespace webnn::ort
