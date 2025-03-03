// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/browser_commands.h"

#include "base/command_line.h"
#include "base/metrics/histogram.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover.h"
#include "chrome/browser/browsing_data/browsing_data_remover_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/dom_distiller/tab_utils.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/translate/chrome_translate_client.h"
#include "chrome/browser/ui/accelerator_utils.h"
#include "chrome/browser/ui/autofill/save_card_bubble_controller_impl.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils.h"
#include "chrome/browser/ui/bookmarks/bookmark_utils_desktop.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_instant_controller.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/find_bar/find_bar.h"
#include "chrome/browser/ui/find_bar/find_bar_controller.h"
#include "chrome/browser/ui/find_bar/find_tab_helper.h"
#include "chrome/browser/ui/location_bar/location_bar.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "chrome/browser/ui/status_bubble.h"
#include "chrome/browser/ui/tab_contents/core_tab_helper.h"
#include "chrome/browser/ui/tab_dialogs.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/content_restriction.h"
#include "chrome/common/pref_names.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/bookmarks/common/bookmark_pref_names.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/google/core/browser/google_util.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/core/live_tab_context.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/signin/core/browser/signin_header_helper.h"
#include "components/translate/core/browser/language_state.h"
#include "components/version_info/version_info.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/page_state.h"
#include "content/public/common/renderer_preferences.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "content/public/common/user_agent.h"
#include "net/base/escape.h"
#include "ui/events/keycodes/keyboard_codes.h"

#if defined(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/api/commands/command_service.h"
#include "chrome/browser/extensions/api/extension_action/extension_action_api.h"
#include "chrome/browser/extensions/tab_helper.h"
#include "chrome/browser/ui/extensions/settings_api_bubble_helpers.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/common/extensions/extension_metrics.h"
#include "chrome/common/extensions/manifest_handlers/app_launch_info.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#endif

#if defined(ENABLE_PRINTING)
#include "chrome/browser/printing/print_view_manager_common.h"
#if defined(ENABLE_PRINT_PREVIEW)
#include "chrome/browser/printing/print_preview_dialog_controller.h"
#endif  // defined(ENABLE_PRINT_PREVIEW)
#endif  // defined(ENABLE_PRINTING)

#if defined(ENABLE_RLZ)
#include "components/rlz/rlz_tracker.h"
#endif

#if defined(ENABLE_MEDIA_ROUTER)
#include "chrome/browser/media/router/media_router_dialog_controller.h"
#endif

namespace {
const char kOsOverrideForTabletSite[] = "Linux; Android 4.0.3";
}

using base::UserMetricsAction;
using bookmarks::BookmarkModel;
using content::NavigationController;
using content::NavigationEntry;
using content::OpenURLParams;
using content::Referrer;
using content::WebContents;

namespace chrome {
namespace {

bool CanBookmarkCurrentPageInternal(const Browser* browser,
                                    bool check_remove_bookmark_ui) {
  BookmarkModel* model =
      BookmarkModelFactory::GetForProfile(browser->profile());
  return browser_defaults::bookmarks_enabled &&
      browser->profile()->GetPrefs()->GetBoolean(
          bookmarks::prefs::kEditBookmarksEnabled) &&
      model && model->loaded() && browser->is_type_tabbed() &&
      (!check_remove_bookmark_ui ||
           !chrome::ShouldRemoveBookmarkThisPageUI(browser->profile()));
}

#if defined(ENABLE_EXTENSIONS)
bool GetBookmarkOverrideCommand(
    Profile* profile,
    const extensions::Extension** extension,
    extensions::Command* command,
    extensions::CommandService::ExtensionCommandType* command_type) {
  DCHECK(extension);
  DCHECK(command);
  DCHECK(command_type);

  ui::Accelerator bookmark_page_accelerator =
      chrome::GetPrimaryChromeAcceleratorForCommandId(IDC_BOOKMARK_PAGE);
  if (bookmark_page_accelerator.key_code() == ui::VKEY_UNKNOWN)
    return false;

  extensions::CommandService* command_service =
      extensions::CommandService::Get(profile);
  const extensions::ExtensionSet& extension_set =
      extensions::ExtensionRegistry::Get(profile)->enabled_extensions();
  for (extensions::ExtensionSet::const_iterator i = extension_set.begin();
       i != extension_set.end();
       ++i) {
    extensions::Command prospective_command;
    extensions::CommandService::ExtensionCommandType prospective_command_type;
    if (command_service->GetSuggestedExtensionCommand(
            (*i)->id(), bookmark_page_accelerator, &prospective_command,
            &prospective_command_type)) {
      *extension = i->get();
      *command = prospective_command;
      *command_type = prospective_command_type;
      return true;
    }
  }
  return false;
}
#endif

// Based on |disposition|, creates a new tab as necessary, and returns the
// appropriate tab to navigate.  If that tab is the current tab, reverts the
// location bar contents, since all browser-UI-triggered navigations should
// revert any omnibox edits in the current tab.
WebContents* GetTabAndRevertIfNecessary(Browser* browser,
                                        WindowOpenDisposition disposition) {
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  switch (disposition) {
    case NEW_FOREGROUND_TAB:
    case NEW_BACKGROUND_TAB: {
      WebContents* new_tab = current_tab->Clone();
      if (disposition == NEW_BACKGROUND_TAB)
        new_tab->WasHidden();
      browser->tab_strip_model()->AddWebContents(
          new_tab, -1, ui::PAGE_TRANSITION_LINK,
          (disposition == NEW_FOREGROUND_TAB) ?
              TabStripModel::ADD_ACTIVE : TabStripModel::ADD_NONE);
      return new_tab;
    }
    case NEW_WINDOW: {
      if (!base::FeatureList::IsEnabled(features::kSingleWindow)) {
          WebContents* new_tab = current_tab->Clone();
          Browser* new_browser =
              new Browser(Browser::CreateParams(browser->profile()));
          new_browser->tab_strip_model()->AddWebContents(
              new_tab, -1, ui::PAGE_TRANSITION_LINK,
              TabStripModel::ADD_ACTIVE);
          new_browser->window()->Show();
          return new_tab;
        }
    }
    default:
      browser->window()->GetLocationBar()->Revert();
      return current_tab;
  }
}

void ReloadInternal(Browser* browser,
                    WindowOpenDisposition disposition,
                    bool bypass_cache) {
  // As this is caused by a user action, give the focus to the page.
  //
  // Also notify RenderViewHostDelegate of the user gesture; this is
  // normally done in Browser::Navigate, but a reload bypasses Navigate.
  WebContents* new_tab = GetTabAndRevertIfNecessary(browser, disposition);
  new_tab->UserGestureDone();
  if (!new_tab->FocusLocationBarByDefault())
    new_tab->Focus();

  DevToolsWindow* devtools =
      DevToolsWindow::GetInstanceForInspectedWebContents(new_tab);
  if (devtools && devtools->ReloadInspectedWebContents(bypass_cache))
    return;

  if (bypass_cache)
    new_tab->GetController().ReloadBypassingCache(true);
  else
    new_tab->GetController().Reload(true);
}

bool IsShowingWebContentsModalDialog(Browser* browser) {
  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents)
    return false;

  // TODO(gbillock): This is currently called in production by the CanPrint
  // method, and may be too restrictive if we allow print preview to overlap.
  // Re-assess how to queue print preview after we know more about popup
  // management policy.
  const web_modal::WebContentsModalDialogManager* manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(web_contents);
  return manager && manager->IsDialogActive();
}

#if defined(ENABLE_BASIC_PRINTING)
bool PrintPreviewShowing(const Browser* browser) {
#if defined(ENABLE_PRINT_PREVIEW)
  WebContents* contents = browser->tab_strip_model()->GetActiveWebContents();
  printing::PrintPreviewDialogController* controller =
      printing::PrintPreviewDialogController::GetInstance();
  return controller && (controller->GetPrintPreviewForContents(contents) ||
                        controller->is_creating_print_preview_dialog());
#else
  return false;
#endif
}
#endif  // ENABLE_BASIC_PRINTING

}  // namespace

bool IsCommandEnabled(Browser* browser, int command) {
  return browser->command_controller()->command_updater()->IsCommandEnabled(
      command);
}

bool SupportsCommand(Browser* browser, int command) {
  return browser->command_controller()->command_updater()->SupportsCommand(
      command);
}

bool ExecuteCommand(Browser* browser, int command) {
  return browser->command_controller()->command_updater()->ExecuteCommand(
      command);
}

bool ExecuteCommandWithDisposition(Browser* browser,
                                   int command,
                                   WindowOpenDisposition disposition) {
  return browser->command_controller()->command_updater()->
      ExecuteCommandWithDisposition(command, disposition);
}

void UpdateCommandEnabled(Browser* browser, int command, bool enabled) {
  browser->command_controller()->command_updater()->UpdateCommandEnabled(
      command, enabled);
}

void AddCommandObserver(Browser* browser,
                        int command,
                        CommandObserver* observer) {
  browser->command_controller()->command_updater()->AddCommandObserver(
      command, observer);
}

void RemoveCommandObserver(Browser* browser,
                           int command,
                           CommandObserver* observer) {
  browser->command_controller()->command_updater()->RemoveCommandObserver(
      command, observer);
}

int GetContentRestrictions(const Browser* browser) {
  int content_restrictions = 0;
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  if (current_tab) {
    CoreTabHelper* core_tab_helper =
        CoreTabHelper::FromWebContents(current_tab);
    content_restrictions = core_tab_helper->content_restrictions();
    NavigationEntry* last_committed_entry =
        current_tab->GetController().GetLastCommittedEntry();
    if (!content::IsSavableURL(
            last_committed_entry ? last_committed_entry->GetURL() : GURL()) ||
        current_tab->ShowingInterstitialPage())
      content_restrictions |= CONTENT_RESTRICTION_SAVE;
    if (current_tab->ShowingInterstitialPage())
      content_restrictions |= CONTENT_RESTRICTION_PRINT;
  }
  return content_restrictions;
}

void NewEmptyWindow(Profile* profile) {
  bool incognito = profile->IsOffTheRecord();
  PrefService* prefs = profile->GetPrefs();
  if (incognito) {
    if (IncognitoModePrefs::GetAvailability(prefs) ==
          IncognitoModePrefs::DISABLED) {
      incognito = false;
    }
  } else if (profile->IsGuestSession() ||
             (browser_defaults::kAlwaysOpenIncognitoWindow &&
              IncognitoModePrefs::ShouldLaunchIncognito(
                  *base::CommandLine::ForCurrentProcess(), prefs))) {
    incognito = true;
  }

  if (incognito) {
    content::RecordAction(UserMetricsAction("NewIncognitoWindow"));
    OpenEmptyWindow(profile->GetOffTheRecordProfile());
  } else {
    content::RecordAction(UserMetricsAction("NewWindow"));
    SessionService* session_service =
        SessionServiceFactory::GetForProfileForSessionRestore(
            profile->GetOriginalProfile());
    if (!session_service ||
        !session_service->RestoreIfNecessary(std::vector<GURL>())) {
      OpenEmptyWindow(profile->GetOriginalProfile());
    }
  }
}

Browser* OpenEmptyWindow(Profile* profile) {
  if (base::FeatureList::IsEnabled(features::kSingleWindow))
    return nullptr;

  Browser* browser =
      new Browser(Browser::CreateParams(Browser::TYPE_TABBED, profile));
  AddTabAt(browser, GURL(), -1, true);
  browser->window()->Show();
  return browser;
}

void OpenWindowWithRestoredTabs(Profile* profile) {
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(profile);
  if (service)
    service->RestoreMostRecentEntry(nullptr);
}

void OpenURLOffTheRecord(Profile* profile,
                         const GURL& url) {
  ScopedTabbedBrowserDisplayer displayer(profile->GetOffTheRecordProfile());
  AddSelectedTabWithURL(displayer.browser(), url,
      ui::PAGE_TRANSITION_LINK);
}

bool CanGoBack(const Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents()->
      GetController().CanGoBack();
}

void GoBack(Browser* browser, WindowOpenDisposition disposition) {
  content::RecordAction(UserMetricsAction("Back"));

  if (CanGoBack(browser)) {
    WebContents* current_tab =
        browser->tab_strip_model()->GetActiveWebContents();
    WebContents* new_tab = GetTabAndRevertIfNecessary(browser, disposition);
    // If we are on an interstitial page and clone the tab, it won't be copied
    // to the new tab, so we don't need to go back.
    if ((new_tab == current_tab) || !current_tab->ShowingInterstitialPage())
      new_tab->GetController().GoBack();
  }
}

bool CanGoForward(const Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents()->
      GetController().CanGoForward();
}

void GoForward(Browser* browser, WindowOpenDisposition disposition) {
  content::RecordAction(UserMetricsAction("Forward"));
  if (CanGoForward(browser)) {
    GetTabAndRevertIfNecessary(browser, disposition)->
        GetController().GoForward();
  }
}

bool NavigateToIndexWithDisposition(Browser* browser,
                                    int index,
                                    WindowOpenDisposition disposition) {
  NavigationController* controller =
      &GetTabAndRevertIfNecessary(browser, disposition)->GetController();
  if (index < 0 || index >= controller->GetEntryCount())
    return false;
  controller->GoToIndex(index);
  return true;
}

void Reload(Browser* browser, WindowOpenDisposition disposition) {
  content::RecordAction(UserMetricsAction("Reload"));
  ReloadInternal(browser, disposition, false);
}

void ReloadBypassingCache(Browser* browser, WindowOpenDisposition disposition) {
  content::RecordAction(UserMetricsAction("ReloadBypassingCache"));
  ReloadInternal(browser, disposition, true);
}

bool CanReload(const Browser* browser) {
  return !browser->is_devtools();
}

void Home(Browser* browser, WindowOpenDisposition disposition) {
  content::RecordAction(UserMetricsAction("Home"));

  std::string extra_headers;
#if defined(ENABLE_RLZ)
  // If the home page is a Google home page, add the RLZ header to the request.
  PrefService* pref_service = browser->profile()->GetPrefs();
  if (pref_service) {
    if (google_util::IsGoogleHomePageUrl(
        GURL(pref_service->GetString(prefs::kHomePage)))) {
      extra_headers = rlz::RLZTracker::GetAccessPointHttpHeader(
          rlz::RLZTracker::ChromeHomePage());
    }
  }
#endif  // defined(ENABLE_RLZ)

  GURL url = browser->profile()->GetHomePage();

#if defined(ENABLE_EXTENSIONS)
  // With bookmark apps enabled, hosted apps should return to their launch page
  // when the home button is pressed.
  if (browser->is_app()) {
    const extensions::Extension* extension =
        extensions::ExtensionRegistry::Get(browser->profile())
            ->GetExtensionById(
                web_app::GetExtensionIdFromApplicationName(browser->app_name()),
                extensions::ExtensionRegistry::EVERYTHING);
    if (!extension)
      return;

    url = extensions::AppLaunchInfo::GetLaunchWebURL(extension);
  }

  if (disposition == CURRENT_TAB || disposition == NEW_FOREGROUND_TAB)
    extensions::MaybeShowExtensionControlledHomeNotification(browser);
#endif

  OpenURLParams params(
      url, Referrer(), disposition,
      ui::PageTransitionFromInt(
          ui::PAGE_TRANSITION_AUTO_BOOKMARK |
          ui::PAGE_TRANSITION_HOME_PAGE),
      false);
  params.extra_headers = extra_headers;
  browser->OpenURL(params);
}

void OpenCurrentURL(Browser* browser) {
  content::RecordAction(UserMetricsAction("LoadURL"));
  LocationBar* location_bar = browser->window()->GetLocationBar();
  if (!location_bar)
    return;

  GURL url(location_bar->GetDestinationURL());

  ui::PageTransition page_transition = location_bar->GetPageTransition();
  WindowOpenDisposition open_disposition =
      location_bar->GetWindowOpenDisposition();
  // A PAGE_TRANSITION_TYPED means the user has typed a URL. We do not want to
  // open URLs with instant_controller since in some cases it disregards it
  // and performs a search instead. For example, when using CTRL-Enter, the
  // location_bar is aware of the URL but instant is not.
  // Instant should also not handle PAGE_TRANSITION_RELOAD because its knowledge
  // of the omnibox text may be stale if the user focuses in the omnibox and
  // presses enter without typing anything.
  if (!ui::PageTransitionCoreTypeIs(page_transition,
                                    ui::PAGE_TRANSITION_TYPED) &&
      !ui::PageTransitionCoreTypeIs(page_transition,
                                    ui::PAGE_TRANSITION_RELOAD) &&
      browser->instant_controller() &&
      browser->instant_controller()->OpenInstant(open_disposition, url))
    return;

  NavigateParams params(browser, url, page_transition);
  params.disposition = open_disposition;
  // Use ADD_INHERIT_OPENER so that all pages opened by the omnibox at least
  // inherit the opener. In some cases the tabstrip will determine the group
  // should be inherited, in which case the group is inherited instead of the
  // opener.
  params.tabstrip_add_types =
      TabStripModel::ADD_FORCE_INDEX | TabStripModel::ADD_INHERIT_OPENER;
  Navigate(&params);

#if defined(ENABLE_EXTENSIONS)
  DCHECK(extensions::ExtensionSystem::Get(
      browser->profile())->extension_service());
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(browser->profile())
          ->enabled_extensions().GetAppByURL(url);
  if (extension) {
    extensions::RecordAppLaunchType(extension_misc::APP_LAUNCH_OMNIBOX_LOCATION,
                                    extension->GetType());
  }
#endif
}

void Stop(Browser* browser) {
  content::RecordAction(UserMetricsAction("Stop"));
  browser->tab_strip_model()->GetActiveWebContents()->Stop();
}

void NewWindow(Browser* browser) {
  NewEmptyWindow(browser->profile()->GetOriginalProfile());
}

void NewIncognitoWindow(Browser* browser) {
  NewEmptyWindow(browser->profile()->GetOffTheRecordProfile());
}

void CloseWindow(Browser* browser) {
  content::RecordAction(UserMetricsAction("CloseWindow"));
  browser->window()->Close();
}

void NewTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("NewTab"));
  // TODO(asvitkine): This is invoked programmatically from several places.
  // Audit the code and change it so that the histogram only gets collected for
  // user-initiated commands.
  UMA_HISTOGRAM_ENUMERATION("Tab.NewTab", TabStripModel::NEW_TAB_COMMAND,
                            TabStripModel::NEW_TAB_ENUM_COUNT);

  if (browser->is_type_tabbed()) {
    AddTabAt(browser, GURL(), -1, true);
    browser->tab_strip_model()->GetActiveWebContents()->RestoreFocus();
  } else {
    ScopedTabbedBrowserDisplayer displayer(browser->profile());
    Browser* b = displayer.browser();
    AddTabAt(b, GURL(), -1, true);
    b->window()->Show();
    // The call to AddBlankTabAt above did not set the focus to the tab as its
    // window was not active, so we have to do it explicitly.
    // See http://crbug.com/6380.
    b->tab_strip_model()->GetActiveWebContents()->RestoreFocus();
  }
}

void CloseTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("CloseTab_Accelerator"));
  browser->tab_strip_model()->CloseSelectedTabs();
}

bool CanZoomIn(content::WebContents* contents) {
  return contents && !contents->IsCrashed() &&
         zoom::ZoomController::FromWebContents(contents)->GetZoomPercent() !=
             contents->GetMaximumZoomPercent();
}

bool CanZoomOut(content::WebContents* contents) {
  return contents && !contents->IsCrashed() &&
         zoom::ZoomController::FromWebContents(contents)->GetZoomPercent() !=
             contents->GetMinimumZoomPercent();
}

bool CanResetZoom(content::WebContents* contents) {
  zoom::ZoomController* zoom_controller =
      zoom::ZoomController::FromWebContents(contents);
  return !zoom_controller->IsAtDefaultZoom() ||
         !zoom_controller->PageScaleFactorIsOne();
}

TabStripModelDelegate::RestoreTabType GetRestoreTabType(
    const Browser* browser) {
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(browser->profile());
  if (!service || service->entries().empty())
    return TabStripModelDelegate::RESTORE_NONE;
  if (service->entries().front()->type == sessions::TabRestoreService::WINDOW)
    return TabStripModelDelegate::RESTORE_WINDOW;
  return TabStripModelDelegate::RESTORE_TAB;
}

void SelectNextTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("SelectNextTab"));
  browser->tab_strip_model()->SelectNextTab();
}

void SelectPreviousTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("SelectPrevTab"));
  browser->tab_strip_model()->SelectPreviousTab();
}

void MoveTabNext(Browser* browser) {
  content::RecordAction(UserMetricsAction("MoveTabNext"));
  browser->tab_strip_model()->MoveTabNext();
}

void MoveTabPrevious(Browser* browser) {
  content::RecordAction(UserMetricsAction("MoveTabPrevious"));
  browser->tab_strip_model()->MoveTabPrevious();
}

void SelectNumberedTab(Browser* browser, int index) {
  if (index < browser->tab_strip_model()->count()) {
    content::RecordAction(UserMetricsAction("SelectNumberedTab"));
    browser->tab_strip_model()->ActivateTabAt(index, true);
  }
}

void SelectLastTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("SelectLastTab"));
  browser->tab_strip_model()->SelectLastTab();
}

void DuplicateTab(Browser* browser) {
  content::RecordAction(UserMetricsAction("Duplicate"));
  DuplicateTabAt(browser, browser->tab_strip_model()->active_index());
}

bool CanDuplicateTab(const Browser* browser) {
  return CanDuplicateTabAt(browser, browser->tab_strip_model()->active_index());
}

WebContents* DuplicateTabAt(Browser* browser, int index) {
  WebContents* contents = browser->tab_strip_model()->GetWebContentsAt(index);
  CHECK(contents);
  WebContents* contents_dupe = contents->Clone();

  bool pinned = false;
  if (browser->CanSupportWindowFeature(Browser::FEATURE_TABSTRIP)) {
    // If this is a tabbed browser, just create a duplicate tab inside the same
    // window next to the tab being duplicated.
    int index = browser->tab_strip_model()->GetIndexOfWebContents(contents);
    pinned = browser->tab_strip_model()->IsTabPinned(index);
    int add_types = TabStripModel::ADD_ACTIVE |
        TabStripModel::ADD_INHERIT_GROUP |
        (pinned ? TabStripModel::ADD_PINNED : 0);
    browser->tab_strip_model()->InsertWebContentsAt(
        index + 1, contents_dupe, add_types);
  } else {
    Browser* new_browser = NULL;
    if (browser->is_app() && !browser->is_type_popup()) {
      new_browser = new Browser(Browser::CreateParams::CreateForApp(
          browser->app_name(), browser->is_trusted_source(), gfx::Rect(),
          browser->profile()));
    } else {
      new_browser = new Browser(
          Browser::CreateParams(browser->type(), browser->profile()));
    }
    // Preserve the size of the original window. The new window has already
    // been given an offset by the OS, so we shouldn't copy the old bounds.
    BrowserWindow* new_window = new_browser->window();
    new_window->SetBounds(gfx::Rect(new_window->GetRestoredBounds().origin(),
                          browser->window()->GetRestoredBounds().size()));

    // We need to show the browser now.  Otherwise ContainerWin assumes the
    // WebContents is invisible and won't size it.
    new_browser->window()->Show();

    // The page transition below is only for the purpose of inserting the tab.
    new_browser->tab_strip_model()->AddWebContents(
        contents_dupe, -1,
        ui::PAGE_TRANSITION_LINK,
        TabStripModel::ADD_ACTIVE);
  }

  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(browser->profile());
  if (session_service)
    session_service->TabRestored(contents_dupe, pinned);
  return contents_dupe;
}

bool CanDuplicateTabAt(const Browser* browser, int index) {
  WebContents* contents = browser->tab_strip_model()->GetWebContentsAt(index);
  // If an interstitial is showing, do not allow tab duplication, since
  // the last committed entry is what would get duplicated and is not
  // what the user expects to duplicate.
  return contents && !contents->ShowingInterstitialPage() &&
         contents->GetController().GetLastCommittedEntry();
}

void ConvertPopupToTabbedBrowser(Browser* browser) {
  if (base::FeatureList::IsEnabled(features::kSingleWindow))
    return;

  content::RecordAction(UserMetricsAction("ShowAsTab"));
  TabStripModel* tab_strip = browser->tab_strip_model();
  WebContents* contents =
      tab_strip->DetachWebContentsAt(tab_strip->active_index());
  Browser* b = new Browser(Browser::CreateParams(browser->profile()));
  b->tab_strip_model()->AppendWebContents(contents, true);
  b->window()->Show();
}

void Exit() {
  content::RecordAction(UserMetricsAction("Exit"));
  chrome::AttemptUserExit();
}

void BookmarkCurrentPageIgnoringExtensionOverrides(Browser* browser) {
  content::RecordAction(UserMetricsAction("Star"));

  BookmarkModel* model =
      BookmarkModelFactory::GetForProfile(browser->profile());
  if (!model || !model->loaded())
    return;  // Ignore requests until bookmarks are loaded.

  GURL url;
  base::string16 title;
  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  GetURLAndTitleToBookmark(web_contents, &url, &title);
  bool is_bookmarked_by_any = model->IsBookmarked(url);
  if (!is_bookmarked_by_any &&
      web_contents->GetBrowserContext()->IsOffTheRecord()) {
    // If we're incognito the favicon may not have been saved. Save it now
    // so that bookmarks have an icon for the page.
    favicon::ContentFaviconDriver::FromWebContents(web_contents)->SaveFavicon();
  }
  bool was_bookmarked_by_user = bookmarks::IsBookmarkedByUser(model, url);
  bookmarks::AddIfNotBookmarked(model, url, title);
  bool is_bookmarked_by_user = bookmarks::IsBookmarkedByUser(model, url);
  // Make sure the model actually added a bookmark before showing the star. A
  // bookmark isn't created if the url is invalid.
  if (browser->window()->IsActive() && is_bookmarked_by_user) {
    // Only show the bubble if the window is active, otherwise we may get into
    // weird situations where the bubble is deleted as soon as it is shown.
    browser->window()->ShowBookmarkBubble(url, was_bookmarked_by_user);
  }
}

void BookmarkCurrentPageAllowingExtensionOverrides(Browser* browser) {
  DCHECK(!chrome::ShouldRemoveBookmarkThisPageUI(browser->profile()));

#if defined(ENABLE_EXTENSIONS)
  const extensions::Extension* extension = NULL;
  extensions::Command command;
  extensions::CommandService::ExtensionCommandType command_type;
  if (GetBookmarkOverrideCommand(browser->profile(),
                                 &extension,
                                 &command,
                                 &command_type)) {
    switch (command_type) {
      case extensions::CommandService::NAMED:
        browser->window()->ExecuteExtensionCommand(extension, command);
        break;
      case extensions::CommandService::BROWSER_ACTION:
      case extensions::CommandService::PAGE_ACTION:
        // BookmarkCurrentPage is called through a user gesture, so it is safe
        // to grant the active tab permission.
        extensions::ExtensionActionAPI::Get(browser->profile())->
            ShowExtensionActionPopup(extension, browser, true);
        break;
    }
    return;
  }
#endif
  BookmarkCurrentPageIgnoringExtensionOverrides(browser);
}

bool CanBookmarkCurrentPage(const Browser* browser) {
  return CanBookmarkCurrentPageInternal(browser, true);
}

void BookmarkAllTabs(Browser* browser) {
  content::RecordAction(UserMetricsAction("BookmarkAllTabs"));
  chrome::ShowBookmarkAllTabsDialog(browser);
}

bool CanBookmarkAllTabs(const Browser* browser) {
  return browser->tab_strip_model()->count() > 1 &&
             !chrome::ShouldRemoveBookmarkOpenPagesUI(browser->profile()) &&
             CanBookmarkCurrentPageInternal(browser, false);
}

void SaveCreditCard(Browser* browser) {
  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  autofill::SaveCardBubbleControllerImpl* controller =
      autofill::SaveCardBubbleControllerImpl::FromWebContents(web_contents);
  controller->ReshowBubble();
}

void Translate(Browser* browser) {
  if (!browser->window()->IsActive())
    return;

  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  ChromeTranslateClient* chrome_translate_client =
      ChromeTranslateClient::FromWebContents(web_contents);

  translate::TranslateStep step = translate::TRANSLATE_STEP_BEFORE_TRANSLATE;
  if (chrome_translate_client) {
    if (chrome_translate_client->GetLanguageState().translation_pending())
      step = translate::TRANSLATE_STEP_TRANSLATING;
    else if (chrome_translate_client->GetLanguageState().IsPageTranslated())
      step = translate::TRANSLATE_STEP_AFTER_TRANSLATE;
  }
  browser->window()->ShowTranslateBubble(
      web_contents, step, translate::TranslateErrors::NONE, true);
}

void ManagePasswordsForPage(Browser* browser) {
  WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  ManagePasswordsUIController* controller =
      ManagePasswordsUIController::FromWebContents(web_contents);
  TabDialogs::FromWebContents(web_contents)->ShowManagePasswordsBubble(
      !controller->IsAutomaticallyOpeningBubble());
}

void SavePage(Browser* browser) {
  content::RecordAction(UserMetricsAction("SavePage"));
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  if (current_tab && current_tab->GetContentsMimeType() == "application/pdf")
    content::RecordAction(UserMetricsAction("PDF.SavePage"));
  current_tab->OnSavePage();
}

bool CanSavePage(const Browser* browser) {
  // LocalState can be NULL in tests.
  if (g_browser_process->local_state() &&
      !g_browser_process->local_state()->GetBoolean(
      prefs::kAllowFileSelectionDialogs)) {
    return false;
  }
  return !browser->is_devtools() &&
      !(GetContentRestrictions(browser) & CONTENT_RESTRICTION_SAVE);
}

void ShowFindBar(Browser* browser) {
  browser->GetFindBarController()->Show();
}

void ShowWebsiteSettings(
    Browser* browser,
    content::WebContents* web_contents,
    const GURL& url,
    const security_state::SecurityStateModel::SecurityInfo& security_info) {
  browser->window()->ShowWebsiteSettings(
      Profile::FromBrowserContext(web_contents->GetBrowserContext()),
      web_contents, url, security_info);
}

void Print(Browser* browser) {
#if defined(ENABLE_PRINTING)
  printing::StartPrint(
      browser->tab_strip_model()->GetActiveWebContents(),
      browser->profile()->GetPrefs()->GetBoolean(prefs::kPrintPreviewDisabled),
      false);
#endif  // defined(ENABLE_PRINTING)
}

bool CanPrint(Browser* browser) {
  // Do not print when printing is disabled via pref or policy.
  // Do not print when a page has crashed.
  // Do not print when a constrained window is showing. It's confusing.
  // TODO(gbillock): Need to re-assess the call to
  // IsShowingWebContentsModalDialog after a popup management policy is
  // refined -- we will probably want to just queue the print request, not
  // block it.
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  return browser->profile()->GetPrefs()->GetBoolean(prefs::kPrintingEnabled) &&
      (current_tab && !current_tab->IsCrashed()) &&
      !(IsShowingWebContentsModalDialog(browser) ||
        GetContentRestrictions(browser) & CONTENT_RESTRICTION_PRINT);
}

#if defined(ENABLE_BASIC_PRINTING)
void BasicPrint(Browser* browser) {
  printing::StartBasicPrint(browser->tab_strip_model()->GetActiveWebContents());
}

bool CanBasicPrint(Browser* browser) {
  // If printing is not disabled via pref or policy, it is always possible to
  // advanced print when the print preview is visible.
  return browser->profile()->GetPrefs()->GetBoolean(prefs::kPrintingEnabled) &&
      (PrintPreviewShowing(browser) || CanPrint(browser));
}
#endif  // ENABLE_BASIC_PRINTING

bool CanRouteMedia(Browser* browser) {
  // Do not allow user to open Media Router dialog when there is already an
  // active modal dialog. This avoids overlapping dialogs.
  return media_router::MediaRouterEnabled(browser->profile()) &&
         !IsShowingWebContentsModalDialog(browser);
}

void RouteMedia(Browser* browser) {
#if defined(ENABLE_MEDIA_ROUTER)
  DCHECK(CanRouteMedia(browser));

  media_router::MediaRouterDialogController* dialog_controller =
      media_router::MediaRouterDialogController::GetOrCreateForWebContents(
          browser->tab_strip_model()->GetActiveWebContents());
  if (!dialog_controller)
    return;

  dialog_controller->ShowMediaRouterDialog();
#endif  // defined(ENABLE_MEDIA_ROUTER)
}

void EmailPageLocation(Browser* browser) {
  content::RecordAction(UserMetricsAction("EmailPageLocation"));
  WebContents* wc = browser->tab_strip_model()->GetActiveWebContents();
  DCHECK(wc);

  std::string title = net::EscapeQueryParamValue(
      base::UTF16ToUTF8(wc->GetTitle()), false);
  std::string page_url = net::EscapeQueryParamValue(wc->GetURL().spec(), false);
  std::string mailto = std::string("mailto:?subject=Fwd:%20") +
      title + "&body=%0A%0A" + page_url;
  platform_util::OpenExternal(browser->profile(), GURL(mailto));
}

bool CanEmailPageLocation(const Browser* browser) {
  return browser->toolbar_model()->ShouldDisplayURL() &&
      browser->tab_strip_model()->GetActiveWebContents()->GetURL().is_valid();
}

void CutCopyPaste(Browser* browser, int command_id) {
  if (command_id == IDC_CUT)
    content::RecordAction(UserMetricsAction("Cut"));
  else if (command_id == IDC_COPY)
    content::RecordAction(UserMetricsAction("Copy"));
  else
    content::RecordAction(UserMetricsAction("Paste"));
  browser->window()->CutCopyPaste(command_id);
}

void Find(Browser* browser) {
  content::RecordAction(UserMetricsAction("Find"));
  FindInPage(browser, false, false);
}

void FindNext(Browser* browser) {
  content::RecordAction(UserMetricsAction("FindNext"));
  FindInPage(browser, true, true);
}

void FindPrevious(Browser* browser) {
  content::RecordAction(UserMetricsAction("FindPrevious"));
  FindInPage(browser, true, false);
}

void FindInPage(Browser* browser, bool find_next, bool forward_direction) {
  ShowFindBar(browser);
  if (find_next) {
    base::string16 find_text;
    FindTabHelper* find_helper = FindTabHelper::FromWebContents(
        browser->tab_strip_model()->GetActiveWebContents());
#if defined(OS_MACOSX)
    // We always want to search for the current contents of the find bar on
    // OS X. For regular profile it's always the current find pboard. For
    // Incognito window it's the newest value of the find pboard content and
    // user-typed text.
    FindBar* find_bar = browser->GetFindBarController()->find_bar();
    find_text = find_bar->GetFindText();
#endif
    find_helper->StartFinding(find_text, forward_direction, false);
  }
}

void Zoom(Browser* browser, content::PageZoom zoom) {
  zoom::PageZoom::Zoom(browser->tab_strip_model()->GetActiveWebContents(),
                       zoom);
}

void FocusToolbar(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusToolbar"));
  browser->window()->FocusToolbar();
}

void FocusLocationBar(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusLocation"));
  browser->window()->SetFocusToLocationBar(true);
}

void FocusSearch(Browser* browser) {
  // TODO(beng): replace this with FocusLocationBar
  content::RecordAction(UserMetricsAction("FocusSearch"));
  browser->window()->GetLocationBar()->FocusSearch();
}

void FocusAppMenu(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusAppMenu"));
  browser->window()->FocusAppMenu();
}

void FocusBookmarksToolbar(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusBookmarksToolbar"));
  browser->window()->FocusBookmarksToolbar();
}

void FocusInfobars(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusInfobars"));
  browser->window()->FocusInfobars();
}

void FocusNextPane(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusNextPane"));
  browser->window()->RotatePaneFocus(true);
}

void FocusPreviousPane(Browser* browser) {
  content::RecordAction(UserMetricsAction("FocusPreviousPane"));
  browser->window()->RotatePaneFocus(false);
}

void ToggleDevToolsWindow(Browser* browser, DevToolsToggleAction action) {
  if (action.type() == DevToolsToggleAction::kShowConsole)
    content::RecordAction(UserMetricsAction("DevTools_ToggleConsole"));
  else
    content::RecordAction(UserMetricsAction("DevTools_ToggleWindow"));
  DevToolsWindow::ToggleDevToolsWindow(browser, action);
}

bool CanOpenTaskManager() {
#if defined(ENABLE_TASK_MANAGER)
  return true;
#else
  return false;
#endif
}

void OpenTaskManager(Browser* browser) {
#if defined(ENABLE_TASK_MANAGER)
  content::RecordAction(UserMetricsAction("TaskManager"));
  chrome::ShowTaskManager(browser);
#else
  NOTREACHED();
#endif
}

void OpenFeedbackDialog(Browser* browser) {
  content::RecordAction(UserMetricsAction("Feedback"));
  chrome::ShowFeedbackPage(browser, std::string(), std::string());
}

void ToggleBookmarkBar(Browser* browser) {
  content::RecordAction(UserMetricsAction("ShowBookmarksBar"));
  ToggleBookmarkBarWhenVisible(browser->profile());
}

void ShowAppMenu(Browser* browser) {
  // We record the user metric for this event in AppMenu::RunMenu.
  browser->window()->ShowAppMenu();
}

void ShowAvatarMenu(Browser* browser) {
  browser->window()->ShowAvatarBubbleFromAvatarButton(
      BrowserWindow::AVATAR_BUBBLE_MODE_DEFAULT, signin::ManageAccountsParams(),
      signin_metrics::AccessPoint::ACCESS_POINT_AVATAR_BUBBLE_SIGN_IN);
}

void ShowFastUserSwitcher(Browser* browser) {
  browser->window()->ShowAvatarBubbleFromAvatarButton(
      BrowserWindow::AVATAR_BUBBLE_MODE_FAST_USER_SWITCH,
      signin::ManageAccountsParams(),
      signin_metrics::AccessPoint::ACCESS_POINT_AVATAR_BUBBLE_SIGN_IN);
}

void OpenUpdateChromeDialog(Browser* browser) {
  if (UpgradeDetector::GetInstance()->is_outdated_install()) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_OUTDATED_INSTALL,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  } else if (UpgradeDetector::GetInstance()->is_outdated_install_no_au()) {
    content::NotificationService::current()->Notify(
        chrome::NOTIFICATION_OUTDATED_INSTALL_NO_AU,
        content::NotificationService::AllSources(),
        content::NotificationService::NoDetails());
  } else {
    content::RecordAction(UserMetricsAction("UpdateChrome"));
    browser->window()->ShowUpdateChromeDialog();
  }
}

void DistillCurrentPage(Browser* browser) {
  DistillCurrentPageAndView(browser->tab_strip_model()->GetActiveWebContents());
}

bool CanRequestTabletSite(WebContents* current_tab) {
  return current_tab &&
      current_tab->GetController().GetLastCommittedEntry() != NULL;
}

bool IsRequestingTabletSite(Browser* browser) {
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  if (!current_tab)
    return false;
  content::NavigationEntry* entry =
      current_tab->GetController().GetLastCommittedEntry();
  if (!entry)
    return false;
  return entry->GetIsOverridingUserAgent();
}

void ToggleRequestTabletSite(Browser* browser) {
  WebContents* current_tab = browser->tab_strip_model()->GetActiveWebContents();
  if (!current_tab)
    return;
  NavigationController& controller = current_tab->GetController();
  NavigationEntry* entry = controller.GetLastCommittedEntry();
  if (!entry)
    return;
  if (entry->GetIsOverridingUserAgent()) {
    entry->SetIsOverridingUserAgent(false);
  } else {
    entry->SetIsOverridingUserAgent(true);
    std::string product = version_info::GetProductNameAndVersionForUserAgent();
    current_tab->SetUserAgentOverride(content::BuildUserAgentFromOSAndProduct(
        kOsOverrideForTabletSite, product));
  }
  controller.ReloadOriginalRequestURL(true);
}

void ToggleFullscreenMode(Browser* browser) {
  DCHECK(browser);
  browser->exclusive_access_manager()
      ->fullscreen_controller()
      ->ToggleBrowserFullscreenMode();
}

void ClearCache(Browser* browser) {
  BrowsingDataRemover* remover =
      BrowsingDataRemoverFactory::GetForBrowserContext(browser->profile());
  remover->Remove(BrowsingDataRemover::Unbounded(),
                  BrowsingDataRemover::REMOVE_CACHE,
                  BrowsingDataHelper::UNPROTECTED_WEB);
  // BrowsingDataRemover takes care of deleting itself when done.
}

bool IsDebuggerAttachedToCurrentTab(Browser* browser) {
  WebContents* contents = browser->tab_strip_model()->GetActiveWebContents();
  return contents ?
      content::DevToolsAgentHost::IsDebuggerAttached(contents) : false;
}

void ViewSource(Browser* browser, WebContents* contents) {
  DCHECK(contents);

  // Use the last committed entry, since the pending entry hasn't loaded yet and
  // won't be copied into the cloned tab.
  NavigationEntry* entry = contents->GetController().GetLastCommittedEntry();
  if (!entry)
    return;

  ViewSource(browser, contents, entry->GetURL(), entry->GetPageState());
}

void ViewSource(Browser* browser,
                WebContents* contents,
                const GURL& url,
                const content::PageState& page_state) {
  content::RecordAction(UserMetricsAction("ViewSource"));
  DCHECK(contents);

  WebContents* view_source_contents = contents->Clone();
  DCHECK(view_source_contents->GetController().CanPruneAllButLastCommitted());
  view_source_contents->GetController().PruneAllButLastCommitted();
  NavigationEntry* last_committed_entry =
      view_source_contents->GetController().GetLastCommittedEntry();
  if (!last_committed_entry)
    return;

  GURL view_source_url =
      GURL(content::kViewSourceScheme + std::string(":") + url.spec());
  last_committed_entry->SetVirtualURL(view_source_url);

  // Do not restore scroller position.
  last_committed_entry->SetPageState(page_state.RemoveScrollOffset());

  // Do not restore title, derive it from the url.
  last_committed_entry->SetTitle(base::string16());

  // Now show view-source entry.
  if (browser->CanSupportWindowFeature(Browser::FEATURE_TABSTRIP)) {
    // If this is a tabbed browser, just create a duplicate tab inside the same
    // window next to the tab being duplicated.
    int index = browser->tab_strip_model()->GetIndexOfWebContents(contents);
    int add_types = TabStripModel::ADD_ACTIVE |
        TabStripModel::ADD_INHERIT_GROUP;
    browser->tab_strip_model()->InsertWebContentsAt(
        index + 1,
        view_source_contents,
        add_types);
  } else {
    Browser* b = new Browser(
        Browser::CreateParams(Browser::TYPE_TABBED, browser->profile()));

    // Preserve the size of the original window. The new window has already
    // been given an offset by the OS, so we shouldn't copy the old bounds.
    BrowserWindow* new_window = b->window();
    new_window->SetBounds(gfx::Rect(new_window->GetRestoredBounds().origin(),
                          browser->window()->GetRestoredBounds().size()));

    // We need to show the browser now. Otherwise ContainerWin assumes the
    // WebContents is invisible and won't size it.
    b->window()->Show();

    // The page transition below is only for the purpose of inserting the tab.
    b->tab_strip_model()->AddWebContents(view_source_contents, -1,
                                         ui::PAGE_TRANSITION_LINK,
                                         TabStripModel::ADD_ACTIVE);
  }

  SessionService* session_service =
      SessionServiceFactory::GetForProfileIfExisting(browser->profile());
  if (session_service)
    session_service->TabRestored(view_source_contents, false);
}

void ViewSelectedSource(Browser* browser) {
  ViewSource(browser, browser->tab_strip_model()->GetActiveWebContents());
}

bool CanViewSource(const Browser* browser) {
  return !browser->is_devtools() &&
      browser->tab_strip_model()->GetActiveWebContents()->GetController().
          CanViewSource();
}

#if defined(ENABLE_EXTENSIONS)
void CreateApplicationShortcuts(Browser* browser) {
  content::RecordAction(UserMetricsAction("CreateShortcut"));
  extensions::TabHelper::FromWebContents(
      browser->tab_strip_model()->GetActiveWebContents())->
          CreateApplicationShortcuts();
}

void CreateBookmarkAppFromCurrentWebContents(Browser* browser) {
  content::RecordAction(UserMetricsAction("CreateHostedApp"));
  extensions::TabHelper::FromWebContents(
      browser->tab_strip_model()->GetActiveWebContents())->
          CreateHostedAppFromWebContents();
}

bool CanCreateApplicationShortcuts(const Browser* browser) {
  return extensions::TabHelper::FromWebContents(
      browser->tab_strip_model()->GetActiveWebContents())->
          CanCreateApplicationShortcuts();
}

bool CanCreateBookmarkApp(const Browser* browser) {
  return extensions::TabHelper::FromWebContents(
             browser->tab_strip_model()->GetActiveWebContents())
      ->CanCreateBookmarkApp();
}

void ConvertTabToAppWindow(Browser* browser,
                           content::WebContents* contents) {
  if (base::FeatureList::IsEnabled(features::kSingleWindow))
    return;

  const GURL& url = contents->GetController().GetLastCommittedEntry()->GetURL();
  std::string app_name = web_app::GenerateApplicationNameFromURL(url);

  int index = browser->tab_strip_model()->GetIndexOfWebContents(contents);
  if (index >= 0)
    browser->tab_strip_model()->DetachWebContentsAt(index);

  Browser* app_browser = new Browser(Browser::CreateParams::CreateForApp(
      app_name, true /* trusted_source */, gfx::Rect(), browser->profile()));
  app_browser->tab_strip_model()->AppendWebContents(contents, true);

  contents->GetMutableRendererPrefs()->can_accept_load_drops = false;
  contents->GetRenderViewHost()->SyncRendererPrefs();
  app_browser->window()->Show();
}
#endif  // defined(ENABLE_EXTENSIONS)

}  // namespace chrome
