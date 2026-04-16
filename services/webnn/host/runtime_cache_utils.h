// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_WEBNN_HOST_RUNTIME_CACHE_UTILS_H_
#define SERVICES_WEBNN_HOST_RUNTIME_CACHE_UTILS_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/hash/sha1.h"
#include "base/strings/string_number_conversions.h"

namespace webnn {

// The subdirectory within the user profile for WebNN runtime cache storage.
inline constexpr char kWebNNRuntimeCacheDir[] = "WebNN/RuntimeCache";
inline constexpr char kWebNNRuntimeCacheStorageKeyMetadataFile[] =
    ".storage_key";

inline base::FilePath GetRuntimeCacheRootDir(const base::FilePath& profile_dir) {
  return profile_dir.AppendASCII(kWebNNRuntimeCacheDir);
}

// Constructs the storage-partitioned cache directory path for a given storage
// key. The serialized storage key is hashed to create a filesystem-safe
// directory name.
//
// Example:
//   profile_dir = "C:\Users\foo\AppData\Local\Chromium\User Data\Default"
//   storage_key = "https://example.com^0https://example.com"
//   result = "C:\...\Default\WebNN\RuntimeCache\a1b2c3d4e5..."
inline base::FilePath GetRuntimeCachePartitionDir(
    const base::FilePath& profile_dir,
    const std::string& storage_key) {
  std::string storage_key_hash =
      base::HexEncode(base::SHA1Hash(base::as_byte_span(storage_key)));
  return GetRuntimeCacheRootDir(profile_dir).AppendASCII(storage_key_hash);
}

inline base::FilePath GetRuntimeCacheStorageKeyMetadataPath(
    const base::FilePath& partition_dir) {
  return partition_dir.AppendASCII(kWebNNRuntimeCacheStorageKeyMetadataFile);
}

inline bool WriteRuntimeCacheStorageKeyMetadata(
    const base::FilePath& partition_dir,
    const std::string& storage_key) {
  if (!base::CreateDirectory(partition_dir)) {
    return false;
  }

  return base::WriteFile(GetRuntimeCacheStorageKeyMetadataPath(partition_dir),
                         storage_key);
}

inline std::optional<std::string> ReadRuntimeCacheStorageKeyMetadata(
    const base::FilePath& partition_dir) {
  std::string storage_key;
  if (!base::ReadFileToString(
          GetRuntimeCacheStorageKeyMetadataPath(partition_dir), &storage_key) ||
      storage_key.empty()) {
    return std::nullopt;
  }

  return storage_key;
}

inline std::vector<base::FilePath> GetRuntimeCachePartitionDirs(
    const base::FilePath& profile_dir) {
  std::vector<base::FilePath> partition_dirs;
  base::FilePath cache_root = GetRuntimeCacheRootDir(profile_dir);
  if (!base::DirectoryExists(cache_root)) {
    return partition_dirs;
  }

  base::FileEnumerator enumerator(cache_root, /*recursive=*/false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    partition_dirs.push_back(std::move(path));
  }

  return partition_dirs;
}

// Deletes all WebNN runtime cache data for all origins.
// Intended to be called by BrowsingDataRemover when the user
// clears "Cached images and files" or all browsing data.
//
// TODO(webnn): Register this with BrowsingDataRemoverImpl via
// BrowsingDataRemoverImpl::RemoveImpl() for
// BrowsingDataRemover::DATA_TYPE_CACHE.
inline bool ClearAllRuntimeCaches(const base::FilePath& profile_dir) {
  base::FilePath cache_root = GetRuntimeCacheRootDir(profile_dir);
  return !base::PathExists(cache_root) ||
         base::DeletePathRecursively(cache_root);
}

// Deletes WebNN runtime cache data for a specific storage partition.
// Intended to be called when the user clears site-specific data.
inline bool ClearRuntimeCacheForStorageKey(const base::FilePath& profile_dir,
                                           const std::string& storage_key) {
  base::FilePath partition_dir =
      GetRuntimeCachePartitionDir(profile_dir, storage_key);
  return !base::PathExists(partition_dir) ||
         base::DeletePathRecursively(partition_dir);
}

}  // namespace webnn

#endif  // SERVICES_WEBNN_HOST_RUNTIME_CACHE_UTILS_H_
