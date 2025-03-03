// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_INPUT_METHOD_H_
#define UI_BASE_IME_INPUT_METHOD_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/event_types.h"
#include "base/memory/scoped_vector.h"
#include "build/build_config.h"
#include "ui/base/ime/text_input_mode.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/rect.h"

namespace extensions {
class InputImeApiTest;
}  // namespace extensions

namespace ui {

namespace internal {
class InputMethodDelegate;
}  // namespace internal

class InputMethodObserver;
class KeyEvent;
class TextInputClient;

// An interface implemented by an object that encapsulates a native input method
// service provided by the underlying operating system, and acts as a "system
// wide" input method for all Chrome windows. A class that implements this
// interface should behave as follows:
// - Receives a keyboard event directly from a message dispatcher for the
//   system through the InputMethod::DispatchKeyEvent API, and forwards it to
//   an underlying input method for the OS.
// - The input method should handle the key event either of the following ways:
//   1) Send the original key down event to the focused window, which is e.g.
//      a NativeWidgetAura (NWA) or a RenderWidgetHostViewAura (RWHVA), using
//      internal::InputMethodDelegate::DispatchKeyEventPostIME API, then send
//      a Char event using TextInputClient::InsertChar API to a text input
//      client, which is, again, e.g. NWA or RWHVA, and then send the original
//      key up event to the same window.
//   2) Send VKEY_PROCESSKEY event to the window using the DispatchKeyEvent API,
//      then update IME status (e.g. composition text) using TextInputClient,
//      and then send the original key up event to the window.
// - Keeps track of the focused TextInputClient to see which client can call
//   APIs, OnTextInputTypeChanged, OnCaretBoundsChanged, and CancelComposition,
//   that change the state of the input method.
// In Aura environment, aura::WindowTreeHost creates an instance of
// ui::InputMethod and owns it.
class InputMethod {
 public:
  InputMethod() : track_key_events_for_testing_(false) {}

#if defined(OS_WIN)
  typedef LRESULT NativeEventResult;
#else
  typedef int32_t NativeEventResult;
#endif

  virtual ~InputMethod() {}

  // Sets the delegate used by this InputMethod instance. It should only be
  // called by an object which manages the whole UI.
  virtual void SetDelegate(internal::InputMethodDelegate* delegate) = 0;

  // Called when the top-level system window gets keyboard focus.
  virtual void OnFocus() = 0;

  // Called when the top-level system window loses keyboard focus.
  virtual void OnBlur() = 0;

  // Called when the focused window receives native IME messages that are not
  // translated into other predefined event callbacks. Currently this method is
  // used only for IME functionalities specific to Windows.
  // TODO(ime): Break down these messages into platform-neutral methods.
  virtual bool OnUntranslatedIMEMessage(const base::NativeEvent& event,
                                        NativeEventResult* result) = 0;

  // Sets the text input client which receives text input events such as
  // SetCompositionText(). |client| can be NULL. A gfx::NativeWindow which
  // implementes TextInputClient interface, e.g. NWA and RWHVA, should register
  // itself by calling the method when it is focused, and unregister itself by
  // calling the method with NULL when it is unfocused.
  virtual void SetFocusedTextInputClient(TextInputClient* client) = 0;

  // Detaches and forgets the |client| regardless of whether it has the focus or
  // not.  This method is meant to be called when the |client| is going to be
  // destroyed.
  virtual void DetachTextInputClient(TextInputClient* client) = 0;

  // Gets the current text input client. Returns NULL when no client is set.
  virtual TextInputClient* GetTextInputClient() const = 0;

  // Dispatches a key event to the input method. The key event will be
  // dispatched back to the caller via
  // ui::InputMethodDelegate::DispatchKeyEventPostIME(), once it's processed by
  // the input method. It should only be called by a message dispatcher.
  virtual void DispatchKeyEvent(ui::KeyEvent* event) = 0;

  // Called by the focused client whenever its text input type is changed.
  // Before calling this method, the focused client must confirm or clear
  // existing composition text and call InputMethod::CancelComposition() when
  // necessary. Otherwise unexpected behavior may happen. This method has no
  // effect if the client is not the focused client.
  virtual void OnTextInputTypeChanged(const TextInputClient* client) = 0;

  // Called by the focused client whenever its caret bounds is changed.
  // This method has no effect if the client is not the focused client.
  virtual void OnCaretBoundsChanged(const TextInputClient* client) = 0;

  // Called by the focused client to ask the input method cancel the ongoing
  // composition session. This method has no effect if the client is not the
  // focused client.
  virtual void CancelComposition(const TextInputClient* client) = 0;

  // Called by the focused client whenever its input locale is changed.
  // This method is currently used only on Windows.
  // This method does not take a parameter of TextInputClient for historical
  // reasons.
  // TODO(ime): Consider to take a parameter of TextInputClient.
  virtual void OnInputLocaleChanged() = 0;

  // Returns the locale of current keyboard layout or input method, as a BCP-47
  // tag, or an empty string if the input method cannot provide it.
  virtual std::string GetInputLocale() = 0;

  // TODO(yoichio): Following 3 methods(GetTextInputType, GetTextInputMode and
  // CanComposeInline) calls client's same method and returns its value. It is
  // not InputMethod itself's infomation. So rename these to
  // GetClientTextInputType and so on.
  // Gets the text input type of the focused text input client. Returns
  // ui::TEXT_INPUT_TYPE_NONE if there is no focused client.
  virtual TextInputType GetTextInputType() const = 0;

  // Gets the text input mode of the focused text input client. Returns
  // ui::TEXT_INPUT_TYPE_DEFAULT if there is no focused client.
  virtual TextInputMode GetTextInputMode() const = 0;

  // Gets the text input flags of the focused text input client. Returns
  // 0 if there is no focused client.
  virtual int GetTextInputFlags() const = 0;

  // Checks if the focused text input client supports inline composition.
  virtual bool CanComposeInline() const = 0;

  // Returns true if we know for sure that a candidate window (or IME suggest,
  // etc.) is open.  Returns false if no popup window is open or the detection
  // of IME popups is not supported.
  virtual bool IsCandidatePopupOpen() const = 0;

  // Displays an on screen keyboard if enabled.
  virtual void ShowImeIfNeeded() = 0;

  // Hides VKB. Introduced for WebOS VKB
  virtual void HideIme(ui::ImeHiddenType hidden_type) {}

  virtual void SetImeEnabled(bool enable) {}
  virtual gfx::Rect GetInputPanelRect() const = 0;
  virtual gfx::Rect GetCaretBounds() const = 0;
  virtual bool IsVisible() const = 0;
  virtual bool IsInputMethodActive() const = 0;

  // Management of the observer list.
  virtual void AddObserver(InputMethodObserver* observer) = 0;
  virtual void RemoveObserver(InputMethodObserver* observer) = 0;

 protected:
  friend class extensions::InputImeApiTest;

  // Gets the tracked key events of using input.ime.sendKeyEvents API.
  virtual const std::vector<std::unique_ptr<ui::KeyEvent>>&
  GetKeyEventsForTesting() = 0;

  // Whether the key events will be tracked. Only used for testing.
  bool track_key_events_for_testing_;
};

}  // namespace ui

#endif  // UI_BASE_IME_INPUT_METHOD_H_
