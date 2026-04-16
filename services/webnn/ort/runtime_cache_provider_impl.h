// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_WEBNN_ORT_RUNTIME_CACHE_PROVIDER_IMPL_H_
#define SERVICES_WEBNN_ORT_RUNTIME_CACHE_PROVIDER_IMPL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mojo/public/cpp/bindings/shared_remote.h"
#include "services/webnn/public/mojom/webnn_runtime_cache.mojom.h"

namespace webnn::ort {

// GPU-process runtime-cache bridge. SessionOptions owns an instance of this
// class and passes its address plus static callback-function addresses through
// ORT session config so the EP ABI can call back into Chromium without
// depending on Chromium-specific C++ types.
class RuntimeCacheProviderImpl {
 public:
  explicit RuntimeCacheProviderImpl(
      mojo::SharedRemote<mojom::RuntimeCacheHost> remote);
  ~RuntimeCacheProviderImpl();

  RuntimeCacheProviderImpl(const RuntimeCacheProviderImpl&) = delete;
  RuntimeCacheProviderImpl& operator=(const RuntimeCacheProviderImpl&) = delete;

  // Trampolines passed to the EP ABI as plain function pointers.
  static bool LoadCallback(void* context,
                           const char* cache_key,
                           void** data,
                           size_t* size);
  static bool SaveCallback(void* context,
                           const char* cache_key,
                           const void* data,
                           size_t size);
  static void ReleaseCallback(void* context, void* data, size_t size);

  std::vector<uint8_t> LoadCache(const char* cache_key);
  bool SaveCache(const char* cache_key, const void* data, size_t size);

 private:
  mojo::SharedRemote<mojom::RuntimeCacheHost> remote_;
};

}  // namespace webnn::ort

#endif  // SERVICES_WEBNN_ORT_RUNTIME_CACHE_PROVIDER_IMPL_H_
