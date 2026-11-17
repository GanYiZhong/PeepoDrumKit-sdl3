#include "audio_backend.h"
#include "audio_common.h"

// NOTE: SDL3 audio stream backend. Currently only compiled for the web (Emscripten) build where neither WASAPI, CoreAudio nor libsoundio exist.
//		 In a browser SDL3 outputs through the Web Audio API and (without pthreads) the callback runs on the main thread
#ifdef __EMSCRIPTEN__

#include <SDL3/SDL.h>
#include <stdio.h>
#include <atomic>
#include <vector>

namespace Audio
{
	struct SDL3Backend::Impl
	{
		static void SDLCALL StreamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount)
		{
			(void)totalAmount;
			auto* impl = static_cast<SDL3Backend::Impl*>(userdata);
			if (impl == nullptr || additionalAmount <= 0)
				return;

			const u32 channelCount = impl->streamParam.ChannelCount;
			const u32 frameCount = static_cast<u32>(additionalAmount) / (sizeof(i16) * channelCount);
			if (frameCount == 0)
				return;

			impl->renderBuffer.resize(static_cast<size_t>(frameCount) * channelCount);
			if (impl->renderCallback && impl->isOpenRunning.load())
				impl->renderCallback(impl->renderBuffer.data(), frameCount, channelCount);
			else
				std::fill(impl->renderBuffer.begin(), impl->renderBuffer.end(), i16(0));

			SDL_PutAudioStreamData(stream, impl->renderBuffer.data(), static_cast<int>(impl->renderBuffer.size() * sizeof(i16)));
		}

		b8 OpenStartStream(const BackendStreamParam& param, BackendRenderCallback callback)
		{
			if (isOpenRunning.load())
				return false;

			if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
			{
				printf("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n", SDL_GetError());
				return false;
			}

			streamParam = param;
			renderCallback = std::move(callback);

			{
				// NOTE: Web Audio only accepts power of two buffer sizes between 256 and 16384. The callback runs on the (busy) main thread,
				//		 so the tiny buffer sizes used for the native low latency backends would only result in constant underruns
				static constexpr u32 minFrameCount = 1024, maxFrameCount = 16384;
				u32 frameCount = minFrameCount;
				while (frameCount < param.DesiredFrameCount && frameCount < maxFrameCount)
					frameCount *= 2;

				char frameCountString[32];
				snprintf(frameCountString, sizeof(frameCountString), "%u", frameCount);
				SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frameCountString);
			}

			SDL_AudioSpec spec = {};
			spec.format = SDL_AUDIO_S16;
			spec.channels = static_cast<int>(param.ChannelCount);
			spec.freq = static_cast<int>(param.SampleRate);

			stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &StreamCallback, this);
			if (stream == nullptr)
			{
				printf("SDL_OpenAudioDeviceStream() failed: %s\n", SDL_GetError());
				renderCallback = nullptr;
				return false;
			}

			isOpenRunning.store(true);
			if (!SDL_ResumeAudioStreamDevice(stream))
				printf("SDL_ResumeAudioStreamDevice() failed: %s\n", SDL_GetError());

			return true;
		}

		b8 StopCloseStream()
		{
			if (!isOpenRunning.load())
				return false;

			isOpenRunning.store(false);
			if (stream != nullptr)
				SDL_DestroyAudioStream(stream);
			stream = nullptr;
			renderCallback = nullptr;
			return true;
		}

		BackendStreamParam streamParam = {};
		BackendRenderCallback renderCallback;
		SDL_AudioStream* stream = nullptr;
		std::atomic<b8> isOpenRunning = false;
		std::vector<i16> renderBuffer;
	};

	SDL3Backend::SDL3Backend() : impl(std::make_unique<Impl>()) {}
	SDL3Backend::~SDL3Backend() { impl->StopCloseStream(); }
	b8 SDL3Backend::OpenStartStream(const BackendStreamParam& param, BackendRenderCallback callback) { return impl->OpenStartStream(param, std::move(callback)); }
	b8 SDL3Backend::StopCloseStream() { return impl->StopCloseStream(); }
	b8 SDL3Backend::IsOpenRunning() const { return impl->isOpenRunning.load(); }
	u32 SDL3Backend::GetVariantCount() const { return 1; }
	cstr SDL3Backend::GetVariantName(u32 index) const { return (index == 0) ? "SDL3 (Web Audio)" : "Invalid"; }
}

#endif // __EMSCRIPTEN__
