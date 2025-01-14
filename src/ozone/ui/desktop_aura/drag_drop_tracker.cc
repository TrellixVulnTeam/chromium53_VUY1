// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Copyright (c) 2015 LG Electronics, Inc.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is taken from "ash/drag_drop/drag_drop_tracker.h"

#include "ozone/ui/desktop_aura/drag_drop_tracker.h"

#include "ui/aura/env.h"
#include "ui/aura/client/window_tree_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/display/screen.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/public/activation_delegate.h"

namespace views {
namespace {

// An activation delegate which disables activating the drag and drop window.
class CaptureWindowActivationDelegate
    : public aura::client::ActivationDelegate {
 public:
  CaptureWindowActivationDelegate() {}
  virtual ~CaptureWindowActivationDelegate() {}

  // aura::client::ActivationDelegate overrides:
  bool ShouldActivate() const override {
    return false;
  }

 private:

  DISALLOW_COPY_AND_ASSIGN(CaptureWindowActivationDelegate);
};

// Creates a window for capturing drag events.
aura::Window* CreateCaptureWindow(aura::Window* context_root,
                                  aura::WindowDelegate* delegate) {
  static CaptureWindowActivationDelegate* activation_delegate_instance = NULL;
  if (!activation_delegate_instance)
    activation_delegate_instance = new CaptureWindowActivationDelegate;
  aura::Window* window = new aura::Window(delegate);
  window->SetType(ui::wm::WINDOW_TYPE_NORMAL);
  window->Init(ui::LAYER_NOT_DRAWN);
  aura::client::ParentWindowWithContext(window, context_root, gfx::Rect());
  aura::client::SetActivationDelegate(window, activation_delegate_instance);
  window->Show();
  DCHECK(window->bounds().size().IsEmpty());
  return window;
}

}  // namespace

DragDropTracker::DragDropTracker(aura::Window* context_root,
                                 aura::WindowDelegate* delegate)
    : capture_window_(CreateCaptureWindow(context_root, delegate)) {
}

DragDropTracker::~DragDropTracker()  {
  capture_window_->ReleaseCapture();
}

void DragDropTracker::TakeCapture() {
  capture_window_->SetCapture();
}

aura::Window* DragDropTracker::GetTarget(const ui::LocatedEvent& event) {
  DCHECK(capture_window_.get());
  gfx::Point location_in_screen = event.location();
  ::wm::ConvertPointToScreen(capture_window_.get(), &location_in_screen);

  aura::Window* root_window = aura::Env::GetRootWindow();
  gfx::Point location_in_root = location_in_screen;
  ::wm::ConvertPointFromScreen(root_window, &location_in_root);
  return root_window->GetEventHandlerForPoint(location_in_root);
}

ui::LocatedEvent* DragDropTracker::ConvertEvent(
    aura::Window* target,
    const ui::LocatedEvent& event) {
  DCHECK(capture_window_.get());
  gfx::Point target_location = event.location();
  aura::Window::ConvertPointToTarget(capture_window_.get(), target,
                                     &target_location);
  gfx::Point location_in_screen = event.location();
  ::wm::ConvertPointToScreen(capture_window_.get(), &location_in_screen);
  gfx::Point target_root_location = event.root_location();
  aura::Window::ConvertPointToTarget(
      capture_window_->GetRootWindow(),
      aura::Env::GetRootWindow(), &target_root_location);
  return new ui::MouseEvent(event.type(), target_location,
      target_root_location, ui::EventTimeForNow(), event.flags(),
      static_cast<const ui::MouseEvent&>(event).changed_button_flags());
}

}  // namespace views
