// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/webnn/host/runtime_cache_host_impl.h"

#include <algorithm>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "services/webnn/host/runtime_cache_utils.h"

namespace webnn {

RuntimeCacheHostImpl::RuntimeCacheHostImpl(
    base::FilePath partition_dir,
    std::string storage_key)
    : partition_dir_(std::move(partition_dir)) {
  if (!base::DirectoryExists(partition_dir_)) {
    base::CreateDirectory(partition_dir_);
  }
  if (!storage_key.empty() &&
      !WriteRuntimeCacheStorageKeyMetadata(partition_dir_, storage_key)) {
    LOG(WARNING)
        << "[WebNN RuntimeCacheHost] Failed to persist storage key metadata at "
        << partition_dir_;
  }
}

RuntimeCacheHostImpl::~RuntimeCacheHostImpl() = default;

mojo::PendingRemote<mojom::RuntimeCacheHost> CreateRuntimeCacheHost(
    base::FilePath partition_dir,
    std::string storage_key) {
  if (partition_dir.empty() || storage_key.empty()) {
    return {};
  }

  mojo::PendingRemote<mojom::RuntimeCacheHost> remote;
  auto receiver = remote.InitWithNewPipeAndPassReceiver();
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<RuntimeCacheHostImpl>(std::move(partition_dir),
                                             std::move(storage_key)),
      std::move(receiver));
  return remote;
}

void RuntimeCacheHostImpl::LoadCache(const std::string& cache_key,
                                     LoadCacheCallback callback) {
  base::FilePath path = GetCacheFilePath(cache_key);
  if (path.empty()) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  if (!base::PathExists(path)) {
    VLOG(1) << "[WebNN RuntimeCacheHost] Cache miss for key: " << cache_key;
    std::move(callback).Run(std::nullopt);
    return;
  }

  std::optional<std::vector<uint8_t>> data = base::ReadFileToBytes(path);
  if (!data.has_value()) {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Failed to read cache file: "
                 << path;
    std::move(callback).Run(std::nullopt);
    return;
  }

  VLOG(1) << "[WebNN RuntimeCacheHost] Cache hit for key: " << cache_key
          << " (" << data->size() << " bytes)";
  std::move(callback).Run(std::move(data));
}

void RuntimeCacheHostImpl::SaveCache(const std::string& cache_key,
                                     const std::vector<uint8_t>& data,
                                     SaveCacheCallback callback) {
  // Reject blobs that would single-handedly exceed the per-origin cap.
  if (static_cast<int64_t>(data.size()) > kMaxCacheSizePerOrigin) {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Cache blob too large ("
                 << data.size() << " bytes), rejecting";
    std::move(callback).Run(false);
    return;
  }

  base::FilePath path = GetCacheFilePath(cache_key);
  if (path.empty()) {
    std::move(callback).Run(false);
    return;
  }

  if (!base::DirectoryExists(partition_dir_)) {
    base::CreateDirectory(partition_dir_);
  }

  bool success = base::WriteFile(
      path, base::as_byte_span(data));

  if (success) {
    VLOG(1) << "[WebNN RuntimeCacheHost] Saved cache for key: " << cache_key
            << " (" << data.size() << " bytes)";
    EnforceSizeCap();
  } else {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Failed to write cache file: "
                 << path;
  }

  std::move(callback).Run(success);
}

void RuntimeCacheHostImpl::ClearCache(ClearCacheCallback callback) {
  bool success = base::DeletePathRecursively(partition_dir_);
  if (success) {
    VLOG(1) << "[WebNN RuntimeCacheHost] Cleared cache for partition: "
            << partition_dir_;
  } else {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Failed to clear cache at: "
                 << partition_dir_;
  }
  std::move(callback).Run(success);
}

base::FilePath RuntimeCacheHostImpl::GetCacheFilePath(
    const std::string& cache_key) const {
  // Reject keys that are too long. Cache keys are typically ~60 chars
  // (model_hash + node_name). Cap at 200 to stay well within MAX_PATH.
  static constexpr size_t kMaxCacheKeyLength = 200;
  if (cache_key.size() > kMaxCacheKeyLength) {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Cache key too long ("
                 << cache_key.size() << " chars), rejecting";
    return base::FilePath();
  }

  // Sanitize the cache key to prevent directory traversal.
  // Only allow alphanumeric, underscore, hyphen, and dot characters.
  for (char c : cache_key) {
    if (!base::IsAsciiAlphaNumeric(c) && c != '_' && c != '-' && c != '.') {
      LOG(WARNING) << "[WebNN RuntimeCacheHost] Invalid character in cache "
                      "key, rejecting: "
                   << cache_key;
      return base::FilePath();
    }
  }

  if (cache_key.empty() || cache_key == "." || cache_key == "..") {
    LOG(WARNING) << "[WebNN RuntimeCacheHost] Invalid cache key: " << cache_key;
    return base::FilePath();
  }

  base::FilePath path =
      partition_dir_.AppendASCII(cache_key);

  // Defense-in-depth: ensure the resolved path is within the partition dir.
  if (!partition_dir_.IsParent(path)) {
    LOG(WARNING)
        << "[WebNN RuntimeCacheHost] Cache path escaped partition directory";
    return base::FilePath();
  }

  return path;
}

void RuntimeCacheHostImpl::EnforceSizeCap() {
  // Collect all files in the partition directory with their sizes and
  // modification times.
  struct CacheFileInfo {
    base::FilePath path;
    int64_t size;
    base::Time last_modified;
  };

  std::vector<CacheFileInfo> files;
  int64_t total_size = 0;

  base::FileEnumerator enumerator(partition_dir_, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (path.BaseName().AsUTF8Unsafe() ==
        kWebNNRuntimeCacheStorageKeyMetadataFile) {
      continue;
    }
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    int64_t file_size = info.GetSize();
    total_size += file_size;
    files.push_back(
        {std::move(path), file_size, info.GetLastModifiedTime()});
  }

  if (total_size <= kMaxCacheSizePerOrigin) {
    return;
  }

  // Sort by last modified time, oldest first (LRU eviction).
  std::sort(files.begin(), files.end(),
            [](const CacheFileInfo& a, const CacheFileInfo& b) {
              return a.last_modified < b.last_modified;
            });

  // Evict oldest files until we're under the cap.
  for (const auto& file : files) {
    if (total_size <= kMaxCacheSizePerOrigin) {
      break;
    }
    if (base::DeleteFile(file.path)) {
      total_size -= file.size;
      VLOG(1) << "[WebNN RuntimeCacheHost] Evicted cache file: " << file.path
              << " (" << file.size << " bytes)";
    }
  }
}

}  // namespace webnn
