// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_BROWSER_SHELL_CONTENT_BROWSER_CLIENT_H_
#define CONTENT_SHELL_BROWSER_SHELL_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "build/build_config.h"
#include "content/public/browser/content_browser_client.h"
#include "content/shell/browser/shell_resource_dispatcher_host_delegate.h"
#include "content/shell/browser/shell_speech_recognition_manager_delegate.h"

namespace content {

class ShellBrowserContext;
class ShellBrowserMainParts;

class ShellContentBrowserClient : public ContentBrowserClient {
 public:
  // Gets the current instance.
  static ShellContentBrowserClient* Get();

  static void SetSwapProcessesForRedirect(bool swap);

  ShellContentBrowserClient();
  ~ShellContentBrowserClient() override;

  // ContentBrowserClient overrides.
  BrowserMainParts* CreateBrowserMainParts(
      const MainFunctionParams& parameters) override;
  bool DoesSiteRequireDedicatedProcess(BrowserContext* browser_context,
                                       const GURL& effective_url) override;
  bool IsHandledURL(const GURL& url) override;
  bool IsNPAPIEnabled() override;
  void RegisterInProcessMojoApplications(
      StaticMojoApplicationMap* apps) override;
  void RegisterOutOfProcessMojoApplications(
      OutOfProcessMojoApplicationMap* apps) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  void ResourceDispatcherHostCreated() override;
  GeolocationDelegate* CreateGeolocationDelegate() override;
  std::string GetDefaultDownloadName() override;
  WebContentsViewDelegate* GetWebContentsViewDelegate(
      WebContents* web_contents) override;
  QuotaPermissionContext* CreateQuotaPermissionContext() override;
  void SelectClientCertificate(
      WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      std::unique_ptr<ClientCertificateDelegate> delegate) override;

  SpeechRecognitionManagerDelegate* CreateSpeechRecognitionManagerDelegate()
      override;
  net::NetLog* GetNetLog() override;
  bool ShouldSwapProcessesForRedirect(ResourceContext* resource_context,
                                      const GURL& current_url,
                                      const GURL& new_url) override;
  DevToolsManagerDelegate* GetDevToolsManagerDelegate() override;

  void OpenURL(BrowserContext* browser_context,
               const OpenURLParams& params,
               const base::Callback<void(WebContents*)>& callback) override;

#if defined(OS_ANDROID)
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::FileDescriptorInfo* mappings,
      std::map<int, base::MemoryMappedFile::Region>* regions) override;
#elif defined(OS_POSIX) && !defined(OS_MACOSX)
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::FileDescriptorInfo* mappings) override;
#endif  // defined(OS_ANDROID)
#if defined(OS_WIN)
  bool PreSpawnRenderer(sandbox::TargetPolicy* policy) override;
#endif

  ShellBrowserContext* browser_context();
  ShellBrowserContext* off_the_record_browser_context();
  ShellResourceDispatcherHostDelegate* resource_dispatcher_host_delegate() {
    return resource_dispatcher_host_delegate_.get();
  }
  ShellBrowserMainParts* shell_browser_main_parts() {
    return shell_browser_main_parts_;
  }

  // Used for content_browsertests.
  void set_select_client_certificate_callback(
      base::Closure select_client_certificate_callback) {
    select_client_certificate_callback_ = select_client_certificate_callback;
  }

 protected:
  void set_resource_dispatcher_host_delegate(
      std::unique_ptr<ShellResourceDispatcherHostDelegate> delegate) {
    resource_dispatcher_host_delegate_ = std::move(delegate);
  }

  void set_browser_main_parts(ShellBrowserMainParts* parts) {
    shell_browser_main_parts_ = parts;
  }

 private:
  std::unique_ptr<ShellResourceDispatcherHostDelegate>
      resource_dispatcher_host_delegate_;

  base::Closure select_client_certificate_callback_;

  ShellBrowserMainParts* shell_browser_main_parts_;
};

}  // namespace content

#endif  // CONTENT_SHELL_BROWSER_SHELL_CONTENT_BROWSER_CLIENT_H_
