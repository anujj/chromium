// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_WEBNN_HOST_RUNTIME_CACHE_HOST_IMPL_H_
#define SERVICES_WEBNN_HOST_RUNTIME_CACHE_HOST_IMPL_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/webnn/public/mojom/webnn_runtime_cache.mojom.h"

namespace webnn {

// Browser-process implementation of the RuntimeCacheHost Mojo interface.
// Handles reading/writing TRT-RTX runtime cache blobs to disk with
// origin-based partitioning for privacy isolation.
//
// Each instance is scoped to a single storage partition (origin). The
// partition directory is determined at construction time and all cache
// files are stored within it.
//
// Directory layout:
//   <profile_dir>/WebNN/RuntimeCache/<partition_key>/
//     <cache_key_1>
//     <cache_key_2>
//     ...
class RuntimeCacheHostImpl : public mojom::RuntimeCacheHost {
 public:
  // |partition_dir| is the storage-partition specific directory for this
  // instance. The serialized storage key is not persisted to disk.
  explicit RuntimeCacheHostImpl(base::FilePath partition_dir);

  ~RuntimeCacheHostImpl() override;

  RuntimeCacheHostImpl(const RuntimeCacheHostImpl&) = delete;
  RuntimeCacheHostImpl& operator=(const RuntimeCacheHostImpl&) = delete;

  // mojom::RuntimeCacheHost:
  void LoadCache(const std::string& cache_key,
                 LoadCacheCallback callback) override;
  void SaveCache(const std::string& cache_key,
                 const std::vector<uint8_t>& data,
                 SaveCacheCallback callback) override;
  void ClearCache(ClearCacheCallback callback) override;

  // Maximum total size of all cache files per origin (50 MB).
  static constexpr int64_t kMaxCacheSizePerOrigin = 50 * 1024 * 1024;

 private:
  // Constructs the full file path for a given cache key.
  // Sanitizes the key to prevent directory traversal attacks.
  base::FilePath GetCacheFilePath(const std::string& cache_key) const;

  // Enforces the per-origin size cap by evicting the oldest files.
  void EnforceSizeCap();

  base::FilePath partition_dir_;
};

// Creates a browser-process RuntimeCacheHost and returns a remote endpoint that
// can be passed to the GPU process.
// |storage_key| is used only to reject unpartitioned callers; |partition_dir|
// is already derived from the serialized storage key by the browser process.
mojo::PendingRemote<mojom::RuntimeCacheHost> CreateRuntimeCacheHost(
    base::FilePath partition_dir,
    std::string storage_key);

}  // namespace webnn

#endif  // SERVICES_WEBNN_HOST_RUNTIME_CACHE_HOST_IMPL_H_
