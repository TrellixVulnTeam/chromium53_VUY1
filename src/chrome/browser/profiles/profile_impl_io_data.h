// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_
#define CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_

#include "base/callback.h"
#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/custom_handlers/protocol_handler_registry.h"
#include "chrome/browser/net/chrome_url_request_context_getter.h"
#include "chrome/browser/profiles/profile_io_data.h"
#include "components/prefs/pref_store.h"
#include "content/public/browser/cookie_store_factory.h"

class JsonPrefStore;

namespace chrome_browser_net {
class Predictor;
}  // namespace chrome_browser_net

namespace data_reduction_proxy {
class DataReductionProxyNetworkDelegate;
}  // namespace data_reduction_proxy

namespace domain_reliability {
class DomainReliabilityMonitor;
}  // namespace domain_reliability

namespace net {
class CodeCache;
class CookieCryptoDelegate;
class CookieStore;
class FtpTransactionFactory;
class HttpNetworkSession;
class HttpServerProperties;
class HttpServerPropertiesManager;
class HttpTransactionFactory;
class ProxyConfig;
class SdchManager;
class SdchOwner;
}  // namespace net

namespace storage {
class SpecialStoragePolicy;
}  // namespace storage

class ProfileImplIOData : public ProfileIOData {
 public:
  class Handle {
   public:
    explicit Handle(Profile* profile);
    ~Handle();

    // Init() must be called before ~Handle(). It records most of the
    // parameters needed to construct a ChromeURLRequestContextGetter.
    void Init(const base::FilePath& cookie_path,
              const base::FilePath& channel_id_path,
              const base::FilePath& cache_path,
              int cache_max_size,
              const base::FilePath& media_cache_path,
              int media_cache_max_size,
              const base::FilePath& extensions_cookie_path,
              const base::FilePath& profile_path,
              chrome_browser_net::Predictor* predictor,
              content::CookieStoreConfig::SessionCookieMode session_cookie_mode,
              storage::SpecialStoragePolicy* special_storage_policy,
              std::unique_ptr<domain_reliability::DomainReliabilityMonitor>
                  domain_reliability_monitor);

    // These Create*ContextGetter() functions are only exposed because the
    // circular relationship between Profile, ProfileIOData::Handle, and the
    // ChromeURLRequestContextGetter factories requires Profile be able to call
    // these functions.
    scoped_refptr<ChromeURLRequestContextGetter> CreateMainRequestContextGetter(
        content::ProtocolHandlerMap* protocol_handlers,
        content::URLRequestInterceptorScopedVector request_interceptors,
        IOThread* io_thread) const;
    scoped_refptr<ChromeURLRequestContextGetter>
        CreateIsolatedAppRequestContextGetter(
            const base::FilePath& partition_path,
            bool in_memory,
            content::ProtocolHandlerMap* protocol_handlers,
            content::URLRequestInterceptorScopedVector
                request_interceptors) const;

    content::ResourceContext* GetResourceContext() const;
    // GetResourceContextNoInit() does not call LazyInitialize() so it can be
    // safely be used during initialization.
    content::ResourceContext* GetResourceContextNoInit() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetMediaRequestContextGetter() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetExtensionsRequestContextGetter() const;
    scoped_refptr<ChromeURLRequestContextGetter>
        GetIsolatedMediaRequestContextGetter(
            const base::FilePath& partition_path,
            bool in_memory) const;

    // Returns the DevToolsNetworkControllerHandle attached to ProfileIOData.
    DevToolsNetworkControllerHandle* GetDevToolsNetworkControllerHandle() const;

    // Deletes all network related data since |time|. It deletes transport
    // security state since |time| and also deletes HttpServerProperties data.
    // Works asynchronously, however if the |completion| callback is non-null,
    // it will be posted on the UI thread once the removal process completes.
    void ClearNetworkingHistorySince(base::Time time,
                                     const base::Closure& completion);

   private:
    typedef std::map<StoragePartitionDescriptor,
                     scoped_refptr<ChromeURLRequestContextGetter>,
                     StoragePartitionDescriptorLess>
      ChromeURLRequestContextGetterMap;

    // Lazily initialize ProfileParams. We do this on the calls to
    // Get*RequestContextGetter(), so we only initialize ProfileParams right
    // before posting a task to the IO thread to start using them. This prevents
    // objects that are supposed to be deleted on the IO thread, but are created
    // on the UI thread from being unnecessarily initialized.
    void LazyInitialize() const;

    // Collect references to context getters in reverse order, i.e. last item
    // will be main request getter. This list is passed to |io_data_|
    // for invalidation on IO thread.
    std::unique_ptr<ChromeURLRequestContextGetterVector> GetAllContextGetters();

    // The getters will be invalidated on the IO thread before
    // ProfileIOData instance is deleted.
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        main_request_context_getter_;
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        media_request_context_getter_;
    mutable scoped_refptr<ChromeURLRequestContextGetter>
        extensions_request_context_getter_;
    mutable ChromeURLRequestContextGetterMap app_request_context_getter_map_;
    mutable ChromeURLRequestContextGetterMap
        isolated_media_request_context_getter_map_;
    ProfileImplIOData* const io_data_;

    Profile* const profile_;

    mutable bool initialized_;

    DISALLOW_COPY_AND_ASSIGN(Handle);
  };

 private:
  friend class base::RefCountedThreadSafe<ProfileImplIOData>;

  struct LazyParams {
    LazyParams();
    ~LazyParams();

    // All of these parameters are intended to be read on the IO thread.
    base::FilePath cookie_path;
    base::FilePath channel_id_path;
    base::FilePath cache_path;
    int cache_max_size;
    base::FilePath media_cache_path;
    int media_cache_max_size;
    base::FilePath extensions_cookie_path;
    content::CookieStoreConfig::SessionCookieMode session_cookie_mode;
    scoped_refptr<storage::SpecialStoragePolicy> special_storage_policy;
  };

  ProfileImplIOData();
  ~ProfileImplIOData() override;

  void InitializeInternal(
      std::unique_ptr<ChromeNetworkDelegate> chrome_network_delegate,
      ProfileParams* profile_params,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      const override;
  void InitializeExtensionsRequestContext(
      ProfileParams* profile_params) const override;
  net::URLRequestContext* InitializeAppRequestContext(
      net::URLRequestContext* main_context,
      const StoragePartitionDescriptor& partition_descriptor,
      std::unique_ptr<ProtocolHandlerRegistry::JobInterceptorFactory>
          protocol_handler_interceptor,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      const override;
  net::URLRequestContext* InitializeMediaRequestContext(
      net::URLRequestContext* original_context,
      const StoragePartitionDescriptor& partition_descriptor) const override;
  net::URLRequestContext* AcquireMediaRequestContext() const override;
  net::URLRequestContext* AcquireIsolatedAppRequestContext(
      net::URLRequestContext* main_context,
      const StoragePartitionDescriptor& partition_descriptor,
      std::unique_ptr<ProtocolHandlerRegistry::JobInterceptorFactory>
          protocol_handler_interceptor,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      const override;
  net::URLRequestContext* AcquireIsolatedMediaRequestContext(
      net::URLRequestContext* app_context,
      const StoragePartitionDescriptor& partition_descriptor) const override;
  chrome_browser_net::Predictor* GetPredictor() override;

  // Deletes all network related data since |time|. It deletes transport
  // security state since |time| and also deletes HttpServerProperties data.
  // Works asynchronously, however if the |completion| callback is non-null,
  // it will be posted on the UI thread once the removal process completes.
  void ClearNetworkingHistorySinceOnIOThread(base::Time time,
                                             const base::Closure& completion);

  mutable std::unique_ptr<
      data_reduction_proxy::DataReductionProxyNetworkDelegate>
      network_delegate_;

  // Lazy initialization params.
  mutable std::unique_ptr<LazyParams> lazy_params_;

  mutable scoped_refptr<JsonPrefStore> network_json_store_;

  mutable std::unique_ptr<net::HttpNetworkSession> http_network_session_;
  mutable std::unique_ptr<net::HttpTransactionFactory> main_http_factory_;
  mutable std::unique_ptr<net::FtpTransactionFactory> ftp_factory_;

  // Same as |ProfileIOData::http_server_properties_|, owned there to maintain
  // destruction ordering.
  mutable net::HttpServerPropertiesManager* http_server_properties_manager_;

  mutable std::unique_ptr<net::CookieStore> main_cookie_store_;
  mutable std::unique_ptr<net::CookieStore> extensions_cookie_store_;

  mutable std::unique_ptr<chrome_browser_net::Predictor> predictor_;

  mutable std::unique_ptr<net::URLRequestContext> media_request_context_;

  mutable std::unique_ptr<net::URLRequestJobFactory> main_job_factory_;
  mutable std::unique_ptr<net::URLRequestJobFactory> extensions_job_factory_;

  mutable std::unique_ptr<domain_reliability::DomainReliabilityMonitor>
      domain_reliability_monitor_;

  mutable std::unique_ptr<net::SdchManager> sdch_manager_;
  mutable std::unique_ptr<net::SdchOwner> sdch_policy_;

  mutable std::unique_ptr<net::CodeCache> code_cache_;

  // Parameters needed for isolated apps.
  base::FilePath profile_path_;
  int app_cache_max_size_;
  int app_media_cache_max_size_;

  DISALLOW_COPY_AND_ASSIGN(ProfileImplIOData);
};

#endif  // CHROME_BROWSER_PROFILES_PROFILE_IMPL_IO_DATA_H_
