/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SettingsDelegate_h
#define SettingsDelegate_h

#include "core/CoreExport.h"
#include "wtf/Allocator.h"
#include <memory>

namespace blink {

class Settings;

class CORE_EXPORT SettingsDelegate {
    DISALLOW_NEW();
public:
    explicit SettingsDelegate(std::unique_ptr<Settings>);
    virtual ~SettingsDelegate();

    Settings* settings() const { return m_settings.get(); }

    // We currently use an enum instead of individual invalidation
    // functions to make generating Settings.in slightly easier.
    enum ChangeType {
        StyleChange,
        ViewportDescriptionChange,
        ViewportRuleChange,
        DNSPrefetchingChange,
        ImageLoadingChange,
        TextAutosizingChange,
        FontFamilyChange,
        AcceleratedCompositingChange,
        MediaQueryChange,
        AccessibilityStateChange,
        TextTrackKindUserPreferenceChange,
        LocalResourceLoadChange
    };

    virtual void settingsChanged(ChangeType) = 0;

protected:
    std::unique_ptr<Settings> const m_settings;
};

} // namespace blink

#endif // SettingsDelegate_h
