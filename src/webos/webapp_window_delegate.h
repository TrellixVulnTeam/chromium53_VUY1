// Copyright (c) 2016-2018 LG Electronics, Inc.
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

#ifndef WEBOS_WEBAPP_WINDOW_DELEGATE_H_
#define WEBOS_WEBAPP_WINDOW_DELEGATE_H_

#include "webos/common/webos_event.h"

class WebAppWindowDelegate {
 public:
  virtual ~WebAppWindowDelegate() {}

  // This is only for wam, so it follows wam's style.
  virtual bool event(WebOSEvent* e) { return false; }
  virtual unsigned CheckKeyFilterTable(unsigned keycode, unsigned* modifier) { return 0; }
};

#endif  // WEBOS_WEBAPP_WINDOW_DELEGATE_H_
