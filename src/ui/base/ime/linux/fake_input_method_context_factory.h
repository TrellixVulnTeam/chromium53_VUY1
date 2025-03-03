// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_LINUX_FAKE_INPUT_METHOD_CONTEXT_FACTORY_H_
#define UI_BASE_IME_LINUX_FAKE_INPUT_METHOD_CONTEXT_FACTORY_H_

#include "base/macros.h"
#include "ui/base/ime/linux/linux_input_method_context_factory.h"

namespace ui {

// An implementation of LinuxInputMethodContextFactory, which creates and
// returns FakeInputMethodContext's.
class FakeInputMethodContextFactory : public LinuxInputMethodContextFactory {
 public:
  FakeInputMethodContextFactory();

  // LinuxInputMethodContextFactory:
  std::unique_ptr<LinuxInputMethodContext> CreateInputMethodContext(
#if defined(USE_OZONE)
      gfx::AcceleratedWidget widget,
#endif
      LinuxInputMethodContextDelegate* delegate,
      bool is_simple) const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeInputMethodContextFactory);
};

}  // namespace ui

#endif  // UI_BASE_IME_LINUX_FAKE_INPUT_METHOD_CONTEXT_FACTORY_H_
