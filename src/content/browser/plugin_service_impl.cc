// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/plugin_service_impl.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/metrics/histogram.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "content/browser/ppapi_plugin_process_host.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/common/content_switches_internal.h"
#include "content/common/pepper_plugin_list.h"
#include "content/common/plugin_list.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/plugin_service_filter.h"
#include "content/public/browser/resource_context.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/webplugininfo.h"

#if defined(OS_WIN)
#include "content/common/plugin_constants_win.h"
#include "ui/gfx/win/hwnd_util.h"
#endif

#if defined(OS_POSIX)
#include "content/browser/plugin_loader_posix.h"
#endif

#if defined(OS_POSIX) && !defined(OS_OPENBSD) && !defined(OS_ANDROID)
using ::base::FilePathWatcher;
#endif

namespace content {
namespace {

// This enum is used to collect Flash usage data.
enum FlashUsage {
  // Number of browser processes that have started at least one NPAPI Flash
  // process during their lifetime.
  START_NPAPI_FLASH_AT_LEAST_ONCE,
  // Number of browser processes that have started at least one PPAPI Flash
  // process during their lifetime.
  START_PPAPI_FLASH_AT_LEAST_ONCE,
  // Total number of browser processes.
  TOTAL_BROWSER_PROCESSES,
  FLASH_USAGE_ENUM_COUNT
};

enum NPAPIPluginStatus {
  // Platform does not support NPAPI.
  NPAPI_STATUS_UNSUPPORTED,
  // Platform supports NPAPI and NPAPI is disabled.
  NPAPI_STATUS_DISABLED,
  // Platform supports NPAPI and NPAPI is enabled.
  NPAPI_STATUS_ENABLED,
  NPAPI_STATUS_ENUM_COUNT
};

bool LoadPluginListInProcess() {
#if defined(OS_WIN)
  return true;
#else
  // If on POSIX, we don't want to load the list of NPAPI plugins in-process as
  // that causes instability.

  // Can't load the plugins on the utility thread when in single process mode
  // since that requires GTK which can only be used on the main thread.
  if (RenderProcessHost::run_renderer_in_process())
    return true;

  return !PluginService::GetInstance()->NPAPIPluginsSupported();
#endif
}

// Callback set on the PluginList to assert that plugin loading happens on the
// correct thread.
void WillLoadPluginsCallback(
    base::SequencedWorkerPool::SequenceToken token) {
  if (LoadPluginListInProcess()) {
    CHECK(BrowserThread::GetBlockingPool()->IsRunningSequenceOnCurrentThread(
        token));
  } else {
    CHECK(false) << "Plugin loading should happen out-of-process.";
  }
}

#if defined(OS_MACOSX)
void NotifyPluginsOfActivation() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  for (PluginProcessHostIterator iter; !iter.Done(); ++iter)
    iter->OnAppActivation();
}
#endif

#if defined(OS_POSIX)
#if !defined(OS_OPENBSD) && !defined(OS_ANDROID)
void NotifyPluginDirChanged(const base::FilePath& path, bool error) {
  if (error) {
    // TODO(pastarmovj): Add some sensible error handling. Maybe silently
    // stopping the watcher would be enough. Or possibly restart it.
    NOTREACHED();
    return;
  }
  VLOG(1) << "Watched path changed: " << path.value();
  // Make the plugin list update itself
  PluginList::Singleton()->RefreshPlugins();
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PluginService::PurgePluginListCache,
                 static_cast<BrowserContext*>(NULL), false));
}
#endif  // !defined(OS_OPENBSD) && !defined(OS_ANDROID)

void ForwardCallback(base::SingleThreadTaskRunner* target_task_runner,
                     const PluginService::GetPluginsCallback& callback,
                     const std::vector<WebPluginInfo>& plugins) {
  target_task_runner->PostTask(FROM_HERE, base::Bind(callback, plugins));
}
#endif  // defined(OS_POSIX)

}  // namespace

// static
PluginService* PluginService::GetInstance() {
  return PluginServiceImpl::GetInstance();
}

void PluginService::PurgePluginListCache(BrowserContext* browser_context,
                                         bool reload_pages) {
  for (RenderProcessHost::iterator it = RenderProcessHost::AllHostsIterator();
       !it.IsAtEnd(); it.Advance()) {
    RenderProcessHost* host = it.GetCurrentValue();
    if (!browser_context || host->GetBrowserContext() == browser_context)
      host->Send(new ViewMsg_PurgePluginListCache(reload_pages));
  }
}

// static
PluginServiceImpl* PluginServiceImpl::GetInstance() {
  return base::Singleton<PluginServiceImpl>::get();
}

PluginServiceImpl::PluginServiceImpl()
    : npapi_plugins_enabled_(false), filter_(NULL) {
  // Collect the total number of browser processes (which create
  // PluginServiceImpl objects, to be precise). The number is used to normalize
  // the number of processes which start at least one NPAPI/PPAPI Flash process.
  static bool counted = false;
  if (!counted) {
    counted = true;
    UMA_HISTOGRAM_ENUMERATION("Plugin.FlashUsage", TOTAL_BROWSER_PROCESSES,
                              FLASH_USAGE_ENUM_COUNT);
  }
}

PluginServiceImpl::~PluginServiceImpl() {
  // Make sure no plugin channel requests have been leaked.
  DCHECK(pending_plugin_clients_.empty());
}

void PluginServiceImpl::Init() {
  plugin_list_token_ = base::SequencedWorkerPool::GetSequenceToken();
  PluginList::Singleton()->set_will_load_plugins_callback(
      base::Bind(&WillLoadPluginsCallback, plugin_list_token_));

  RegisterPepperPlugins();

  // Load any specified on the command line as well.
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  base::FilePath path =
      command_line->GetSwitchValuePath(switches::kLoadPlugin);
  if (!path.empty())
    AddExtraPluginPath(path);
  path = command_line->GetSwitchValuePath(switches::kExtraPluginDir);
  if (!path.empty())
    PluginList::Singleton()->AddExtraPluginDir(path);

  if (command_line->HasSwitch(switches::kDisablePluginsDiscovery))
    PluginList::Singleton()->DisablePluginsDiscovery();
}

void PluginServiceImpl::StartWatchingPlugins() {
  // Start watching for changes in the plugin list. This means watching
  // for changes in the Windows registry keys and on both Windows and POSIX
  // watch for changes in the paths that are expected to contain plugins.
#if defined(OS_WIN)
  if (hkcu_key_.Create(HKEY_CURRENT_USER,
                       kRegistryMozillaPlugins,
                       KEY_NOTIFY) == ERROR_SUCCESS) {
    base::win::RegKey::ChangeCallback callback =
        base::Bind(&PluginServiceImpl::OnKeyChanged, base::Unretained(this),
                   base::Unretained(&hkcu_key_));
    hkcu_key_.StartWatching(callback);
  }
  if (hklm_key_.Create(HKEY_LOCAL_MACHINE,
                       kRegistryMozillaPlugins,
                       KEY_NOTIFY) == ERROR_SUCCESS) {
    base::win::RegKey::ChangeCallback callback =
        base::Bind(&PluginServiceImpl::OnKeyChanged, base::Unretained(this),
                   base::Unretained(&hklm_key_));
    hklm_key_.StartWatching(callback);
  }
#endif
#if defined(OS_POSIX) && !defined(OS_OPENBSD) && !defined(OS_ANDROID)
// On ChromeOS the user can't install plugins anyway and on Windows all
// important plugins register themselves in the registry so no need to do that.

  // Get the list of all paths for registering the FilePathWatchers
  // that will track and if needed reload the list of plugins on runtime.
  std::vector<base::FilePath> plugin_dirs;
  PluginList::Singleton()->GetPluginDirectories(&plugin_dirs);

  for (size_t i = 0; i < plugin_dirs.size(); ++i) {
    // FilePathWatcher can not handle non-absolute paths under windows.
    // We don't watch for file changes in windows now but if this should ever
    // be extended to Windows these lines might save some time of debugging.
#if defined(OS_WIN)
    if (!plugin_dirs[i].IsAbsolute())
      continue;
#endif
    FilePathWatcher* watcher = new FilePathWatcher();
    VLOG(1) << "Watching for changes in: " << plugin_dirs[i].value();
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(&PluginServiceImpl::RegisterFilePathWatcher, watcher,
                   plugin_dirs[i]));
    file_watchers_.push_back(watcher);
  }
#endif
}

PluginProcessHost* PluginServiceImpl::FindNpapiPluginProcess(
    const base::FilePath& plugin_path) {
  for (PluginProcessHostIterator iter; !iter.Done(); ++iter) {
    if (iter->info().path == plugin_path)
      return *iter;
  }

  return NULL;
}

PpapiPluginProcessHost* PluginServiceImpl::FindPpapiPluginProcess(
    const base::FilePath& plugin_path,
    const base::FilePath& profile_data_directory) {
  for (PpapiPluginProcessHostIterator iter; !iter.Done(); ++iter) {
    if (iter->plugin_path() == plugin_path &&
        iter->profile_data_directory() == profile_data_directory) {
      return *iter;
    }
  }
  return NULL;
}

PpapiPluginProcessHost* PluginServiceImpl::FindPpapiBrokerProcess(
    const base::FilePath& broker_path) {
  for (PpapiBrokerProcessHostIterator iter; !iter.Done(); ++iter) {
    if (iter->plugin_path() == broker_path)
      return *iter;
  }

  return NULL;
}

PluginProcessHost* PluginServiceImpl::FindOrStartNpapiPluginProcess(
    int render_process_id,
    const base::FilePath& plugin_path) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (filter_ && !filter_->CanLoadPlugin(render_process_id, plugin_path))
    return NULL;

  PluginProcessHost* plugin_host = FindNpapiPluginProcess(plugin_path);
  if (plugin_host)
    return plugin_host;

  WebPluginInfo info;
  if (!GetPluginInfoByPath(plugin_path, &info)) {
    return NULL;
  }

  // Record when NPAPI Flash process is started for the first time.
  static bool counted = false;
  if (!counted && base::UTF16ToUTF8(info.name) == kFlashPluginName) {
    counted = true;
    UMA_HISTOGRAM_ENUMERATION("Plugin.FlashUsage",
                              START_NPAPI_FLASH_AT_LEAST_ONCE,
                              FLASH_USAGE_ENUM_COUNT);
  }
#if defined(OS_CHROMEOS)
  // TODO(ihf): Move to an earlier place once crbug.com/314301 is fixed. For now
  // we still want Plugin.FlashUsage recorded if we end up here.
  LOG(WARNING) << "Refusing to start npapi plugin on ChromeOS.";
  return NULL;
#endif
  // This plugin isn't loaded by any plugin process, so create a new process.
  std::unique_ptr<PluginProcessHost> new_host(new PluginProcessHost());
  if (!new_host->Init(info)) {
    NOTREACHED();  // Init is not expected to fail.
    return NULL;
  }
  return new_host.release();
}

PpapiPluginProcessHost* PluginServiceImpl::FindOrStartPpapiPluginProcess(
    int render_process_id,
    const base::FilePath& plugin_path,
    const base::FilePath& profile_data_directory) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (filter_ && !filter_->CanLoadPlugin(render_process_id, plugin_path)) {
    VLOG(1) << "Unable to load ppapi plugin: " << plugin_path.MaybeAsASCII();
    return NULL;
  }

  PpapiPluginProcessHost* plugin_host =
      FindPpapiPluginProcess(plugin_path, profile_data_directory);
  if (plugin_host)
    return plugin_host;

  // Validate that the plugin is actually registered.
  PepperPluginInfo* info = GetRegisteredPpapiPluginInfo(plugin_path);
  if (!info) {
    VLOG(1) << "Unable to find ppapi plugin registration for: "
            << plugin_path.MaybeAsASCII();
    return NULL;
  }

  // Record when PPAPI Flash process is started for the first time.
  static bool counted = false;
  if (!counted && info->name == kFlashPluginName) {
    counted = true;
    UMA_HISTOGRAM_ENUMERATION("Plugin.FlashUsage",
                              START_PPAPI_FLASH_AT_LEAST_ONCE,
                              FLASH_USAGE_ENUM_COUNT);
  }

  // This plugin isn't loaded by any plugin process, so create a new process.
  plugin_host = PpapiPluginProcessHost::CreatePluginHost(
      *info, profile_data_directory);
  if (!plugin_host) {
    VLOG(1) << "Unable to create ppapi plugin process for: "
            << plugin_path.MaybeAsASCII();
  }

  return plugin_host;
}

PpapiPluginProcessHost* PluginServiceImpl::FindOrStartPpapiBrokerProcess(
    int render_process_id,
    const base::FilePath& plugin_path) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (filter_ && !filter_->CanLoadPlugin(render_process_id, plugin_path))
    return NULL;

  PpapiPluginProcessHost* plugin_host = FindPpapiBrokerProcess(plugin_path);
  if (plugin_host)
    return plugin_host;

  // Validate that the plugin is actually registered.
  PepperPluginInfo* info = GetRegisteredPpapiPluginInfo(plugin_path);
  if (!info)
    return NULL;

  // TODO(ddorwin): Uncomment once out of process is supported.
  // DCHECK(info->is_out_of_process);

  // This broker isn't loaded by any broker process, so create a new process.
  return PpapiPluginProcessHost::CreateBrokerHost(*info);
}

void PluginServiceImpl::OpenChannelToNpapiPlugin(
    int render_process_id,
    int render_frame_id,
    const GURL& url,
    const GURL& page_url,
    const std::string& mime_type,
    PluginProcessHost::Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(!ContainsKey(pending_plugin_clients_, client));
  pending_plugin_clients_.insert(client);

  // Make sure plugins are loaded if necessary.
  PluginServiceFilterParams params = {
    render_process_id,
    render_frame_id,
    page_url,
    client->GetResourceContext()
  };
  GetPlugins(base::Bind(
      &PluginServiceImpl::ForwardGetAllowedPluginForOpenChannelToPlugin,
      base::Unretained(this), params, url, mime_type, client));
}

void PluginServiceImpl::OpenChannelToPpapiPlugin(
    int render_process_id,
    const base::FilePath& plugin_path,
    const base::FilePath& profile_data_directory,
    PpapiPluginProcessHost::PluginClient* client) {
  PpapiPluginProcessHost* plugin_host = FindOrStartPpapiPluginProcess(
      render_process_id, plugin_path, profile_data_directory);
  if (plugin_host) {
    plugin_host->OpenChannelToPlugin(client);
  } else {
    // Send error.
    client->OnPpapiChannelOpened(IPC::ChannelHandle(), base::kNullProcessId, 0);
  }
}

void PluginServiceImpl::OpenChannelToPpapiBroker(
    int render_process_id,
    const base::FilePath& path,
    PpapiPluginProcessHost::BrokerClient* client) {
  PpapiPluginProcessHost* plugin_host = FindOrStartPpapiBrokerProcess(
      render_process_id, path);
  if (plugin_host) {
    plugin_host->OpenChannelToPlugin(client);
  } else {
    // Send error.
    client->OnPpapiChannelOpened(IPC::ChannelHandle(), base::kNullProcessId, 0);
  }
}

void PluginServiceImpl::CancelOpenChannelToNpapiPlugin(
    PluginProcessHost::Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(ContainsKey(pending_plugin_clients_, client));
  pending_plugin_clients_.erase(client);
}

void PluginServiceImpl::ForwardGetAllowedPluginForOpenChannelToPlugin(
    const PluginServiceFilterParams& params,
    const GURL& url,
    const std::string& mime_type,
    PluginProcessHost::Client* client,
    const std::vector<WebPluginInfo>&) {
  GetAllowedPluginForOpenChannelToPlugin(
      params.render_process_id, params.render_frame_id, url, params.page_url,
      mime_type, client, params.resource_context);
}

void PluginServiceImpl::GetAllowedPluginForOpenChannelToPlugin(
    int render_process_id,
    int render_frame_id,
    const GURL& url,
    const GURL& page_url,
    const std::string& mime_type,
    PluginProcessHost::Client* client,
    ResourceContext* resource_context) {
  WebPluginInfo info;
  bool allow_wildcard = true;
  bool found = GetPluginInfo(
      render_process_id, render_frame_id, resource_context,
      url, page_url, mime_type, allow_wildcard,
      NULL, &info, NULL);
  base::FilePath plugin_path;
  if (found)
    plugin_path = info.path;

  // Now we jump back to the IO thread to finish opening the channel.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&PluginServiceImpl::FinishOpenChannelToPlugin,
                 base::Unretained(this),
                 render_process_id,
                 plugin_path,
                 client));
  if (filter_) {
    DCHECK_EQ(WebPluginInfo::PLUGIN_TYPE_NPAPI, info.type);
    filter_->NPAPIPluginLoaded(render_process_id, render_frame_id, mime_type,
                               info);
  }
}

void PluginServiceImpl::FinishOpenChannelToPlugin(
    int render_process_id,
    const base::FilePath& plugin_path,
    PluginProcessHost::Client* client) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // Make sure it hasn't been canceled yet.
  if (!ContainsKey(pending_plugin_clients_, client))
    return;
  pending_plugin_clients_.erase(client);

  PluginProcessHost* plugin_host = FindOrStartNpapiPluginProcess(
      render_process_id, plugin_path);
  if (plugin_host) {
    client->OnFoundPluginProcessHost(plugin_host);
    plugin_host->OpenChannelToPlugin(client);
  } else {
    client->OnError();
  }
}

bool PluginServiceImpl::GetPluginInfoArray(
    const GURL& url,
    const std::string& mime_type,
    bool allow_wildcard,
    std::vector<WebPluginInfo>* plugins,
    std::vector<std::string>* actual_mime_types) {
  bool use_stale = false;
  PluginList::Singleton()->GetPluginInfoArray(
      url, mime_type, allow_wildcard, &use_stale, NPAPIPluginsSupported(),
      plugins, actual_mime_types);
  return use_stale;
}

bool PluginServiceImpl::GetPluginInfo(int render_process_id,
                                      int render_frame_id,
                                      ResourceContext* context,
                                      const GURL& url,
                                      const GURL& page_url,
                                      const std::string& mime_type,
                                      bool allow_wildcard,
                                      bool* is_stale,
                                      WebPluginInfo* info,
                                      std::string* actual_mime_type) {
  std::vector<WebPluginInfo> plugins;
  std::vector<std::string> mime_types;
  bool stale = GetPluginInfoArray(
      url, mime_type, allow_wildcard, &plugins, &mime_types);
  if (is_stale)
    *is_stale = stale;

  for (size_t i = 0; i < plugins.size(); ++i) {
    if (!filter_ || filter_->IsPluginAvailable(render_process_id,
                                               render_frame_id,
                                               context,
                                               url,
                                               page_url,
                                               &plugins[i])) {
      *info = plugins[i];
      if (actual_mime_type)
        *actual_mime_type = mime_types[i];
      return true;
    }
  }
  return false;
}

bool PluginServiceImpl::GetPluginInfoByPath(const base::FilePath& plugin_path,
                                            WebPluginInfo* info) {
  std::vector<WebPluginInfo> plugins;
  PluginList::Singleton()->GetPluginsNoRefresh(&plugins);

  for (std::vector<WebPluginInfo>::iterator it = plugins.begin();
       it != plugins.end();
       ++it) {
    if (it->path == plugin_path) {
      *info = *it;
      return true;
    }
  }

  return false;
}

base::string16 PluginServiceImpl::GetPluginDisplayNameByPath(
    const base::FilePath& path) {
  base::string16 plugin_name = path.LossyDisplayName();
  WebPluginInfo info;
  if (PluginService::GetInstance()->GetPluginInfoByPath(path, &info) &&
      !info.name.empty()) {
    plugin_name = info.name;
#if defined(OS_MACOSX)
    // Many plugins on the Mac have .plugin in the actual name, which looks
    // terrible, so look for that and strip it off if present.
    const std::string kPluginExtension = ".plugin";
    if (base::EndsWith(plugin_name, base::ASCIIToUTF16(kPluginExtension),
                       base::CompareCase::SENSITIVE))
      plugin_name.erase(plugin_name.length() - kPluginExtension.length());
#endif  // OS_MACOSX
  }
  return plugin_name;
}

void PluginServiceImpl::GetPlugins(const GetPluginsCallback& callback) {
  scoped_refptr<base::SingleThreadTaskRunner> target_task_runner(
      base::ThreadTaskRunnerHandle::Get());

  if (LoadPluginListInProcess()) {
    BrowserThread::GetBlockingPool()
        ->PostSequencedWorkerTaskWithShutdownBehavior(
            plugin_list_token_, FROM_HERE,
            base::Bind(&PluginServiceImpl::GetPluginsInternal,
                       base::Unretained(this),
                       base::RetainedRef(target_task_runner), callback),
            base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
    return;
  }
#if defined(OS_POSIX)
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::Bind(&PluginServiceImpl::GetPluginsOnIOThread,
                 base::Unretained(this), base::RetainedRef(target_task_runner),
                 callback));
#else
  NOTREACHED();
#endif
}

void PluginServiceImpl::GetPluginsInternal(
    base::SingleThreadTaskRunner* target_task_runner,
    const PluginService::GetPluginsCallback& callback) {
  DCHECK(BrowserThread::GetBlockingPool()->IsRunningSequenceOnCurrentThread(
      plugin_list_token_));

  std::vector<WebPluginInfo> plugins;
  PluginList::Singleton()->GetPlugins(&plugins, NPAPIPluginsSupported());

  target_task_runner->PostTask(FROM_HERE, base::Bind(callback, plugins));
}

#if defined(OS_POSIX)
void PluginServiceImpl::GetPluginsOnIOThread(
    base::SingleThreadTaskRunner* target_task_runner,
    const GetPluginsCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // If we switch back to loading plugins in process, then we need to make
  // sure g_thread_init() gets called since plugins may call glib at load.

  if (!plugin_loader_.get())
    plugin_loader_ = new PluginLoaderPosix;

  plugin_loader_->GetPlugins(base::Bind(
      &ForwardCallback, base::RetainedRef(target_task_runner), callback));
}
#endif

#if defined(OS_WIN)
void PluginServiceImpl::OnKeyChanged(base::win::RegKey* key) {
  key->StartWatching(base::Bind(&PluginServiceImpl::OnKeyChanged,
                                base::Unretained(this),
                                base::Unretained(key)));

  PluginList::Singleton()->RefreshPlugins();
  PurgePluginListCache(NULL, false);
}
#endif  // defined(OS_WIN)

void PluginServiceImpl::RegisterPepperPlugins() {
  ComputePepperPluginList(&ppapi_plugins_);
  for (size_t i = 0; i < ppapi_plugins_.size(); ++i) {
    RegisterInternalPlugin(ppapi_plugins_[i].ToWebPluginInfo(), true);
  }
}

// There should generally be very few plugins so a brute-force search is fine.
PepperPluginInfo* PluginServiceImpl::GetRegisteredPpapiPluginInfo(
    const base::FilePath& plugin_path) {
  PepperPluginInfo* info = NULL;
  for (size_t i = 0; i < ppapi_plugins_.size(); ++i) {
    if (ppapi_plugins_[i].path == plugin_path) {
      info = &ppapi_plugins_[i];
      break;
    }
  }
  if (info)
    return info;
  // We did not find the plugin in our list. But wait! the plugin can also
  // be a latecomer, as it happens with pepper flash. This information
  // can be obtained from the PluginList singleton and we can use it to
  // construct it and add it to the list. This same deal needs to be done
  // in the renderer side in PepperPluginRegistry.
  WebPluginInfo webplugin_info;
  if (!GetPluginInfoByPath(plugin_path, &webplugin_info))
    return NULL;
  PepperPluginInfo new_pepper_info;
  if (!MakePepperPluginInfo(webplugin_info, &new_pepper_info))
    return NULL;
  ppapi_plugins_.push_back(new_pepper_info);
  return &ppapi_plugins_[ppapi_plugins_.size() - 1];
}

#if defined(OS_POSIX) && !defined(OS_OPENBSD) && !defined(OS_ANDROID)
// static
void PluginServiceImpl::RegisterFilePathWatcher(FilePathWatcher* watcher,
                                                const base::FilePath& path) {
  bool result = watcher->Watch(path, false,
                               base::Bind(&NotifyPluginDirChanged));
  DCHECK(result);
}
#endif

void PluginServiceImpl::SetFilter(PluginServiceFilter* filter) {
  filter_ = filter;
}

PluginServiceFilter* PluginServiceImpl::GetFilter() {
  return filter_;
}

void PluginServiceImpl::ForcePluginShutdown(const base::FilePath& plugin_path) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&PluginServiceImpl::ForcePluginShutdown,
                   base::Unretained(this), plugin_path));
    return;
  }

  PluginProcessHost* plugin = FindNpapiPluginProcess(plugin_path);
  if (plugin)
    plugin->ForceShutdown();
}

void PluginServiceImpl::ShutdownAllPluginProcessIfIdle() {
  // for npapi plugins
  for (PluginProcessHostIterator iter; !iter.Done(); ++iter)
    iter->ShutdownIfIdle();
}

static const unsigned int kMaxCrashesPerInterval = 3;
static const unsigned int kCrashesInterval = 120;

void PluginServiceImpl::RegisterPluginCrash(const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  std::map<base::FilePath, std::vector<base::Time> >::iterator i =
      crash_times_.find(path);
  if (i == crash_times_.end()) {
    crash_times_[path] = std::vector<base::Time>();
    i = crash_times_.find(path);
  }
  if (i->second.size() == kMaxCrashesPerInterval) {
    i->second.erase(i->second.begin());
  }
  base::Time time = base::Time::Now();
  i->second.push_back(time);
}

bool PluginServiceImpl::IsPluginUnstable(const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  std::map<base::FilePath, std::vector<base::Time> >::const_iterator i =
      crash_times_.find(path);
  if (i == crash_times_.end()) {
    return false;
  }
  if (i->second.size() != kMaxCrashesPerInterval) {
    return false;
  }
  base::TimeDelta delta = base::Time::Now() - i->second[0];
  return delta.InSeconds() <= kCrashesInterval;
}

void PluginServiceImpl::RefreshPlugins() {
  PluginList::Singleton()->RefreshPlugins();
}

void PluginServiceImpl::AddExtraPluginPath(const base::FilePath& path) {
  if (!NPAPIPluginsSupported()) {
    // TODO(jam): remove and just have CHECK once we're sure this doesn't get
    // triggered.
    DVLOG(0) << "NPAPI plugins not supported";
    return;
  }
  PluginList::Singleton()->AddExtraPluginPath(path);
}

void PluginServiceImpl::RemoveExtraPluginPath(const base::FilePath& path) {
  PluginList::Singleton()->RemoveExtraPluginPath(path);
}

void PluginServiceImpl::AddExtraPluginDir(const base::FilePath& path) {
  PluginList::Singleton()->AddExtraPluginDir(path);
}

void PluginServiceImpl::RegisterInternalPlugin(
    const WebPluginInfo& info,
    bool add_at_beginning) {
  // Internal plugins should never be NPAPI.
  CHECK_NE(info.type, WebPluginInfo::PLUGIN_TYPE_NPAPI);
  if (info.type == WebPluginInfo::PLUGIN_TYPE_NPAPI) {
    DVLOG(0) << "Don't register NPAPI plugins when they're not supported";
    return;
  }
  PluginList::Singleton()->RegisterInternalPlugin(info, add_at_beginning);
}

void PluginServiceImpl::UnregisterInternalPlugin(const base::FilePath& path) {
  PluginList::Singleton()->UnregisterInternalPlugin(path);
}

void PluginServiceImpl::GetInternalPlugins(
    std::vector<WebPluginInfo>* plugins) {
  PluginList::Singleton()->GetInternalPlugins(plugins);
}

bool PluginServiceImpl::NPAPIPluginsSupported() {
#if defined(OS_WIN) || defined(OS_MACOSX)
  npapi_plugins_enabled_ = GetContentClient()->browser()->IsNPAPIEnabled();
#if defined(OS_WIN)
  // NPAPI plugins don't play well with Win32k renderer lockdown.
  if (npapi_plugins_enabled_)
    DisableWin32kRendererLockdown();
#endif
  NPAPIPluginStatus status =
      npapi_plugins_enabled_ ? NPAPI_STATUS_ENABLED : NPAPI_STATUS_DISABLED;
#else
#if defined(WEBOS_PLUGIN_SUPPORT)
  npapi_plugins_enabled_ = true;
  NPAPIPluginStatus status = NPAPI_STATUS_ENABLED;
#else
  NPAPIPluginStatus status = NPAPI_STATUS_UNSUPPORTED;
#endif
#endif
  UMA_HISTOGRAM_ENUMERATION("Plugin.NPAPIStatus", status,
                            NPAPI_STATUS_ENUM_COUNT);

  return npapi_plugins_enabled_;
}

void PluginServiceImpl::DisablePluginsDiscoveryForTesting() {
  PluginList::Singleton()->DisablePluginsDiscovery();
}

#if defined(OS_MACOSX)
void PluginServiceImpl::AppActivated() {
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                          base::Bind(&NotifyPluginsOfActivation));
}
#endif

bool PluginServiceImpl::PpapiDevChannelSupported(
    BrowserContext* browser_context,
    const GURL& document_url) {
  return GetContentClient()->browser()->IsPluginAllowedToUseDevChannelAPIs(
      browser_context, document_url);
}

}  // namespace content
