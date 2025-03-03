// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_MOCK_INPUT_METHOD_H_
#define UI_BASE_IME_MOCK_INPUT_METHOD_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/input_method_observer.h"
#include "ui/base/ime/ui_base_ime_export.h"

namespace ui {

class KeyEvent;
class TextInputClient;

// A mock ui::InputMethod implementation for testing. You can get the instance
// of this class as the global input method with calling
// SetUpInputMethodFactoryForTesting() which is declared in
// ui/base/ime/input_method_factory.h
class UI_BASE_IME_EXPORT MockInputMethod
    : NON_EXPORTED_BASE(public InputMethod) {
 public:
  explicit MockInputMethod(internal::InputMethodDelegate* delegate);
  ~MockInputMethod() override;

  // Overriden from InputMethod.
  void SetDelegate(internal::InputMethodDelegate* delegate) override;
  void OnFocus() override;
  void OnBlur() override;
  bool OnUntranslatedIMEMessage(const base::NativeEvent& event,
                                NativeEventResult* result) override;
  void SetFocusedTextInputClient(TextInputClient* client) override;
  void DetachTextInputClient(TextInputClient* client) override;
  TextInputClient* GetTextInputClient() const override;
  void DispatchKeyEvent(ui::KeyEvent* event) override;
  void OnTextInputTypeChanged(const TextInputClient* client) override;
  void OnCaretBoundsChanged(const TextInputClient* client) override;
  void CancelComposition(const TextInputClient* client) override;
  void OnInputLocaleChanged() override;
  std::string GetInputLocale() override;
  TextInputType GetTextInputType() const override;
  TextInputMode GetTextInputMode() const override;
  int GetTextInputFlags() const override;
  bool CanComposeInline() const override;
  bool IsCandidatePopupOpen() const override;
  void ShowImeIfNeeded() override;
  void AddObserver(InputMethodObserver* observer) override;
  void RemoveObserver(InputMethodObserver* observer) override;
  void SetImeEnabled(bool enable) override {}
  gfx::Rect GetInputPanelRect() const override;
  gfx::Rect GetCaretBounds() const override;
  bool IsVisible() const override;
  bool IsInputMethodActive() const override;

 private:
  // InputMethod:
  const std::vector<std::unique_ptr<ui::KeyEvent>>& GetKeyEventsForTesting()
      override;

  TextInputClient* text_input_client_;
  base::ObserverList<InputMethodObserver> observer_list_;
  internal::InputMethodDelegate* delegate_;

  std::vector<std::unique_ptr<ui::KeyEvent>> key_events_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(MockInputMethod);
};

}  // namespace ui

#endif  // UI_BASE_IME_MOCK_INPUT_METHOD_H_
