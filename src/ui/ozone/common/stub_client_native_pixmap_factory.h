// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_COMMON_STUB_CLIENT_NATIVE_PIXMAP_FACTORY_H_
#define UI_OZONE_COMMON_STUB_CLIENT_NATIVE_PIXMAP_FACTORY_H_

#include "ui/ozone/ozone_export.h"
#include "ui/ozone/public/client_native_pixmap_factory.h"

namespace ui {

// Platforms which don't need to share native pixmap use this.
// The caller takes ownership of the instance.
OZONE_EXPORT ClientNativePixmapFactory* CreateStubClientNativePixmapFactory();

}  // namespace ui

#endif  // UI_OZONE_COMMON_STUB_CLIENT_NATIVE_PIXMAP_FACTORY_H_
