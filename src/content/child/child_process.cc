// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/child_process.h"

#include <string.h>

#include "base/lazy_instance.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/statistics_recorder.h"
#include "base/process/process_handle.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread.h"
#include "base/threading/thread_local.h"
#include "build/build_config.h"
#include "content/child/child_thread_impl.h"

#if defined(OS_ANDROID)
#include "base/debug/debugger.h"
#endif

#if defined(OS_POSIX) && !defined(OS_ANDROID)
#include <signal.h>
static void SigUSR1Handler(int signal) { }
#endif

namespace content {

namespace {

base::LazyInstance<base::ThreadLocalPointer<ChildProcess> > g_lazy_tls =
    LAZY_INSTANCE_INITIALIZER;
}

ChildProcess::ChildProcess() : ChildProcess(base::ThreadPriority::NORMAL) {}

ChildProcess::ChildProcess(base::ThreadPriority io_thread_priority)
    : ref_count_(0),
      shutdown_timeout_minutes_(0),
      shutdown_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                      base::WaitableEvent::InitialState::NOT_SIGNALED),
      io_thread_("Chrome_ChildIOThread") {
  DCHECK(!g_lazy_tls.Pointer()->Get());
  g_lazy_tls.Pointer()->Set(this);

  base::StatisticsRecorder::Initialize();

  // We can't recover from failing to start the IO thread.
  base::Thread::Options thread_options(base::MessageLoop::TYPE_IO, 0);
  thread_options.priority = io_thread_priority;
#if defined(OS_ANDROID)
  // TODO(reveman): Remove this in favor of setting it explicitly for each type
  // of process.
  thread_options.priority = base::ThreadPriority::DISPLAY;
#endif
  CHECK(io_thread_.StartWithOptions(thread_options));
}

ChildProcess::~ChildProcess() {
  DCHECK(g_lazy_tls.Pointer()->Get() == this);

  // Signal this event before destroying the child process.  That way all
  // background threads can cleanup.
  // For example, in the renderer the RenderThread instances will be able to
  // notice shutdown before the render process begins waiting for them to exit.
  shutdown_event_.Signal();

  // Kill the main thread object before nulling child_process, since
  // destruction code might depend on it.
  if (main_thread_) {  // null in unittests.
    main_thread_->Shutdown();
    main_thread_.reset();
  }

  g_lazy_tls.Pointer()->Set(NULL);
  io_thread_.Stop();
}

ChildThreadImpl* ChildProcess::main_thread() {
  return main_thread_.get();
}

void ChildProcess::set_main_thread(ChildThreadImpl* thread) {
  main_thread_.reset(thread);
}

void ChildProcess::AddRefProcess() {
  DCHECK(!main_thread_.get() ||  // null in unittests.
         base::MessageLoop::current() == main_thread_->message_loop());
  ref_count_++;

  shutdown_timer_.Stop();
}

void ChildProcess::ReleaseProcess() {
  DCHECK(!main_thread_.get() ||  // null in unittests.
         base::MessageLoop::current() == main_thread_->message_loop());
  DCHECK(ref_count_);
  if (--ref_count_)
    return;

  if (shutdown_timeout_minutes_) {
    shutdown_timer_.Start(FROM_HERE,
                          base::TimeDelta::FromMinutes(shutdown_timeout_minutes_),
                          this,
                          &ChildProcess::ShutdownTimeoutFired);
    return;
  }

  if (main_thread_)  // null in unittests.
    main_thread_->OnProcessFinalRelease();
}

void ChildProcess::ShutdownTimeoutFired() {
  if (main_thread_)
    main_thread_->OnProcessFinalRelease();
}

void ChildProcess::ShutdownIfIdle() {
  if (shutdown_timer_.IsRunning()) {
    shutdown_timer_.Stop();
    ShutdownTimeoutFired();
  }
}

ChildProcess* ChildProcess::current() {
  return g_lazy_tls.Pointer()->Get();
}

base::WaitableEvent* ChildProcess::GetShutDownEvent() {
  return &shutdown_event_;
}

void ChildProcess::WaitForDebugger(const std::string& label) {
#if defined(OS_WIN)
#if defined(GOOGLE_CHROME_BUILD)
  std::string title = "Google Chrome";
#else  // CHROMIUM_BUILD
  std::string title = "Chromium";
#endif  // CHROMIUM_BUILD
  title += " ";
  title += label;  // makes attaching to process easier
  std::string message = label;
  message += " starting with pid: ";
  message += base::IntToString(base::GetCurrentProcId());
  ::MessageBox(NULL, base::UTF8ToWide(message).c_str(),
               base::UTF8ToWide(title).c_str(),
               MB_OK | MB_SETFOREGROUND);
#elif defined(OS_POSIX)
#if defined(OS_ANDROID)
  LOG(ERROR) << label << " waiting for GDB.";
  // Wait 24 hours for a debugger to be attached to the current process.
  base::debug::WaitForDebugger(24 * 60 * 60, true);
#else
  // TODO(playmobil): In the long term, overriding this flag doesn't seem
  // right, either use our own flag or open a dialog we can use.
  // This is just to ease debugging in the interim.
  LOG(ERROR) << label
             << " ("
             << getpid()
             << ") paused waiting for debugger to attach. "
             << "Send SIGUSR1 to unpause.";
  // Install a signal handler so that pause can be woken.
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SigUSR1Handler;
  sigaction(SIGUSR1, &sa, NULL);

  pause();
#endif  // defined(OS_ANDROID)
#endif  // defined(OS_POSIX)
}

}  // namespace content
