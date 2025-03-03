// Copyright (c) 2015-2018 LG Electronics, Inc.
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

#ifndef MEDIA_BLINK_WEBOS_TV_PIPELINE_H_
#define MEDIA_BLINK_WEBOS_TV_PIPELINE_H_

#include <string>
#include <cstdint>
#include <memory>

#include <Acb.h>
#include "base/time/time.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "media/base/pipeline.h"
#include "media/blink/webmediaplayer_util.h"
#include "media/blink/webos/custom_pipeline.h"
#include "media/webos/base/lunaservice_client.h"
#include "third_party/WebKit/public/platform/WebRect.h"

class GURL;

namespace media {

class TvPipeline : public CustomPipeline,
                   public base::RefCountedThreadSafe<TvPipeline> {
 public:
  TvPipeline(const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
             const std::string& app_id);
  void Load(bool is_video,
            const std::string& app_id,
            const GURL& url,
            const std::string& mime_type,
            const std::string& payload,
            const CustomPipeline::BufferingStateCB& buffering_state_cb,
            const base::Closure& video_display_window_change_cb,
            const CustomPipeline::PipelineMsgCB& pipeline_message_cb) override;
  void Dispose() override;
  void SetPlaybackRate(float playback_rate) override;
  float GetPlaybackRate() const override { return playback_rate_; }
  std::string MediaId() const override { return broadcast_id_; }
  double Duration() override { return 0; }
  base::TimeDelta CurrentTime() override {
    return base::TimeDelta::FromMilliseconds(0);
  }
  float BufferEnd() override { return static_cast<float>(0); }
  bool HasVideo() override { return has_video_; }
  bool HasAudio() override { return has_audio_; }
  gfx::Size NaturalVideoSize() override { return gfx::Size(1920, 1080); }
  void SetDisplayWindow(const blink::WebRect&,
                        const blink::WebRect&,
                        bool fullScreen,
                        bool forced = false) override;
  bool DidLoadingProgress() override { return false; }
  virtual void ContentsPositionChanged(float position) override;

 private:
  friend class base::RefCountedThreadSafe<TvPipeline>;
  virtual ~TvPipeline();

  bool loaded_;
  bool need_change_channel_;
  bool is_first_use_;
  bool is_suspended_;
  bool has_video_;
  bool has_audio_;
  float playback_rate_;
  const scoped_refptr<base::SingleThreadTaskRunner> media_task_runner_;
  media::LunaServiceClient luna_service_client_;
  std::unique_ptr<Acb> acb_client_;
  long acb_id_;
  long acb_load_task_id_;
  long acb_suspend_task_id_;
  std::string app_id_;
  GURL url_;
  std::string mime_type_;
  base::Closure video_display_window_change_cb_;
  BufferingStateCB buffering_state_cb_;
  PipelineMsgCB pipeline_message_cb_;
  std::string broadcast_id_;
  std::string last_channel_id_;
  EventCallback event_handler_;
  base::Lock lock_;
  blink::WebRect out_rect_;
  blink::WebRect source_rect_;
  bool full_screen_;
  LSMessageToken subscribe_key_;

  enum BroadcastType { BroadcastTV, BroadcastStandalone, BroadcastCable };
  BroadcastType broadcast_type_;

  void PlayPipeline();
  void PausePipeline();
  void ClosePipeline();
  void GetVideoInfo();
  void HandleGetVideoInfoReply(const std::string& payload);
  void ChangeAcbLoaded(const long& playState);
  void AcbLoadCompleted();
  void AcbSuspendCompleted();
  void GetLastChannelId();
  void HandleGetLastChannelIdReply(const std::string& payload);
  void ChangeChannel(const std::string& channel_id);
  void ChangeVolatileChannel(const std::string& channel_info);
  void HandleChangeChannelReply(const std::string& payload);
  void HandleOpenReply(const std::string& payload);
  void AcbHandler(long acb_id,
                  long task_id,
                  long event_type,
                  long app_state,
                  long play_state,
                  const char* reply);
  void DispatchAcbHandler(long acb_id,
                          long task_id,
                          long event_type,
                          long app_state,
                          long play_state,
                          const char* reply);
  DISALLOW_COPY_AND_ASSIGN(TvPipeline);
};

}  // namespace media

#endif  // MEDIA_BLINK_WEBOS_TV_PIPELINE_H_
