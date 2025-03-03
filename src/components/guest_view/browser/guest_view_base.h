// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_GUEST_VIEW_BROWSER_GUEST_VIEW_BASE_H_
#define COMPONENTS_GUEST_VIEW_BROWSER_GUEST_VIEW_BASE_H_

#include <memory>
#include <queue>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "components/guest_view/common/guest_view_constants.h"
#include "components/zoom/zoom_observer.h"
#include "content/public/browser/browser_plugin_guest_delegate.h"
#include "content/public/browser/guest_host.h"
#include "content/public/browser/render_process_host_observer.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"

struct RendererContentSettingRules;

namespace guest_view {

class GuestViewEvent;

// A struct of parameters for SetSize(). The parameters are all declared as
// scoped pointers since they are all optional. Null pointers indicate that the
// parameter has not been provided, and the last used value should be used. Note
// that when |enable_auto_size| is true, providing |normal_size| is not
// meaningful. This is because the normal size of the guestview is overridden
// whenever autosizing occurs.
struct SetSizeParams {
  SetSizeParams();
  ~SetSizeParams();

  std::unique_ptr<bool> enable_auto_size;
  std::unique_ptr<gfx::Size> min_size;
  std::unique_ptr<gfx::Size> max_size;
  std::unique_ptr<gfx::Size> normal_size;
};

// A GuestViewBase is the base class browser-side API implementation for a
// <*view> tag. GuestViewBase maintains an association between a guest
// WebContents and an owner WebContents. It receives events issued from
// the guest and relays them to the owner. GuestViewBase tracks the lifetime
// of its owner. A GuestViewBase's owner is referred to as an embedder if
// it is attached to a container within the owner's WebContents.
class GuestViewBase : public content::BrowserPluginGuestDelegate,
                      public content::WebContentsDelegate,
                      public content::WebContentsObserver,
                      public zoom::ZoomObserver {
 public:
  // Returns a *ViewGuest if this GuestView is of the given view type.
  template <typename T>
  T* As() {
    if (IsViewType(T::Type))
      return static_cast<T*>(this);

    return nullptr;
  }

  // Cleans up state when this GuestView is being destroyed.
  // Note that this cannot be done in the destructor since a GuestView could
  // potentially be created and destroyed in JavaScript before getting a
  // GuestViewBase instance. This method can be hidden by a CleanUp() method in
  // a derived class, in which case the derived method should call this one.
  static void CleanUp(content::BrowserContext* browser_context,
                      int embedder_process_id,
                      int view_instance_id);

  static GuestViewBase* FromWebContents(
      const content::WebContents* web_contents);

  static GuestViewBase* From(int owner_process_id, int instance_id);

  // Given a |web_contents|, returns the top level owner WebContents. If
  // |web_contents| does not belong to a GuestView, it will be returned
  // unchanged.
  static content::WebContents* GetTopLevelWebContents(
      content::WebContents* web_contents);

  static bool IsGuest(content::WebContents* web_contents);

  // Returns the name of the derived type of this GuestView.
  virtual const char* GetViewType() const = 0;

  // This method queries whether autosize is supported for this particular view.
  // By default, autosize is not supported. Derived classes can override this
  // behavior to support autosize.
  virtual bool IsAutoSizeSupported() const;

  // This method queries whether preferred size events are enabled for this
  // view. By default, preferred size events are disabled, since they add a
  // small amount of overhead.
  virtual bool IsPreferredSizeModeEnabled() const;

  // This indicates whether zoom should propagate from the embedder to the guest
  // content.
  virtual bool ZoomPropagatesFromEmbedderToGuest() const;

  // Access to guest views are determined by the availability of the internal
  // extension API used to implement the guest view.
  //
  // This should be the name of the API as it appears in the _api_features.json
  // file.
  virtual const char* GetAPINamespace() const = 0;

  // This method is the task prefix to show for a task produced by this
  // GuestViewBase's derived type.
  virtual int GetTaskPrefix() const = 0;

  // Dispatches an event to the guest proxy.
  void DispatchEventToGuestProxy(std::unique_ptr<GuestViewEvent> event);

  // Dispatches an event to the view.
  void DispatchEventToView(std::unique_ptr<GuestViewEvent> event);

  // This creates a WebContents and initializes |this| GuestViewBase to use the
  // newly created WebContents.
  using WebContentsCreatedCallback =
      base::Callback<void(content::WebContents*)>;
  void Init(const base::DictionaryValue& create_params,
            const WebContentsCreatedCallback& callback);

  void InitWithWebContents(const base::DictionaryValue& create_params,
                           content::WebContents* guest_web_contents);

  bool IsViewType(const char* const view_type) const {
    return !strcmp(GetViewType(), view_type);
  }

  // Used to toggle autosize mode for this GuestView, and set both the automatic
  // and normal sizes.
  void SetSize(const SetSizeParams& params);

  bool initialized() const { return initialized_; }

  content::WebContents* embedder_web_contents() const {
    return attached() ? owner_web_contents_ : nullptr;
  }

  content::WebContents* owner_web_contents() const {
    return owner_web_contents_;
  }

  content::GuestHost* host() const {
    return guest_host_;
  }

  // Returns the parameters associated with the element hosting this GuestView
  // passed in from JavaScript.
  base::DictionaryValue* attach_params() const { return attach_params_.get(); }

  // Returns whether this guest has an associated embedder.
  bool attached() const {
    return element_instance_id_ != kInstanceIDNone;
  }

  // Returns the instance ID of the <*view> element.
  int view_instance_id() const { return view_instance_id_; }

  // Returns the instance ID of this GuestViewBase.
  int guest_instance_id() const { return guest_instance_id_; }

  // Returns the instance ID of the GuestViewBase's element.
  int element_instance_id() const { return element_instance_id_; }

  bool can_owner_receive_events() const { return !!view_instance_id_; }

  gfx::Size size() const { return guest_size_; }

  // Returns the user browser context of the embedder.
  content::BrowserContext* browser_context() const { return browser_context_; }

  GuestViewBase* GetOpener() const {
    return opener_.get();
  }

  // Returns the URL of the owner WebContents.
  const GURL& GetOwnerSiteURL() const;

  // Returns the host of the owner WebContents. For extensions, this is the
  // extension ID.
  std::string owner_host() const { return owner_host_; }

  // Whether the guest view is inside a plugin document.
  bool is_full_page_plugin() const { return is_full_page_plugin_; }

  // Returns the routing ID of the guest proxy in the owner's renderer process.
  // This value is only valid after attachment or first navigation.
  int proxy_routing_id() const { return guest_proxy_routing_id_; }

  // Destroy this guest.
  void Destroy();

  // Saves the attach state of the custom element hosting this GuestView.
  void SetAttachParams(const base::DictionaryValue& params);
  void SetOpener(GuestViewBase* opener);

 protected:
  explicit GuestViewBase(content::WebContents* owner_web_contents);

  ~GuestViewBase() override;

  // BrowserPluginGuestDelegate implementation.
  void SetContextMenuPosition(const gfx::Point& position) override;

  // WebContentsDelegate implementation.
  void HandleKeyboardEvent(
      content::WebContents* source,
      const content::NativeWebKeyboardEvent& event) override;
  bool PreHandleGestureEvent(content::WebContents* source,
                             const blink::WebGestureEvent& event) override;
  void FindReply(content::WebContents* source,
                 int request_id,
                 int number_of_matches,
                 const gfx::Rect& selection_rect,
                 int active_match_ordinal,
                 bool final_update) override;

  // WebContentsObserver implementation.
  void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) override;

  // Given a set of initialization parameters, a concrete subclass of
  // GuestViewBase can create a specialized WebContents that it returns back to
  // GuestViewBase.
  virtual void CreateWebContents(
      const base::DictionaryValue& create_params,
      const WebContentsCreatedCallback& callback) = 0;

  // This method is called after the guest has been attached to an embedder and
  // suspended resource loads have been resumed.
  //
  // This method can be overriden by subclasses. This gives the derived class
  // an opportunity to perform setup actions after attachment.
  virtual void DidAttachToEmbedder() {}

  // This method is called after this GuestViewBase has been initiated.
  //
  // This gives the derived class an opportunity to perform additional
  // initialization.
  virtual void DidInitialize(const base::DictionaryValue& create_params) {}

  // This method is called when embedder WebContents's fullscreen is toggled.
  //
  // If the guest asked the embedder to enter fullscreen, the guest uses this
  // signal to exit fullscreen state.
  virtual void EmbedderFullscreenToggled(bool entered_fullscreen) {}

  // This method is called when the initial set of frames within the page have
  // completed loading.
  virtual void GuestViewDidStopLoading() {}

  // This method is called when the guest WebContents has been destroyed. This
  // object will be destroyed after this call returns.
  //
  // This gives the derived class an opportunity to perform some cleanup.
  virtual void GuestDestroyed() {}

  // This method is invoked when the guest RenderView is ready, e.g. because we
  // recreated it after a crash or after reattachment.
  //
  // This gives the derived class an opportunity to perform some initialization
  // work.
  virtual void GuestReady() {}

  // This method is called when the guest's zoom changes.
  virtual void GuestZoomChanged(double old_zoom_level, double new_zoom_level) {}

  // This method is invoked when the contents auto-resized to give the container
  // an opportunity to match it if it wishes.
  //
  // This gives the derived class an opportunity to inform its container element
  // or perform other actions.
  virtual void GuestSizeChangedDueToAutoSize(const gfx::Size& old_size,
                                             const gfx::Size& new_size) {}

  // This method is invoked when the contents preferred size changes. This will
  // only ever fire if IsPreferredSizeSupported returns true.
  virtual void OnPreferredSizeChanged(const gfx::Size& pref_size) {}

  // Signals that the guest view is ready.  The default implementation signals
  // immediately, but derived class can override this if they need to do
  // asynchronous setup.
  virtual void SignalWhenReady(const base::Closure& callback);

  // Returns true if this guest should handle find requests for its
  // embedder. This should generally be true for guests that make up the
  // entirety of the embedder's content.
  virtual bool ShouldHandleFindRequestsForEmbedder() const;

  // This method queries whether drag-and-drop is enabled for this particular
  // view. By default, drag-and-drop is disabled. Derived classes can override
  // this behavior to enable drag-and-drop.
  virtual bool IsDragAndDropEnabled() const;

  // This method is called immediately before suspended resource loads have been
  // resumed on attachment to an embedder.
  //
  // This method can be overriden by subclasses. This gives the derived class
  // an opportunity to perform setup actions before attachment.
  virtual void WillAttachToEmbedder() {}

  // This method is called when the guest WebContents is about to be destroyed.
  //
  // This gives the derived class an opportunity to perform some cleanup prior
  // to destruction.
  virtual void WillDestroy() {}

  void LoadURLWithParams(
      const content::NavigationController::LoadURLParams& load_params);

  // Convert sizes in pixels from logical to physical numbers of pixels.
  // Note that a size can consist of a fractional number of logical pixels
  // (hence |logical_pixels| is represented as a double), but will always
  // consist of an integral number of physical pixels (hence the return value
  // is represented as an int).
  int LogicalPixelsToPhysicalPixels(double logical_pixels) const;

  // Convert sizes in pixels from physical to logical numbers of pixels.
  // Note that a size can consist of a fractional number of logical pixels
  // (hence the return value is represented as a double), but will always
  // consist of an integral number of physical pixels (hence |physical_pixels|
  // is represented as an int).
  double PhysicalPixelsToLogicalPixels(int physical_pixels) const;

  void SetGuestZoomLevelToMatchEmbedder();

 private:
  friend class GuestViewMessageFilter;

  class OwnerContentsObserver;
  class OpenerLifetimeObserver;

  // BrowserPluginGuestDelegate implementation.
  content::WebContents* CreateNewGuestWindow(
      const content::WebContents::CreateParams& create_params) final;
  void DidAttach(int guest_proxy_routing_id) final;
  void DidDetach() final;
  content::WebContents* GetOwnerWebContents() const final;
  bool HandleFindForEmbedder(int request_id,
                             const base::string16& search_text,
                             const blink::WebFindOptions& options) final;
  bool HandleStopFindingForEmbedder(content::StopFindAction action) final;
  void GuestSizeChanged(const gfx::Size& new_size) final;
  void SetGuestHost(content::GuestHost* guest_host) final;
  void WillAttach(content::WebContents* embedder_web_contents,
                  int browser_plugin_instance_id,
                  bool is_full_page_plugin,
                  const base::Closure& callback) final;

  // WebContentsDelegate implementation.
  void ActivateContents(content::WebContents* contents) final;
  void ContentsMouseEvent(content::WebContents* source,
                          const gfx::Point& location,
                          bool motion,
                          bool exited) final;
  void ContentsZoomChange(bool zoom_in) final;
  void LoadingStateChanged(content::WebContents* source,
                           bool to_different_document) final;
  content::ColorChooser* OpenColorChooser(
      content::WebContents* web_contents,
      SkColor color,
      const std::vector<content::ColorSuggestion>& suggestions) final;
  void ResizeDueToAutoResize(content::WebContents* web_contents,
                             const gfx::Size& new_size) final;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      const content::FileChooserParams& params) final;
  bool ShouldFocusPageAfterCrash() final;
  void UpdatePreferredSize(content::WebContents* web_contents,
                           const gfx::Size& pref_size) final;
  void UpdateTargetURL(content::WebContents* source, const GURL& url) final;
  bool ShouldResumeRequestsForCreatedWindow() final;

  // WebContentsObserver implementation.
  void DidStopLoading() final;
  void RenderViewReady() final;
  void WebContentsDestroyed() final;

  // ui_zoom::ZoomObserver implementation.
  void OnZoomChanged(
      const zoom::ZoomController::ZoomChangedEventData& data) final;

  void SendQueuedEvents();

  void CompleteInit(std::unique_ptr<base::DictionaryValue> create_params,
                    const WebContentsCreatedCallback& callback,
                    content::WebContents* guest_web_contents);

  // Dispatches the onResize event to the embedder.
  void DispatchOnResizeEvent(const gfx::Size& old_size,
                             const gfx::Size& new_size);

  // Returns the default size of the guestview.
  gfx::Size GetDefaultSize() const;

  // Get the zoom factor for the embedder's web contents.
  double GetEmbedderZoomFactor() const;

  void SetUpSizing(const base::DictionaryValue& params);

  void StartTrackingEmbedderZoomLevel();
  void StopTrackingEmbedderZoomLevel();

  void UpdateGuestSize(const gfx::Size& new_size, bool due_to_auto_resize);

  // This guest tracks the lifetime of the WebContents specified by
  // |owner_web_contents_|. If |owner_web_contents_| is destroyed then this
  // guest will also self-destruct.
  content::WebContents* owner_web_contents_;
  std::string owner_host_;
  content::BrowserContext* const browser_context_;

  // |guest_instance_id_| is a profile-wide unique identifier for a guest
  // WebContents.
  const int guest_instance_id_;

  // |view_instance_id_| is an identifier that's unique within a particular
  // embedder RenderViewHost for a particular <*view> instance.
  int view_instance_id_;

  // |element_instance_id_| is an identifer that's unique to a particular
  // GuestViewContainer element.
  int element_instance_id_;

  // |initialized_| indicates whether GuestViewBase::Init has been called for
  // this object.
  bool initialized_;

  // Indicates that this guest is in the process of being destroyed.
  bool is_being_destroyed_;

  // This is a queue of Events that are destined to be sent to the embedder once
  // the guest is attached to a particular embedder.
  std::deque<std::unique_ptr<GuestViewEvent>> pending_events_;

  // The opener guest view.
  base::WeakPtr<GuestViewBase> opener_;

  // The parameters associated with the element hosting this GuestView that
  // are passed in from JavaScript. This will typically be the view instance ID,
  // and element-specific parameters. These parameters are passed along to new
  // guests that are created from this guest.
  std::unique_ptr<base::DictionaryValue> attach_params_;

  // This observer ensures that this guest self-destructs if the embedder goes
  // away.
  std::unique_ptr<OwnerContentsObserver> owner_contents_observer_;

  // This observer ensures that if the guest is unattached and its opener goes
  // away then this guest also self-destructs.
  std::unique_ptr<OpenerLifetimeObserver> opener_lifetime_observer_;

  // The size of the guest content. Note: In autosize mode, the container
  // element may not match the size of the guest.
  gfx::Size guest_size_;

  // A pointer to the guest_host.
  content::GuestHost* guest_host_;

  // Indicates whether autosize mode is enabled or not.
  bool auto_size_enabled_;

  // The maximum size constraints of the container element in autosize mode.
  gfx::Size max_auto_size_;

  // The minimum size constraints of the container element in autosize mode.
  gfx::Size min_auto_size_;

  // The size that will be used when autosize mode is disabled.
  gfx::Size normal_size_;

  // Whether the guest view is inside a plugin document.
  bool is_full_page_plugin_;

  // The routing ID of the proxy to the guest in the owner's renderer process.
  int guest_proxy_routing_id_;

  // This is used to ensure pending tasks will not fire after this object is
  // destroyed.
  base::WeakPtrFactory<GuestViewBase> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(GuestViewBase);
};

}  // namespace guest_view

#endif  // COMPONENTS_GUEST_VIEW_BROWSER_GUEST_VIEW_BASE_H_
