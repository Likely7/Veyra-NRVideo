#pragma once
#include <windows.h>
#include <dshow.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <functional>
namespace veyra::source {
enum class CaptureAllocatorMode { SinkDefault, Upstream };
// Exact raw connection contract, including subtype and VideoInfo2 field flags.
bool nativeCaptureContractsMatch(const AM_MEDIA_TYPE&,const AM_MEDIA_TYPE&);
// A timestamp-less priming sample is not a fatal driver error. Acknowledge it
// without creating a frame/PTS; timed samples retain the exact driver timestamp.
std::function<HRESULT(IMediaSample*)> timedCaptureDelivery(std::function<HRESULT(double,IMediaSample*)>);
// Native terminal filter: no legacy SampleGrabber orientation/VideoInfo2
// restriction, conversion filter or extra queue. Callback borrows the sample.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Blackmagic compatibility: let the output pin choose its own allocator and
// requirements. The selected allocator is still retained after NotifyAllocator.
HRESULT createNativeCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&,CaptureAllocatorMode);
HRESULT createNativeAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
HRESULT createBitstreamAudioSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Compressed (MJPEG/H.264/HEVC/...) variant: carries the driver payload without
// a pixel-layout contract; decoding happens in our own backend.
HRESULT createCompressedCaptureSink(const AM_MEDIA_TYPE&,std::function<HRESULT(IMediaSample*)>,Microsoft::WRL::ComPtr<IBaseFilter>&,Microsoft::WRL::ComPtr<IPin>&);
// Advisory request on the upstream output pin, before ConnectDirect. Failure
// does not invalidate the device; callers retain the compatible connection.
HRESULT suggestCaptureAudioBuffering(IPin*,const WAVEFORMATEX&);
// Advisory video-pin request (N4): suggest the driver's allocator buffer count
// and per-frame byte size before ConnectDirect. Failure is logged and ignored.
HRESULT suggestCaptureVideoBuffering(IPin*,long buffers,long bytes);
// Reads the allocator actually in use on our input pin after ConnectDirect.
HRESULT queryCaptureAllocatorProperties(IPin*,ALLOCATOR_PROPERTIES&);
}
