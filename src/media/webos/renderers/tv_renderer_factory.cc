// Copyright (c) 2015-2017 LG Electronics, Inc.

#include "media/webos/renderers/tv_renderer_factory.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "media/base/decoder_factory.h"
#include "media/base/media_log.h"
#include "media/filters/gpu_video_decoder.h"
#include "media/filters/opus_audio_decoder.h"
#include "media/renderers/audio_renderer_impl.h"
#include "media/renderers/gpu_video_accelerator_factories.h"
#include "media/renderers/renderer_impl.h"
#include "media/renderers/video_renderer_impl.h"
#include "media/webos/base/media_apis_wrapper.h"
#include "media/webos/filters/tv_audio_decoder.h"
#include "media/webos/filters/tv_video_decoder.h"
#include "media/webos/renderers/tv_video_renderer.h"


#if !defined(MEDIA_DISABLE_FFMPEG)
#include "media/filters/ffmpeg_audio_decoder.h"
#if !defined(DISABLE_FFMPEG_VIDEO_DECODERS)
#include "media/filters/ffmpeg_video_decoder.h"
#endif
#endif

#if !defined(MEDIA_DISABLE_LIBVPX)
#include "media/filters/vpx_video_decoder.h"
#endif

namespace media {

TvRendererFactory::TvRendererFactory(
    const scoped_refptr<MediaLog>& media_log,
    DecoderFactory* decoder_factory,
    const GetGpuFactoriesCB& get_gpu_factories_cb)
    : media_log_(media_log),
      decoder_factory_(decoder_factory),
      get_gpu_factories_cb_(get_gpu_factories_cb) {}

TvRendererFactory::~TvRendererFactory() {
}

ScopedVector<AudioDecoder> TvRendererFactory::CreateAudioDecoders(
    const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner) {
  // Create our audio decoders and renderer.
  ScopedVector<AudioDecoder> audio_decoders;

  audio_decoders.push_back(new TvAudioDecoder(media_task_runner, media_apis_wrapper_));

#if !defined(MEDIA_DISABLE_FFMPEG)
  audio_decoders.push_back(
      new FFmpegAudioDecoder(media_task_runner, media_log_));
#endif

  audio_decoders.push_back(new OpusAudioDecoder(media_task_runner));

  // Use an external decoder only if we cannot otherwise decode in the
  // renderer.
  if (decoder_factory_)
    decoder_factory_->CreateAudioDecoders(media_task_runner, &audio_decoders);

  return audio_decoders;
}

ScopedVector<VideoDecoder> TvRendererFactory::CreateVideoDecoders(
    const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
    const RequestSurfaceCB& request_surface_cb,
    GpuVideoAcceleratorFactories* gpu_factories) {
  // Create our video decoders and renderer.
  ScopedVector<VideoDecoder> video_decoders;

  // Prefer an external decoder since one will only exist if it is hardware
  // accelerated.
  if (gpu_factories) {
    // |gpu_factories_| requires that its entry points be called on its
    // |GetTaskRunner()|.  Since |pipeline_| will own decoders created from the
    // factories, require that their message loops are identical.
    DCHECK(gpu_factories->GetTaskRunner() == media_task_runner.get());

    if (decoder_factory_) {
      decoder_factory_->CreateVideoDecoders(media_task_runner, gpu_factories,
                                            &video_decoders);
    }
    video_decoders.push_back(
        new GpuVideoDecoder(gpu_factories, request_surface_cb, media_log_));
  }

  video_decoders.push_back(new TvVideoDecoder(media_task_runner,
                                              media_apis_wrapper_));

#if !defined(MEDIA_DISABLE_LIBVPX)
  video_decoders.push_back(new VpxVideoDecoder());
#endif

#if !defined(MEDIA_DISABLE_FFMPEG) && !defined(DISABLE_FFMPEG_VIDEO_DECODERS)
  video_decoders.push_back(new FFmpegVideoDecoder());
#endif

  return video_decoders;
}

std::unique_ptr<Renderer> TvRendererFactory::CreateRenderer(
    const scoped_refptr<base::SingleThreadTaskRunner>& media_task_runner,
    const scoped_refptr<base::TaskRunner>& worker_task_runner,
    AudioRendererSink* audio_renderer_sink,
    VideoRendererSink* video_renderer_sink,
    const RequestSurfaceCB& request_surface_cb) {
  DCHECK(audio_renderer_sink);
  DCHECK(media_apis_wrapper_) << "WebOS Media APIs Wrapper uninitialized";

  std::unique_ptr<AudioRenderer> audio_renderer(new AudioRendererImpl(
      media_task_runner, audio_renderer_sink,
      CreateAudioDecoders(media_task_runner), media_log_));

  GpuVideoAcceleratorFactories* gpu_factories = nullptr;
  if (!get_gpu_factories_cb_.is_null())
    gpu_factories = get_gpu_factories_cb_.Run();

  std::unique_ptr<VideoRenderer> video_renderer(new TvVideoRenderer(
      media_task_runner, media_apis_wrapper_, worker_task_runner, video_renderer_sink,
      CreateVideoDecoders(media_task_runner, request_surface_cb, gpu_factories),
      true, gpu_factories, media_log_));

  // Create renderer.
  std::unique_ptr<RendererImpl> renderer(new RendererImpl(
      media_task_runner, std::move(audio_renderer), std::move(video_renderer)));

  renderer->SetMediaAPIsWrapper(media_apis_wrapper_);

  return std::move(renderer);
}

void TvRendererFactory::SetMediaAPIsWrapper(
    const scoped_refptr<MediaAPIsWrapper>& media_apis_wrapper) {
  media_apis_wrapper_ = media_apis_wrapper;
}

}  // namespace media
