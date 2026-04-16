// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_WEBNN_ORT_ORT_SESSION_OPTIONS_H_
#define SERVICES_WEBNN_ORT_ORT_SESSION_OPTIONS_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/types/pass_key.h"
#include "mojo/public/cpp/bindings/shared_remote.h"
#include "services/webnn/ort/scoped_ort_types.h"
#include "services/webnn/public/mojom/webnn_device.mojom.h"
#include "services/webnn/public/mojom/webnn_error.mojom.h"
#include "services/webnn/public/mojom/webnn_service_introspection.mojom-forward.h"
#include "services/webnn/public/mojom/webnn_runtime_cache.mojom.h"
#include "third_party/windows_app_sdk_headers/src/inc/abi/winml/winml/onnxruntime_c_api.h"

namespace webnn::ort {

class Environment;
class RuntimeCacheProviderImpl;

// `SessionOptions` is a wrapper of `OrtSessionOptions` and used to create
// sessions on background threads.
class SessionOptions final : public base::RefCountedThreadSafe<SessionOptions> {
 public:
  // The `device_type` would be used to configure ONNX Runtime EP.
  // `cache_remote` is optional; when provided, enables TRT-RTX runtime cache
  // persistence through the browser process.
  static scoped_refptr<SessionOptions> Create(
      OrtHardwareDeviceType device_type,
      scoped_refptr<Environment> env,
      mojo::SharedRemote<mojom::RuntimeCacheHost> cache_remote = {});

  SessionOptions(base::PassKey<SessionOptions>,
                 ScopedOrtSessionOptions session_options,
                 OrtHardwareDeviceType device_type,
                 scoped_refptr<Environment> env,
                 std::unique_ptr<RuntimeCacheProviderImpl> cache_provider);

  SessionOptions(const SessionOptions&) = delete;
  SessionOptions& operator=(const SessionOptions&) = delete;

  const OrtSessionOptions* get() const { return session_options_.get(); }

  std::vector<mojom::WebNNExecutionProviderDetailsPtr>
  GetExecutionProvidersInfo() const;

  // Returns the first selected EP device for WebNN.
  const OrtEpDevice* first_selected_device() const {
    return first_selected_device_;
  }

 private:
  friend class base::RefCountedThreadSafe<SessionOptions>;

  ~SessionOptions();

  ScopedOrtSessionOptions session_options_;
  const OrtHardwareDeviceType device_type_;
  scoped_refptr<Environment> env_;
  // It's safe to keep `first_selected_device_` as `env_` owns all EP devices.
  raw_ptr<const OrtEpDevice> first_selected_device_;
  // Owned RuntimeCacheProviderImpl whose raw pointer is passed to the EP ABI.
  // Must outlive the ORT session that references it.
  std::unique_ptr<RuntimeCacheProviderImpl> cache_provider_;
};

}  // namespace webnn::ort

#endif  // SERVICES_WEBNN_ORT_ORT_SESSION_OPTIONS_H_
