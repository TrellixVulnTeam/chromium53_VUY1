// Copyright (c) 2017-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_OZONE_H_
#define UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_OZONE_H_

#include <list>
#include <set>
#include <string>
#include <vector>

#include "ui/aura/window_tree_host.h"
#include "ui/platform_window/platform_window_delegate.h"
#include "ui/platform_window/webos/webos_xinput_types.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host.h"

#if defined(OS_WEBOS)
class SkBitmap;
#endif

namespace ui {
class PlatformWindow;
class WindowManagerWayland;

#if defined(OS_WEBOS)
class WindowGroupConfiguration;
#endif
}

namespace views {

namespace corewm {
class Tooltip;
}

class DesktopDragDropClientWayland;

class VIEWS_EXPORT DesktopWindowTreeHostOzone
    : public DesktopWindowTreeHost,
      public aura::WindowTreeHost,
      public ui::PlatformWindowDelegate {
 public:
  DesktopWindowTreeHostOzone(
      internal::NativeWidgetDelegate* native_widget_delegate,
      DesktopNativeWidgetAura* desktop_native_widget_aura);
  ~DesktopWindowTreeHostOzone() override;

  // Accepts a opaque handle widget and returns associated aura::Window.
  static aura::Window* GetContentWindowForAcceleratedWidget(
      gfx::AcceleratedWidget widget);

  // Accepts a opaque handle widget and returns associated
  // DesktopWindowTreeHostOzone.
  static DesktopWindowTreeHostOzone* GetHostForAcceleratedWidget(
      gfx::AcceleratedWidget widget);

  // Get all open top-level windows. This includes windows that may not be
  // visible. This list is sorted in their stacking order, i.e. the first window
  // is the topmost window.
  static const std::vector<aura::Window*>& GetAllOpenWindows();

  // Deallocates the internal list of open windows.
  static void CleanUpWindowList();

  // Returns window bounds. This is used by Screen to determine if a point
  // belongs to a particular window.
  gfx::Rect GetBoundsInScreen() const;

  views::DesktopNativeWidgetAura* desktop_native_widget_aura() const {
    return desktop_native_widget_aura_;
  }

#if defined(OS_WEBOS)
  // Overridden from DesktopWindowTreeHostWebOS
  ui::InputMethod* GetInputMethod() override;
  unsigned GetWindowHandle() override;
  void AddPreTargetHandler(ui::EventHandler* handler) override;
  void SetCustomCursor(webos::CustomCursorType type,
                       const std::string& path,
                       int hotspot_x,
                       int hotspot_y) override;
  void SetPlatformCursor(const SkBitmap* cursor_bitmap,
                         const gfx::Point& location) override;
  void SetKeyMask(webos::WebOSKeyMask key_mask, bool value) override;
  void SetWindowHostState(ui::WidgetState state) override;
  void SetWindowProperty(const std::string& name,
                         const std::string& value) override;
  void ResetCompositor() override;
  void Resize(int width, int height) override;
  void WebAppWindowHidden() override;
  void CreateGroup(const ui::WindowGroupConfiguration& config) override;
  void AttachToGroup(const std::string& name,
                     const std::string& layer) override;
  void FocusGroupOwner() override;
  void FocusGroupLayer() override;
  void DetachGroup() override;
  void WebOSXInputActivate(const std::string& type) override;
  void WebOSXInputDeactivate() override;
  void WebOSXInputInvokeAction(uint32_t keysym,
                               ui::WebOSXInputSpecialKeySymbolType symbol_type,
                               ui::WebOSXInputEventType event_type) override;
#endif

 protected:
  void ApplyCompositorVisible();
  // Overridden from DesktopWindowTreeHost:
  void Init(aura::Window* content_window,
            const views::Widget::InitParams& params) override;
  void OnNativeWidgetCreated(const views::Widget::InitParams& params) override;
  std::unique_ptr<corewm::Tooltip> CreateTooltip() override;
  std::unique_ptr<aura::client::DragDropClient> CreateDragDropClient(
      DesktopNativeCursorManager* cursor_manager) override;
  void Close() override;
  void CloseNow() override;
  aura::WindowTreeHost* AsWindowTreeHost() override;
  void ShowWindowWithState(ui::WindowShowState show_state) override;
  void ShowMaximizedWithBounds(const gfx::Rect& restored_bounds) override;
  bool IsVisible() const override;
  void SetSize(const gfx::Size& size) override;
  void StackAbove(aura::Window* window) override;
  void StackAtTop() override;
  void CenterWindow(const gfx::Size& size) override;
  void GetWindowPlacement(gfx::Rect* bounds,
                          ui::WindowShowState* show_state) const override;
  gfx::Rect GetWindowBoundsInScreen() const override;
  gfx::Rect GetClientAreaBoundsInScreen() const override;
  gfx::Rect GetRestoredBounds() const override;
  std::string GetWorkspace() const override;
  gfx::Rect GetWorkAreaBoundsInScreen() const override;
  void SetShape(SkRegion* native_region) override;
  void Activate() override;
  void Deactivate() override;
  bool IsActive() const override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  bool IsMaximized() const override;
  bool IsMinimized() const override;
  bool HasCapture() const override;
  void SetAlwaysOnTop(bool always_on_top) override;
  bool IsAlwaysOnTop() const override;
  void SetVisibleOnAllWorkspaces(bool always_visible) override;
  bool SetWindowTitle(const base::string16& title) override;
  void ClearNativeFocus() override;
  views::Widget::MoveLoopResult RunMoveLoop(
      const gfx::Vector2d& drag_offset,
      views::Widget::MoveLoopSource source,
      views::Widget::MoveLoopEscapeBehavior escape_behavior) override;
  void EndMoveLoop() override;
  void SetVisibilityChangedAnimationsEnabled(bool value) override;
  bool ShouldUseNativeFrame() const override;
  bool ShouldWindowContentsBeTransparent() const override;
  void FrameTypeChanged() override;
  void SetFullscreen(bool fullscreen) override;
  bool IsFullscreen() const override;
  void SetOpacity(float opacity) override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void InitModalType(ui::ModalType modal_type) override;
  void FlashFrame(bool flash_frame) override;
  void OnRootViewLayout() override;
  void OnNativeWidgetFocus() override;
  void OnNativeWidgetBlur() override;
  bool IsAnimatingClosed() const override;
  bool IsTranslucentWindowOpacitySupported() const override;
  void SizeConstraintsChanged() override;

  // Overridden from aura::WindowTreeHost:
  gfx::Transform GetRootTransform() const override;
  ui::EventSource* GetEventSource() override;
  gfx::AcceleratedWidget GetAcceleratedWidget() override;
  void ShowImpl() override;
  void HideImpl() override;
  gfx::Rect GetBounds() const override;
  void SetBounds(const gfx::Rect& bounds) override;
  gfx::Point GetLocationOnNativeScreen() const override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void SetCursorNative(gfx::NativeCursor cursor) override;
  void MoveCursorToNative(const gfx::Point& location) override;
  void OnCursorVisibilityChangedNative(bool show) override;

  // Overridden from ui::PlatformWindowDelegate:
  void OnBoundsChanged(const gfx::Rect&) override;
  void OnDamageRect(const gfx::Rect& damaged_region) override;
  void DispatchEvent(ui::Event* event) override;
  void OnCloseRequest() override;
  void OnClosed() override;
  void OnWindowStateChanged(ui::PlatformWindowState new_state) override;
  void OnLostCapture() override;
  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget,
                                    float device_pixel_ratio) override;
  void OnAcceleratedWidgetDestroyed() override;
  void OnActivationChanged(bool active) override;
  void OnDragEnter(unsigned windowhandle,
                   float x,
                   float y,
                   const std::vector<std::string>& mime_types,
                   uint32_t serial) override;
  void OnDragDataReceived(int fd) override;
  void OnDragLeave() override;
  void OnDragMotion(float x, float y, uint32_t time) override;
  void OnDragDrop() override;

   gfx::AcceleratedWidget GetWindow() const { return window_; }
  void SetWindow(gfx::AcceleratedWidget window) { window_ = window; }
  void SetContentWindow(aura::Window* content_window) {
    content_window_ = content_window;
  }

  enum {
    Uninitialized = 0x00,
    Visible = 0x01,  // Window is Visible.
    FullScreen = 0x02,  // Window is in fullscreen mode.
    Maximized = 0x04,  // Window is maximized,
    Minimized = 0x08,  // Window is minimized.
    Active = 0x10  // Window is Active.
  };

  // Platform-specific part of this DesktopWindowTreeHost.
  std::unique_ptr<ui::PlatformWindow> platform_window_;

 private:
  typedef unsigned RootWindowState;

  // Initializes our Ozone surface to draw on. This method performs all
  // initialization related to talking to the Ozone server.
  void InitOzoneWindow(const views::Widget::InitParams& params);

  void Relayout();
  gfx::Size AdjustSize(const gfx::Size& requested_size);
  void ShowWindow();

  static std::list<gfx::AcceleratedWidget>& open_windows();
  gfx::Rect ToDIPRect(const gfx::Rect& rect_in_pixels) const;
  gfx::Rect ToPixelRect(const gfx::Rect& rect_in_dip) const;
  void ResetWindowRegion();

  RootWindowState state_;
  bool has_capture_;
  bool custom_window_shape_;
  bool always_on_top_;

  // Original bounds of DRWH.
  gfx::Rect previous_bounds_;
  gfx::Rect previous_maximize_bounds_;
  gfx::AcceleratedWidget window_;
  base::string16 title_;

#if defined(OS_WEBOS)
  ui::EventHandler* event_handler_;
  gfx::Size contents_size_;
  ui::PlatformCursor platform_cursor_;
#endif

  // Owned by DesktopNativeWidgetAura.
  DesktopDragDropClientWayland* drag_drop_client_;
  views::internal::NativeWidgetDelegate* native_widget_delegate_;
  aura::Window* content_window_;

  views::DesktopNativeWidgetAura* desktop_native_widget_aura_;
  // We can optionally have a parent which can order us to close, or own
  // children who we're responsible for closing when we CloseNow().
  DesktopWindowTreeHostOzone* window_parent_;
  std::set<DesktopWindowTreeHostOzone*> window_children_;

  // Platform-specific part of this DesktopWindowTreeHost.
  base::WeakPtrFactory<DesktopWindowTreeHostOzone> close_widget_factory_;

  // A list of all (top-level) windows that have been created but not yet
  // destroyed.
  static std::list<gfx::AcceleratedWidget>* open_windows_;
  // List of all open aura::Window.
  static std::vector<aura::Window*>* aura_windows_;
  DISALLOW_COPY_AND_ASSIGN(DesktopWindowTreeHostOzone);
};

}  // namespace views

#endif  // UI_VIEWS_WIDGET_DESKTOP_AURA_DESKTOP_WINDOW_TREE_HOST_OZONE_H_
