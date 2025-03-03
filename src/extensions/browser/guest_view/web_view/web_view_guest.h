// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_H_
#define EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_H_

#include <stdint.h>

#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "components/guest_view/browser/guest_view.h"
#include "content/public/browser/javascript_dialog_manager.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/browser/guest_view/web_view/javascript_dialog_helper.h"
#include "extensions/browser/guest_view/web_view/web_view_find_helper.h"
#include "extensions/browser/guest_view/web_view/web_view_guest_delegate.h"
#include "extensions/browser/guest_view/web_view/web_view_permission_helper.h"
#include "extensions/browser/guest_view/web_view/web_view_permission_types.h"
#include "extensions/browser/script_executor.h"

namespace blink {
struct WebFindOptions;
}  // namespace blink

namespace content {
struct GlobalRequestID;
}  // namespace content

namespace extensions {

class WebViewInternalFindFunction;

// A WebViewGuest provides the browser-side implementation of the <webview> API
// and manages the dispatch of <webview> extension events. WebViewGuest is
// created on attachment. That is, when a guest WebContents is associated with
// a particular embedder WebContents. This happens on either initial navigation
// or through the use of the New Window API, when a new window is attached to
// a particular <webview>.
class WebViewGuest : public guest_view::GuestView<WebViewGuest>,
                     public content::NotificationObserver {
 public:
  // Clean up state when this GuestView is being destroyed. See
  // GuestViewBase::CleanUp().
  static void CleanUp(content::BrowserContext* browser_context,
                      int embedder_process_id,
                      int view_instance_id);

  static GuestViewBase* Create(content::WebContents* owner_web_contents);

  // For WebViewGuest, we create special guest processes, which host the
  // tag content separately from the main application that embeds the tag.
  // A <webview> can specify both the partition name and whether the storage
  // for that partition should be persisted. Each tag gets a SiteInstance with
  // a specially formatted URL, based on the application it is hosted by and
  // the partition requested by it. The format for that URL is:
  // chrome-guest://partition_domain/persist?partition_name
  static bool GetGuestPartitionConfigForSite(const GURL& site,
                                             std::string* partition_domain,
                                             std::string* partition_name,
                                             bool* in_memory);

  // Returns the WebView partition ID associated with the render process
  // represented by |render_process_host|, if any. Otherwise, an empty string is
  // returned.
  static std::string GetPartitionID(
      const content::RenderProcessHost* render_process_host);

  static const char Type[];

  // Returns the stored rules registry ID of the given webview. Will generate
  // an ID for the first query.
  static int GetOrGenerateRulesRegistryID(
      int embedder_process_id,
      int web_view_instance_id);

  // Get the current zoom.
  double GetZoom() const;

  // Get the current zoom mode.
  zoom::ZoomController::ZoomMode GetZoomMode();

  // Request navigating the guest to the provided |src| URL.
  void NavigateGuest(const std::string& src, bool force_navigation);

  // Shows the context menu for the guest.
  void ShowContextMenu(int request_id);

  // Sets the frame name of the guest.
  void SetName(const std::string& name);
  const std::string& name() { return name_; }

  // Set the zoom factor.
  void SetZoom(double zoom_factor);

  // Set the zoom mode.
  void SetZoomMode(zoom::ZoomController::ZoomMode zoom_mode);

  void SetAllowScaling(bool allow);
  bool allow_scaling() const { return allow_scaling_; }

  // Sets the transparency of the guest.
  void SetAllowTransparency(bool allow);
  bool allow_transparency() const { return allow_transparency_; }

  // Loads a data URL with a specified base URL and virtual URL.
  bool LoadDataWithBaseURL(const std::string& data_url,
                           const std::string& base_url,
                           const std::string& virtual_url,
                           std::string* error);

  // Begin or continue a find request.
  void StartFind(const base::string16& search_text,
                 const blink::WebFindOptions& options,
                 scoped_refptr<WebViewInternalFindFunction> find_function);

  // Conclude a find request to clear highlighting.
  void StopFinding(content::StopFindAction);

  // If possible, navigate the guest to |relative_index| entries away from the
  // current navigation entry. Returns true on success.
  bool Go(int relative_index);

  // Reload the guest.
  void Reload();

  // Overrides the user agent for this guest.
  // This affects subsequent guest navigations.
  void SetUserAgentOverride(const std::string& user_agent_override);

  // Stop loading the guest.
  void Stop();

  // Suspend the guest process
  void Suspend();

  // Resume the guest process
  void Resume();

  // Kill the guest process.
  void Terminate();

  // Clears data in the storage partition of this guest.
  //
  // Partition data that are newer than |removal_since| will be removed.
  // |removal_mask| corresponds to bitmask in StoragePartition::RemoveDataMask.
  bool ClearData(const base::Time remove_since,
                 uint32_t removal_mask,
                 const base::Closure& callback);

  ScriptExecutor* script_executor() { return script_executor_.get(); }

 private:
  friend class WebViewPermissionHelper;

  explicit WebViewGuest(content::WebContents* owner_web_contents);

  ~WebViewGuest() override;

  void ClearDataInternal(const base::Time remove_since,
                         uint32_t removal_mask,
                         const base::Closure& callback);

  void OnWebViewNewWindowResponse(int new_window_instance_id,
                                  bool allow,
                                  const std::string& user_input);

  void OnFullscreenPermissionDecided(bool allowed,
                                     const std::string& user_input);
  bool GuestMadeEmbedderFullscreen() const;
  void SetFullscreenState(bool is_fullscreen);

  // GuestViewBase implementation.
  bool CanRunInDetachedState() const final;
  void CreateWebContents(const base::DictionaryValue& create_params,
                         const WebContentsCreatedCallback& callback) final;
  void DidAttachToEmbedder() final;
  void DidDropLink(const GURL& url) final;
  void DidInitialize(const base::DictionaryValue& create_params) final;
  void EmbedderFullscreenToggled(bool entered_fullscreen) final;
  void FindReply(content::WebContents* source,
                 int request_id,
                 int number_of_matches,
                 const gfx::Rect& selection_rect,
                 int active_match_ordinal,
                 bool final_update) final;
  const char* GetAPINamespace() const final;
  int GetTaskPrefix() const final;
  void GuestDestroyed() final;
  void GuestReady() final;
  void GuestSizeChangedDueToAutoSize(const gfx::Size& old_size,
                                     const gfx::Size& new_size) final;
  void GuestViewDidStopLoading() final;
  void GuestZoomChanged(double old_zoom_level, double new_zoom_level) final;
  bool IsAutoSizeSupported() const final;
  bool IsDragAndDropEnabled() const final;
  void SetContextMenuPosition(const gfx::Point& position) final;
  void SignalWhenReady(const base::Closure& callback) final;
  bool ShouldHandleFindRequestsForEmbedder() const final;
  void WillAttachToEmbedder() final;
  void WillDestroy() final;

  // NotificationObserver implementation.
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) final;

  // WebContentsDelegate implementation.
  bool AddMessageToConsole(content::WebContents* source,
                           int32_t level,
                           const base::string16& message,
                           int32_t line_no,
                           const base::string16& source_id) final;
  void CloseContents(content::WebContents* source) final;
  bool HandleContextMenu(const content::ContextMenuParams& params) final;
  void HandleKeyboardEvent(content::WebContents* source,
                           const content::NativeWebKeyboardEvent& event) final;
  void LoadProgressChanged(content::WebContents* source, double progress) final;
  bool PreHandleGestureEvent(content::WebContents* source,
                             const blink::WebGestureEvent& event) final;
  void RendererResponsive(content::WebContents* source) final;
  void RendererUnresponsive(content::WebContents* source) final;
  void RequestMediaAccessPermission(
      content::WebContents* source,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback) final;
  void RequestPointerLockPermission(
      bool user_gesture,
      bool last_unlocked_by_target,
      const base::Callback<void(bool)>& callback) final;
  bool CheckMediaAccessPermission(content::WebContents* source,
                                  const GURL& security_origin,
                                  content::MediaStreamType type) final;
  void CanDownload(const GURL& url,
                   const std::string& request_method,
                   const base::Callback<void(bool)>& callback) final;
  content::JavaScriptDialogManager* GetJavaScriptDialogManager(
      content::WebContents* source) final;
  void AddNewContents(content::WebContents* source,
                      content::WebContents* new_contents,
                      WindowOpenDisposition disposition,
                      const gfx::Rect& initial_rect,
                      bool user_gesture,
                      bool* was_blocked) final;
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params) final;
  void WebContentsCreated(content::WebContents* source_contents,
                          int opener_render_frame_id,
                          const std::string& frame_name,
                          const GURL& target_url,
                          content::WebContents* new_contents) final;
  void EnterFullscreenModeForTab(content::WebContents* web_contents,
                                 const GURL& origin) final;
  void ExitFullscreenModeForTab(content::WebContents* web_contents) final;
  bool IsFullscreenForTabOrPending(
      const content::WebContents* web_contents) const final;

  // WebContentsObserver implementation.
  void DidCommitProvisionalLoadForFrame(
      content::RenderFrameHost* render_frame_host,
      const GURL& url,
      ui::PageTransition transition_type) final;
  void DidFailProvisionalLoad(content::RenderFrameHost* render_frame_host,
                              const GURL& validated_url,
                              int error_code,
                              const base::string16& error_description,
                              bool was_ignored_by_handler) final;
  void DidStartProvisionalLoadForFrame(
      content::RenderFrameHost* render_frame_host,
      const GURL& validated_url,
      bool is_error_page,
      bool is_iframe_srcdoc) final;
  void RenderProcessGone(base::TerminationStatus status) final;
  void UserAgentOverrideSet(const std::string& user_agent) final;
  void FrameNameChanged(content::RenderFrameHost* render_frame_host,
                        const std::string& name) final;

  // Informs the embedder of a frame name change.
  void ReportFrameNameChange(const std::string& name);

  // Called after the load handler is called in the guest's main frame.
  void LoadHandlerCalled();

  // Called when a redirect notification occurs.
  void LoadRedirect(const GURL& old_url,
                    const GURL& new_url,
                    bool is_top_level);

  void PushWebViewStateToIOThread();
  static void RemoveWebViewStateFromIOThread(
      content::WebContents* web_contents);

  // Loads the |url| provided. |force_navigation| indicates whether to reload
  // the content if the provided |url| matches the current page of the guest.
  void LoadURLWithParams(
      const GURL& url,
      const content::Referrer& referrer,
      ui::PageTransition transition_type,
      bool force_navigation);

  void RequestNewWindowPermission(
      WindowOpenDisposition disposition,
      const gfx::Rect& initial_bounds,
      bool user_gesture,
      content::WebContents* new_contents);

  // Requests resolution of a potentially relative URL.
  GURL ResolveURL(const std::string& src);

  // Notification that a load in the guest resulted in abort. Note that |url|
  // may be invalid.
  void LoadAbort(bool is_top_level,
                 const GURL& url,
                 int error_code,
                 const std::string& error_type);

  // Creates a new guest window owned by this WebViewGuest.
  void CreateNewGuestWebViewWindow(const content::OpenURLParams& params);

  void NewGuestWebViewCallback(const content::OpenURLParams& params,
                               content::WebContents* guest_web_contents);

  bool HandleKeyboardShortcuts(const content::NativeWebKeyboardEvent& event);

  void ApplyAttributes(const base::DictionaryValue& params);

  // Identifies the set of rules registries belonging to this guest.
  int rules_registry_id_;

  // Handles find requests and replies for the webview find API.
  WebViewFindHelper find_helper_;

  base::ObserverList<ScriptExecutionObserver> script_observers_;
  std::unique_ptr<ScriptExecutor> script_executor_;

  content::NotificationRegistrar notification_registrar_;

  // True if the user agent is overridden.
  bool is_overriding_user_agent_;

  // Stores the window name of the main frame of the guest.
  std::string name_;

  // Stores whether the contents of the guest can be transparent.
  bool allow_transparency_;

  // Stores the src URL of the WebView.
  GURL src_;

  // Handles the JavaScript dialog requests.
  JavaScriptDialogHelper javascript_dialog_helper_;

  // Handles permission requests.
  std::unique_ptr<WebViewPermissionHelper> web_view_permission_helper_;

  std::unique_ptr<WebViewGuestDelegate> web_view_guest_delegate_;

  // Tracks the name, and target URL of the new window. Once the first
  // navigation commits, we no longer track this information.
  struct NewWindowInfo {
    GURL url;
    std::string name;
    bool changed;
    NewWindowInfo(const GURL& url, const std::string& name) :
        url(url),
        name(name),
        changed(false) {}
  };

  using PendingWindowMap = std::map<WebViewGuest*, NewWindowInfo>;
  PendingWindowMap pending_new_windows_;

  // Determines if this guest accepts pinch-zoom gestures.
  bool allow_scaling_;
  bool is_guest_fullscreen_;
  bool is_embedder_fullscreen_;
  bool last_fullscreen_permission_was_allowed_by_embedder_;

  // Tracks whether the webview has a pending zoom from before the first
  // navigation. This will be equal to 0 when there is no pending zoom.
  double pending_zoom_factor_;

  // Whether the GuestView is suspended.
  bool is_suspended_;

  // This is used to ensure pending tasks will not fire after this object is
  // destroyed.
  base::WeakPtrFactory<WebViewGuest> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebViewGuest);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_GUEST_VIEW_WEB_VIEW_WEB_VIEW_GUEST_H_
