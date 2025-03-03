// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/alsa/alsa_input.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "media/audio/alsa/alsa_output.h"
#include "media/audio/alsa/alsa_util.h"
#include "media/audio/alsa/alsa_wrapper.h"
#include "media/audio/alsa/audio_manager_alsa.h"
#include "media/audio/audio_manager.h"

namespace media {

static const int kNumPacketsInRingBuffer = 3;

static const char kDefaultDevice1[] = "default";
static const char kDefaultDevice2[] = "plug:default";

const char AlsaPcmInputStream::kAutoSelectDevice[] = "";

AlsaPcmInputStream::AlsaPcmInputStream(AudioManagerBase* audio_manager,
                                       const std::string& device_name,
                                       const AudioParameters& params,
                                       AlsaWrapper* wrapper)
    : audio_manager_(audio_manager),
      device_name_(device_name),
      params_(params),
      bytes_per_buffer_(params.frames_per_buffer() *
                        (params.channels() * params.bits_per_sample()) /
                        8),
      wrapper_(wrapper),
      buffer_duration_(base::TimeDelta::FromMicroseconds(
          params.frames_per_buffer() * base::Time::kMicrosecondsPerSecond /
          static_cast<float>(params.sample_rate()))),
      callback_(NULL),
      device_handle_(NULL),
      mixer_handle_(NULL),
      mixer_element_handle_(NULL),
      read_callback_behind_schedule_(false),
      audio_bus_(AudioBus::Create(params)),
      weak_factory_(this) {
}

AlsaPcmInputStream::~AlsaPcmInputStream() {}

bool AlsaPcmInputStream::Open() {
  if (device_handle_)
    return false;  // Already open.

  snd_pcm_format_t pcm_format = alsa_util::BitsToFormat(
      params_.bits_per_sample());
  if (pcm_format == SND_PCM_FORMAT_UNKNOWN) {
    LOG(WARNING) << "Unsupported bits per sample: "
                 << params_.bits_per_sample();
    return false;
  }

  uint32_t latency_us =
      buffer_duration_.InMicroseconds() * kNumPacketsInRingBuffer;

  // Use the same minimum required latency as output.
  latency_us = std::max(latency_us, AlsaPcmOutputStream::kMinLatencyMicros);

  if (device_name_ == kAutoSelectDevice) {
    const char* device_names[] = { kDefaultDevice1, kDefaultDevice2 };
    for (size_t i = 0; i < arraysize(device_names); ++i) {
      device_handle_ = alsa_util::OpenCaptureDevice(
          wrapper_, device_names[i], params_.channels(),
          params_.sample_rate(), pcm_format, latency_us);

      if (device_handle_) {
        device_name_ = device_names[i];
        break;
      }
    }
  } else {
    device_handle_ = alsa_util::OpenCaptureDevice(wrapper_,
                                                  device_name_.c_str(),
                                                  params_.channels(),
                                                  params_.sample_rate(),
                                                  pcm_format, latency_us);
  }

  if (device_handle_) {
    audio_buffer_.reset(new uint8_t[bytes_per_buffer_]);

    // Open the microphone mixer.
    mixer_handle_ = alsa_util::OpenMixer(wrapper_, device_name_);
    if (mixer_handle_) {
      mixer_element_handle_ = alsa_util::LoadCaptureMixerElement(
          wrapper_, mixer_handle_);
    }
  }

  return device_handle_ != NULL;
}

void AlsaPcmInputStream::Start(AudioInputCallback* callback) {
  DCHECK(!callback_ && callback);
  callback_ = callback;
  StartAgc();
  int error = wrapper_->PcmPrepare(device_handle_);
  if (error < 0) {
    HandleError("PcmPrepare", error);
  } else {
    error = wrapper_->PcmStart(device_handle_);
    if (error < 0)
      HandleError("PcmStart", error);
  }

  if (error < 0) {
    callback_ = NULL;
  } else {
    // We start reading data half |buffer_duration_| later than when the
    // buffer might have got filled, to accommodate some delays in the audio
    // driver. This could also give us a smooth read sequence going forward.
    base::TimeDelta delay = buffer_duration_ + buffer_duration_ / 2;
    next_read_time_ = base::TimeTicks::Now() + delay;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&AlsaPcmInputStream::ReadAudio, weak_factory_.GetWeakPtr()),
        delay);
  }
}

bool AlsaPcmInputStream::Recover(int original_error) {
  int error = wrapper_->PcmRecover(device_handle_, original_error, 1);
  if (error < 0) {
    // Docs say snd_pcm_recover returns the original error if it is not one
    // of the recoverable ones, so this log message will probably contain the
    // same error twice.
    LOG(WARNING) << "Unable to recover from \""
                 << wrapper_->StrError(original_error) << "\": "
                 << wrapper_->StrError(error);
    return false;
  }

  if (original_error == -EPIPE) {  // Buffer underrun/overrun.
    // For capture streams we have to repeat the explicit start() to get
    // data flowing again.
    error = wrapper_->PcmStart(device_handle_);
    if (error < 0) {
      HandleError("PcmStart", error);
      return false;
    }
  }

  return true;
}

snd_pcm_sframes_t AlsaPcmInputStream::GetCurrentDelay() {
  snd_pcm_sframes_t delay = -1;

  int error = wrapper_->PcmDelay(device_handle_, &delay);
  if (error < 0)
    Recover(error);

  // snd_pcm_delay() may not work in the beginning of the stream. In this case
  // return delay of data we know currently is in the ALSA's buffer.
  if (delay < 0)
    delay = wrapper_->PcmAvailUpdate(device_handle_);

  return delay;
}

void AlsaPcmInputStream::ReadAudio() {
  DCHECK(callback_);

  snd_pcm_sframes_t frames = wrapper_->PcmAvailUpdate(device_handle_);
  if (frames < 0) {  // Potentially recoverable error?
    LOG(WARNING) << "PcmAvailUpdate(): " << wrapper_->StrError(frames);
    Recover(frames);
  }

  if (frames < params_.frames_per_buffer()) {
    // Not enough data yet or error happened. In both cases wait for a very
    // small duration before checking again.
    // Even Though read callback was behind schedule, there is no data, so
    // reset the next_read_time_.
    if (read_callback_behind_schedule_) {
      next_read_time_ = base::TimeTicks::Now();
      read_callback_behind_schedule_ = false;
    }

    base::TimeDelta next_check_time = buffer_duration_ / 2;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&AlsaPcmInputStream::ReadAudio, weak_factory_.GetWeakPtr()),
        next_check_time);
    return;
  }

  int num_buffers = frames / params_.frames_per_buffer();
  uint32_t hardware_delay_bytes =
      static_cast<uint32_t>(GetCurrentDelay() * params_.GetBytesPerFrame());
  double normalized_volume = 0.0;

  // Update the AGC volume level once every second. Note that, |volume| is
  // also updated each time SetVolume() is called through IPC by the
  // render-side AGC.
  GetAgcVolume(&normalized_volume);

  while (num_buffers--) {
    int frames_read = wrapper_->PcmReadi(device_handle_, audio_buffer_.get(),
                                         params_.frames_per_buffer());
    if (frames_read == params_.frames_per_buffer()) {
      audio_bus_->FromInterleaved(audio_buffer_.get(),
                                  audio_bus_->frames(),
                                  params_.bits_per_sample() / 8);
      callback_->OnData(
          this, audio_bus_.get(), hardware_delay_bytes, normalized_volume);
    } else if (frames_read < 0) {
      bool success = Recover(frames_read);
      LOG(WARNING) << "PcmReadi failed with error " << frames_read << ". "
                   << (success ? "Successfully" : "Unsuccessfully")
                   << " recovered.";
    } else {
      LOG(WARNING) << "PcmReadi returning less than expected frames: "
                   << frames_read << " vs. " << params_.frames_per_buffer()
                   << ". Dropping this buffer.";
    }
  }

  next_read_time_ += buffer_duration_;
  base::TimeDelta delay = next_read_time_ - base::TimeTicks::Now();
  if (delay < base::TimeDelta()) {
    DVLOG(1) << "Audio read callback behind schedule by "
             << (buffer_duration_ - delay).InMicroseconds()
             << " (us).";
    // Read callback is behind schedule. Assuming there is data pending in
    // the soundcard, invoke the read callback immediate in order to catch up.
    read_callback_behind_schedule_ = true;
    delay = base::TimeDelta();
  }

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&AlsaPcmInputStream::ReadAudio, weak_factory_.GetWeakPtr()),
      delay);
}

void AlsaPcmInputStream::Stop() {
  if (!device_handle_ || !callback_)
    return;

  StopAgc();

  weak_factory_.InvalidateWeakPtrs();  // Cancel the next scheduled read.
  int error = wrapper_->PcmDrop(device_handle_);
  if (error < 0)
    HandleError("PcmDrop", error);

  callback_ = NULL;
}

void AlsaPcmInputStream::Close() {
  if (device_handle_) {
    weak_factory_.InvalidateWeakPtrs();  // Cancel the next scheduled read.
    int error = alsa_util::CloseDevice(wrapper_, device_handle_);
    if (error < 0)
      HandleError("PcmClose", error);

    if (mixer_handle_)
      alsa_util::CloseMixer(wrapper_, mixer_handle_, device_name_);

    audio_buffer_.reset();
    device_handle_ = NULL;
    mixer_handle_ = NULL;
    mixer_element_handle_ = NULL;
  }

  audio_manager_->ReleaseInputStream(this);
}

double AlsaPcmInputStream::GetMaxVolume() {
  if (!mixer_handle_ || !mixer_element_handle_) {
    DLOG(WARNING) << "GetMaxVolume is not supported for " << device_name_;
    return 0.0;
  }

  if (!wrapper_->MixerSelemHasCaptureVolume(mixer_element_handle_)) {
    DLOG(WARNING) << "Unsupported microphone volume for " << device_name_;
    return 0.0;
  }

  long min = 0;
  long max = 0;
  if (wrapper_->MixerSelemGetCaptureVolumeRange(mixer_element_handle_,
                                                &min,
                                                &max)) {
    DLOG(WARNING) << "Unsupported max microphone volume for " << device_name_;
    return 0.0;
  }
  DCHECK(min == 0);
  DCHECK(max > 0);

  return static_cast<double>(max);
}

void AlsaPcmInputStream::SetVolume(double volume) {
  if (!mixer_handle_ || !mixer_element_handle_) {
    DLOG(WARNING) << "SetVolume is not supported for " << device_name_;
    return;
  }

  int error = wrapper_->MixerSelemSetCaptureVolumeAll(
      mixer_element_handle_, static_cast<long>(volume));
  if (error < 0) {
    DLOG(WARNING) << "Unable to set volume for " << device_name_;
  }

  // Update the AGC volume level based on the last setting above. Note that,
  // the volume-level resolution is not infinite and it is therefore not
  // possible to assume that the volume provided as input parameter can be
  // used directly. Instead, a new query to the audio hardware is required.
  // This method does nothing if AGC is disabled.
  UpdateAgcVolume();
}

double AlsaPcmInputStream::GetVolume() {
  if (!mixer_handle_ || !mixer_element_handle_) {
    DLOG(WARNING) << "GetVolume is not supported for " << device_name_;
    return 0.0;
  }

  long current_volume = 0;
  int error = wrapper_->MixerSelemGetCaptureVolume(
      mixer_element_handle_, static_cast<snd_mixer_selem_channel_id_t>(0),
      &current_volume);
  if (error < 0) {
    DLOG(WARNING) << "Unable to get volume for " << device_name_;
    return 0.0;
  }

  return static_cast<double>(current_volume);
}

bool AlsaPcmInputStream::IsMuted() {
  return false;
}

void AlsaPcmInputStream::HandleError(const char* method, int error) {
  LOG(WARNING) << method << ": " << wrapper_->StrError(error);
  callback_->OnError(this);
}

}  // namespace media
