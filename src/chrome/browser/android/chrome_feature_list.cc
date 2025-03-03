// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/chrome_feature_list.h"

#include <stddef.h>

#include <string>

#include "base/android/jni_string.h"
#include "base/feature_list.h"
#include "base/macros.h"
#include "chrome/common/chrome_features.h"
#include "components/offline_pages/offline_page_feature.h"
#include "content/public/common/content_features.h"
#include "jni/ChromeFeatureList_jni.h"

using base::android::ConvertJavaStringToUTF8;

namespace chrome {
namespace android {

namespace {

// Array of features exposed through the Java ChromeFeatureList API. Entries in
// this array may either refer to features defined in the header of this file or
// in other locations in the code base (e.g. chrome/, components/, etc).
const base::Feature* kFeaturesExposedToJava[] = {
    &features::kAutoplayMutedVideos,
    &features::kCredentialManagementAPI,
    &features::kSimplifiedFullscreenUI,
    &features::kWebPayments,
    &kAndroidPayIntegrationV1,
    &kImportantSitesInCBD,
    &kMediaStyleNotification,
    &kNTPFakeOmniboxTextFeature,
    &kNTPMaterialDesign,
    &kNTPOfflinePagesFeature,
    &kNTPSnippetsFeature,
    &kNTPToolbarFeature,
    &kPhysicalWebFeature,
    &kPhysicalWebIgnoreOtherClientsFeature,
    &kReadItLaterInMenu,
    &kSystemDownloadManager,
    &offline_pages::kOfflinePagesBackgroundLoadingFeature,
    &offline_pages::kOfflinePagesCTFeature,  // See crbug.com/620421.
};

}  // namespace

const base::Feature kImportantSitesInCBD{"ImportantSitesInCBD",
                                         base::FEATURE_DISABLED_BY_DEFAULT};

const base::Feature kMediaStyleNotification {
  "MediaStyleNotification", base::FEATURE_ENABLED_BY_DEFAULT
};

const base::Feature kNTPMaterialDesign{"NTPMaterialDesign",
                                       base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kNTPOfflinePagesFeature {
  "NTPOfflinePages", base::FEATURE_DISABLED_BY_DEFAULT
};

const base::Feature kNTPSnippetsFeature {
  "NTPSnippets", base::FEATURE_DISABLED_BY_DEFAULT
};

const base::Feature kNTPToolbarFeature {
  "NTPToolbar", base::FEATURE_ENABLED_BY_DEFAULT
};

const base::Feature kNTPFakeOmniboxTextFeature {
  "NTPFakeOmniboxText", base::FEATURE_DISABLED_BY_DEFAULT
};

const base::Feature kAndroidPayIntegrationV1 {
  "AndroidPayIntegrationV1", base::FEATURE_ENABLED_BY_DEFAULT
};

const base::Feature kPhysicalWebFeature {
  "PhysicalWeb", base::FEATURE_ENABLED_BY_DEFAULT
};

const base::Feature kPhysicalWebIgnoreOtherClientsFeature {
  "PhysicalWebIgnoreOtherClients", base::FEATURE_DISABLED_BY_DEFAULT
};

const base::Feature kReadItLaterInMenu {
  "ReadItLaterInMenu", base::FEATURE_DISABLED_BY_DEFAULT
};

const base::Feature kSystemDownloadManager {
  "SystemDownloadManager", base::FEATURE_ENABLED_BY_DEFAULT
};

static jboolean IsEnabled(JNIEnv* env,
                          const JavaParamRef<jclass>& clazz,
                          const JavaParamRef<jstring>& jfeature_name) {
  const std::string feature_name = ConvertJavaStringToUTF8(env, jfeature_name);
  for (size_t i = 0; i < arraysize(kFeaturesExposedToJava); ++i) {
    if (kFeaturesExposedToJava[i]->name == feature_name)
      return base::FeatureList::IsEnabled(*kFeaturesExposedToJava[i]);
  }
  // Features queried via this API must be present in |kFeaturesExposedToJava|.
  NOTREACHED();
  return false;
}

bool RegisterChromeFeatureListJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

}  // namespace android
}  // namespace chrome
