#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <uuids.h>

#include <assert.h>
#include <vector>
#include <string>
#include <cstdio>
#include <iostream>
#include <Windows.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "uuid.lib")

// enum video devices, audio devices
// capture video/audio & set used media type
// handle device lost

//---------------------------------------------------------------------------------------------
GUID videoSubType = MFVideoFormat_NV12;
UINT32 videoWidth = 1280, videoHeight = 720;
double videoFPS = 30.0;
LONG yStride = 0;

HRESULT CreateVideoSource(const WCHAR *name, const WCHAR *path);
HRESULT EnumDevices(bool video);
HRESULT EnumCapability(IMFMediaSource *pSource, bool video);
std::string GetVideoSubtypeString(const GUID &subtype);
std::string GetAudioSubtypeString(const GUID &subtype);
HRESULT CaptureVideo(IMFMediaSource *pSource);
HRESULT CaptureAudio(IMFMediaSource *pSource);

int main()
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	hr = MFStartup(MF_VERSION);

	// 测试发现，同一个camera，使用directshow的“DevicePath” 和 mf枚举到的设备id 是不一样的
	// 但是两个不同的id，调用CreateVideoSource 都可以成功创建mediasource
	// mf的官方demo，通过WM_DEVICECHANGE监控设备移除，其中的设备id字符串，和dshow的一致
	CreateVideoSource(L"Logitech BRIO", L"\\\\?\\usb#vid_046d&pid_085e&mi_00#9&1aa60e46&0&0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global");
	CreateVideoSource(L"Logitech BRIO", L"\\\\?\\usb#vid_046d&pid_085e&mi_00#9&1aa60e46&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global");

	EnumDevices(true);
	EnumDevices(false);

	MFShutdown();
	CoUninitialize();
	return 0;
}

HRESULT CreateVideoSource(const WCHAR *name, const WCHAR *path)
{
	IMFAttributes *sourceAttributes = nullptr;
	HRESULT hr = MFCreateAttributes(&sourceAttributes, 3);
	if (SUCCEEDED(hr)) {
		hr = sourceAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
		if (SUCCEEDED(hr)) {
			hr = sourceAttributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, reinterpret_cast<LPCWSTR>(path));
			if (SUCCEEDED(hr)) {
				hr = sourceAttributes->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, reinterpret_cast<LPCWSTR>(name));
				if (SUCCEEDED(hr)) {
					IMFMediaSource *source = NULL;
					hr = MFCreateDeviceSource(sourceAttributes, &source);
					assert(SUCCEEDED(hr));
					if (SUCCEEDED(hr) && source)
						source->Release();
				}
			}
		}
		sourceAttributes->Release();
	}

	return hr;
}

HRESULT EnumCapability(IMFMediaSource *pSource, bool video)
{
	if (!pSource)
		return E_POINTER;

	auto majorType = video ? MFMediaType_Video : MFMediaType_Audio;

	IMFPresentationDescriptor *pPD = nullptr;
	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
		return hr;

	DWORD streamCount = 0;
	hr = pPD->GetStreamDescriptorCount(&streamCount);
	if (FAILED(hr)) {
		pPD->Release();
		return hr;
	}

	for (DWORD i = 0; i < streamCount; ++i) {
		BOOL selected = FALSE;
		IMFStreamDescriptor *pSD = nullptr;
		hr = pPD->GetStreamDescriptorByIndex(i, &selected, &pSD);
		if (FAILED(hr))
			continue;

		IMFMediaTypeHandler *pHandler = nullptr;
		hr = pSD->GetMediaTypeHandler(&pHandler);
		if (FAILED(hr)) {
			pSD->Release();
			continue;
		}

		GUID majorType;
		hr = pHandler->GetMajorType(&majorType);
		if (FAILED(hr))
			continue;
		if (majorType != majorType)
			continue;

		DWORD typeCount = 0;
		hr = pHandler->GetMediaTypeCount(&typeCount);
		if (FAILED(hr)) {
			pHandler->Release();
			pSD->Release();
			continue;
		}

		for (DWORD j = 0; j < typeCount; ++j) {
			IMFMediaType *pType = nullptr;
			hr = pHandler->GetMediaTypeByIndex(j, &pType);
			if (FAILED(hr))
				continue;

			GUID majorType2{0};
			hr = pType->GetGUID(MF_MT_MAJOR_TYPE, &majorType2);
			if (FAILED(hr))
				continue;
			if (majorType2 != majorType)
				continue;

			if (video) {
				// format
				GUID subtype = {0};
				pType->GetGUID(MF_MT_SUBTYPE, &subtype);
				std::string guidStr = GetVideoSubtypeString(subtype);
				if (guidStr.empty())
					continue;

				// resolution
				UINT32 width = 0, height = 0;
				MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);

				// framerate
				UINT32 numerator = 0,
				       denominator = 0; // 视频设备的每个 IMFMediaType 支持的 framerate（帧率）通常是一个固定值MF_MT_FRAME_RATE，但有些设备/驱动会用“帧率范围”来描述其能力
				MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &numerator, &denominator);
				UINT32 num1 = 0, num2 = 0; //如果 MF_MT_FRAME_RATE_RANGE_MIN 和 MF_MT_FRAME_RATE_RANGE_MAX 存在，说明该格式支持一个帧率区间。
				// 如果是帧率范围 可以设置这个mediatype使用某个固定framerate：pType->SetItem(MF_MT_FRAME_RATE, var)
				// 参考 https://learn.microsoft.com/en-us/windows/win32/medfound/how-to-set-the-video-capture-frame-rate
				MFGetAttributeRatio(pType, MF_MT_FRAME_RATE_RANGE_MAX, &num1, &num2);
				MFGetAttributeRatio(pType, MF_MT_FRAME_RATE_RANGE_MIN, &num1, &num2);

				// log
				printf("Stream %lu/%lu, Type %lu >>> Resolution: %ux%u, Format: %s  FrameRate: %u/%u （%.2ffps） \n", i + 1, streamCount, j, width, height, guidStr.c_str(), numerator,
				       denominator, double(numerator) / double(denominator));
			} else {
				// 格式
				GUID subtype = {0};
				pType->GetGUID(MF_MT_SUBTYPE, &subtype);
				std::string guidStr = GetAudioSubtypeString(subtype);
				if (guidStr.empty())
					continue;

				// 采样率
				UINT32 sampleRate = 0;
				pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);

				// 通道数
				UINT32 channels = 0;
				pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);

				// 位深
				UINT32 bitsPerSample = 0;
				pType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);

				// 打印格式信息，if there are 2 channels, layput is always interleaved(not planar): LRLRLRLRLR
				printf("Stream %lu/%lu, Type %lu >>> SampleRate: %u, Channels: %u, BitsPerSample: %u, Subtype:%s \n", i + 1, streamCount, j, sampleRate, channels, bitsPerSample,
				       guidStr.c_str());
			}

			pType->Release();
		}
		pHandler->Release();
		pSD->Release();
	}
	pPD->Release();
	return S_OK;
}

HRESULT EnumDevices(bool video)
{
	IMFAttributes *pAttributes = nullptr;
	IMFActivate **ppDevices = nullptr;
	UINT32 count = 0;
	HRESULT hr = S_OK;

	hr = MFCreateAttributes(&pAttributes, 1); // 1: only use to set MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE
	if (FAILED(hr))
		return hr;

	hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
	if (FAILED(hr)) {
		pAttributes->Release();
		return hr;
	}

	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
	pAttributes->Release();
	if (FAILED(hr))
		return hr;

	for (UINT32 i = 0; i < count; ++i) {
		WCHAR *szFriendlyName = nullptr;
		WCHAR *szSymbolicLink = nullptr;
		WCHAR *szAudioEndpoint = nullptr;
		UINT32 chSymbolicLink = 0;

		hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szFriendlyName, &chSymbolicLink);
		if (SUCCEEDED(hr)) {
			if (video) {
				hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &szSymbolicLink, &chSymbolicLink);
			} else {
				// for dshow device path
				hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_SYMBOLIC_LINK, &szSymbolicLink, &chSymbolicLink);
				// for win-wasapi
				hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &szAudioEndpoint, &chSymbolicLink);
			}
		}

		if (SUCCEEDED(hr)) {
			if (video) {
				wprintf(L"Video Device %u: %ls\n    SymbolicLink: %ls\n\n", i, szFriendlyName, szSymbolicLink);
			} else {
				wprintf(L"Audio Device %u: %ls\n    SymbolicLink: %ls\n    endpoint: %ls  \n\n", i, szFriendlyName, szSymbolicLink, szAudioEndpoint);
			}
		}

		IMFActivate *pActivate = ppDevices[i];
		IMFMediaSource *pSource = NULL;
		hr = pActivate->ActivateObject(__uuidof(IMFMediaSource), (void **)&pSource);
		if (SUCCEEDED(hr)) {
			EnumCapability(pSource, video);

			bool logitech = szFriendlyName && wcsstr(szFriendlyName, L"Logitech");
			if (logitech) {
				if (video) {
					CaptureVideo(pSource); // 如果先调用了这个函数 则同一个IMFMediaSource 调用CreatePresentationDescriptor会失败
				} else {
					CaptureAudio(pSource);
				}
			}

			pSource->Release();
		}

		printf("\n\n");

		CoTaskMemFree(szFriendlyName);
		CoTaskMemFree(szSymbolicLink);
		CoTaskMemFree(szAudioEndpoint);
		ppDevices[i]->Release();
	}

	CoTaskMemFree(ppDevices);
	return S_OK;
}

std::string GetVideoSubtypeString(const GUID &subtype)
{
#if 0
	// https://learn.microsoft.com/en-us/windows/win32/medfound/video-subtype-guids
	struct {
		GUID guid;
		const char *str;
	} subtypeMap[] = {{MFVideoFormat_RGB32, "RGB32"},     {MFVideoFormat_ARGB32, "ARGB32"},   {MFVideoFormat_RGB24, "RGB24"}, {MFVideoFormat_RGB555, "RGB555"}, {MFVideoFormat_RGB565, "RGB565"},
			  {MFVideoFormat_I420, "I420"},       {MFVideoFormat_IYUV, "IYUV"}, // 等同于 I420
			  {MFVideoFormat_YV12, "YV12"},       {MFVideoFormat_NV12, "NV12"},       {MFVideoFormat_NV21, "NV21"},   {MFVideoFormat_YUY2, "YUY2"},     {MFVideoFormat_UYVY, "UYVY"},
			  {MFVideoFormat_AYUV, "AYUV"},       {MFVideoFormat_P010, "P010"},       {MFVideoFormat_P016, "P016"},   {MFVideoFormat_P210, "P210"},     {MFVideoFormat_P216, "P216"},
			  {MFVideoFormat_v210, "v210"},       {MFVideoFormat_v216, "v216"},       {MFVideoFormat_v410, "v410"},   {MFVideoFormat_Y210, "Y210"},     {MFVideoFormat_Y216, "Y216"},
			  {MFVideoFormat_Y410, "Y410"},       {MFVideoFormat_Y416, "Y416"},       {MFVideoFormat_MJPG, "MJPG"},   {MFVideoFormat_H264, "H264"},     {MFVideoFormat_HEVC, "HEVC"},
			  {MFVideoFormat_HEVC_ES, "HEVC_ES"}, {MFVideoFormat_MPEG2, "MPEG2"},     {MFVideoFormat_H263, "H263"},   {MFVideoFormat_MP43, "MP43"},     {MFVideoFormat_MP4S, "MP4S"},
			  {MFVideoFormat_M4S2, "M4S2"},       {MFVideoFormat_MP4V, "MP4V"},       {MFVideoFormat_WMV1, "WMV1"},   {MFVideoFormat_WMV2, "WMV2"},     {MFVideoFormat_WMV3, "WMV3"},
			  {MFVideoFormat_WVC1, "WVC1"},       {MFVideoFormat_MSS1, "MSS1"},       {MFVideoFormat_MSS2, "MSS2"},   {MFVideoFormat_MPG1, "MPG1"},     {MFVideoFormat_DVSL, "DVSL"},
			  {MFVideoFormat_DVSD, "DVSD"},       {MFVideoFormat_DVHD, "DVHD"},       {MFVideoFormat_DV25, "DV25"},   {MFVideoFormat_DV50, "DV50"},     {MFVideoFormat_DVH1, "DVH1"},
			  {MFVideoFormat_DVC, "DVC"},         {MFVideoFormat_H264_ES, "H264_ES"}, {MFVideoFormat_VP80, "VP80"},   {MFVideoFormat_VP90, "VP90"},     {MFVideoFormat_AV1, "AV1"},
			  {MFVideoFormat_L8, "L8"},           {MFVideoFormat_L16, "L16"},         {MFVideoFormat_D16, "D16"}};
#else
	struct {
		GUID guid;
		const char *str;
	} subtypeMap[] = {
		{MFVideoFormat_RGB32, "RGB32"}, {MFVideoFormat_ARGB32, "ARGB32"}, {MFVideoFormat_RGB24, "RGB24"}, {MFVideoFormat_I420, "I420"}, {MFVideoFormat_IYUV, "IYUV"}, // 等同于 I420
		{MFVideoFormat_YV12, "YV12"},   {MFVideoFormat_NV12, "NV12"},     {MFVideoFormat_NV21, "NV21"},   {MFVideoFormat_YUY2, "YUY2"}, {MFVideoFormat_UYVY, "UYVY"},
		{MFVideoFormat_AYUV, "AYUV"},   {MFVideoFormat_P010, "P010"},     {MFVideoFormat_P016, "P016"},   {MFVideoFormat_P210, "P210"}, {MFVideoFormat_P216, "P216"},
		{MFVideoFormat_v210, "v210"},   {MFVideoFormat_v216, "v216"},     {MFVideoFormat_v410, "v410"},   {MFVideoFormat_Y210, "Y210"}, {MFVideoFormat_Y216, "Y216"},
		{MFVideoFormat_Y410, "Y410"},   {MFVideoFormat_Y416, "Y416"},     {MFVideoFormat_MJPG, "MJPG"},   {MFVideoFormat_H264, "H264"}, {MFVideoFormat_HEVC, "HEVC"},
	};
#endif

	for (const auto &entry : subtypeMap) {
		if (IsEqualGUID(subtype, entry.guid)) {
			return entry.str;
		}
	}

	return "";
}

std::string GetAudioSubtypeString(const GUID &subtype)
{
	struct {
		GUID guid;
		const char *str;
	} subtypeMap[] = {
		{MFAudioFormat_PCM, "PCM"},     // LRLRLR
		{MFAudioFormat_Float, "Float"}, // LRLRLR
		{MFAudioFormat_AAC, "AAC"},
		{MFAudioFormat_DTS, "DTS"},
		{MFAudioFormat_Dolby_AC3, "Dolby_AC3"},
		{MFAudioFormat_ADTS, "ADTS"},
		{MFAudioFormat_MP3, "MP3"},
		{MFAudioFormat_MPEG, "MPEG"},
		{MFAudioFormat_WMAudioV8, "WMAudioV8"},
		{MFAudioFormat_WMAudioV9, "WMAudioV9"},
		{MFAudioFormat_WMAudio_Lossless, "WMAudio_Lossless"},
		{MFAudioFormat_WMASPDIF, "WMASPDIF"},
		{MFAudioFormat_MSP1, "MSP1"},
		{MFAudioFormat_FLAC, "FLAC"},
		{MFAudioFormat_ALAC, "ALAC"},
		{MFAudioFormat_AMR_NB, "AMR_NB"},
		{MFAudioFormat_AMR_WB, "AMR_WB"},
		{MFAudioFormat_DRM, "DRM"},
		{MFAudioFormat_ADTS, "ADTS"},
	};

	for (const auto &entry : subtypeMap) {
		if (IsEqualGUID(subtype, entry.guid)) {
			return entry.str;
		}
	}

	return "";
}

//---------------------------------------------------- capture video --------------------------------------------------------
HRESULT CreateSourceReader(IMFMediaSource *pSource, IMFSourceReader **ppReader)
{
	*ppReader = nullptr;

	IMFAttributes *pAttr = nullptr;
	HRESULT hr = MFCreateAttributes(&pAttr, 2); // 2: set two attributes
	if (FAILED(hr))
		return hr;

	// 视频处理（颜色空间转换、解码等）
	// 如果要异步回调数据，需要设置flag：MF_SOURCE_READER_ASYNC_CALLBACK 参考《MFCaptureD3D》
	pAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
	pAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

	hr = MFCreateSourceReaderFromMediaSource(pSource, pAttr, ppReader);

	pAttr->Release();
	return hr;
}

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
{
	LONG lStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&lStride);
	if (FAILED(hr)) {
		// Attribute not set. Try to calculate the default stride.
		GUID subtype = {};

		UINT32 width = 0;
		UINT32 height = 0;

		// Get the subtype and the image size.
		hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (SUCCEEDED(hr)) {
			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
		}
		if (SUCCEEDED(hr)) {
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
		}

		// Set the attribute for later reference.
		if (SUCCEEDED(hr)) {
			(void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
		}
	}

	if (SUCCEEDED(hr)) {
		*plStride = lStride;
	}
	return hr;
}

HRESULT SetReaderMediaType(IMFMediaSource *pSource, IMFSourceReader *pReader)
{
	bool found = false;
	for (DWORD index = 0; found == false; ++index) {
		IMFMediaType *pNativeType = nullptr;
		HRESULT hr = pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &pNativeType);
		if (FAILED(hr))
			break;

		GUID subtype = {0};
		if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype)) && IsEqualGUID(subtype, videoSubType)) {

			UINT32 width = 0, height = 0;
			if (SUCCEEDED(MFGetAttributeSize(pNativeType, MF_MT_FRAME_SIZE, &width, &height)) && width == videoWidth && height == videoHeight) {

				UINT32 frNum = 0, frDen = 0;
				if (SUCCEEDED(MFGetAttributeRatio(pNativeType, MF_MT_FRAME_RATE, &frNum, &frDen))) {
					if (frDen != 0 && std::abs(double(frNum) / double(frDen) - videoFPS) < 0.01) {
						hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pNativeType);
						assert(SUCCEEDED(hr));
						if (SUCCEEDED(hr)) {
							found = true;
							printf("Selected 1280x720 NV12 30fps media type.\n");
							if (SUCCEEDED(GetDefaultStride(pNativeType, &yStride))) {
								printf("Default y stride: %ld\n", yStride);
							}
						}
					}
				}
			}
		}

		pNativeType->Release();
	}

	if (!found) {
		printf("No matching 1280x720 NV12 30fps media type found.\n");
		return E_FAIL;
	}

	return S_OK;
}

HRESULT ReadVideoFrame(IMFSourceReader *pReader, bool &got)
{
	DWORD streamIndex = 0;
	DWORD flags = 0;
	LONGLONG llTimeStamp = 0;
	IMFSample *pSample = nullptr;

	// 如果不是异步读取，此处会阻塞一会儿
	HRESULT hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
	if (FAILED(hr))
		return hr;

	if (flags & MF_SOURCE_READERF_STREAMTICK)
		return S_OK;

	if (!pSample)
		return S_OK;

	IMFMediaBuffer *pBuffer = nullptr;
	hr = pSample->ConvertToContiguousBuffer(&pBuffer);
	if (SUCCEEDED(hr)) {
		BYTE *pData = nullptr;
		DWORD cbMaxLength = 0, cbCurrentLength = 0;
		hr = pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);
		if (SUCCEEDED(hr)) {
			FILE *fp = NULL;
			fopen_s(&fp, "input.nv12", "wb+");
			if (fp) {
				assert(yStride == videoWidth);
				fwrite(pData, 1, cbCurrentLength, fp);
				fclose(fp);
			}

			pBuffer->Unlock();
		}
		pBuffer->Release();
	}

	pSample->Release();
	got = true;
	return S_OK;
}

HRESULT CaptureVideo(IMFMediaSource *pSource)
{
	IMFSourceReader *pReader = nullptr;
	if (FAILED(CreateSourceReader(pSource, &pReader)))
		return E_FAIL;

	if (FAILED(SetReaderMediaType(pSource, pReader))) {
		pReader->Release();
		return E_FAIL;
	}

	int cnt = 0;
	while (cnt < 5) {
		bool got = false;
		if (FAILED(ReadVideoFrame(pReader, got)))
			break;

		if (got)
			++cnt;
	}

	pReader->Release();
	return S_OK;
}

//---------------------------------------------------- capture audio --------------------------------------------------------
HRESULT CreateAudioSourceReader(IMFMediaSource *pSource, IMFSourceReader **ppReader)
{
	*ppReader = nullptr;
	IMFAttributes *pAttr = nullptr;
	HRESULT hr = MFCreateAttributes(&pAttr, 1);
	if (FAILED(hr))
		return hr;

	// 可选：启用硬件加速
	pAttr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

	hr = MFCreateSourceReaderFromMediaSource(pSource, pAttr, ppReader);
	pAttr->Release();
	return hr;
}

HRESULT SetAudioReaderMediaType(IMFSourceReader *pReader)
{
	bool found = false;
	for (DWORD index = 0; found == false; ++index) {
		IMFMediaType *pNativeType = nullptr;
		HRESULT hr = pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, index, &pNativeType);
		if (FAILED(hr))
			break;

		GUID subtype = {0};
		if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype)) && (IsEqualGUID(subtype, MFAudioFormat_PCM) || IsEqualGUID(subtype, MFAudioFormat_Float))) {

			hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pNativeType);
			assert(SUCCEEDED(hr));
			if (SUCCEEDED(hr)) {
				found = true;

				UINT32 channels = 0, sampleRate = 0, bitsPerSample = 0;
				pNativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
				pNativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
				pNativeType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
				printf("Selected PCM audio. chn=%u samplerate=%u bitsPerSample=%u \n", channels, sampleRate, bitsPerSample);
			}
		}
		pNativeType->Release();
	}

	// 如果没找到理想格式，再找任意一个PCM
	if (!found) {
		for (DWORD index = 0; found == false; ++index) {
			IMFMediaType *pNativeType = nullptr;
			HRESULT hr = pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, index, &pNativeType);
			if (FAILED(hr))
				break;

			GUID subtype = {0};
			if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype)) && IsEqualGUID(subtype, MFAudioFormat_PCM)) {
				hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pNativeType);
				assert(SUCCEEDED(hr));
				if (SUCCEEDED(hr)) {
					found = true;
					UINT32 channels = 0, sampleRate = 0, bitsPerSample = 0;
					pNativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
					pNativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
					pNativeType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
					printf("Selected PCM %uch %uHz %ubit audio media type.\n", channels, sampleRate, bitsPerSample);
				}
			}
			pNativeType->Release();
		}
	}

	if (!found) {
		printf("No matching PCM audio media type found.\n");
		return E_FAIL;
	}
	return S_OK;
}

HRESULT ReadAudioFrame(IMFSourceReader *pReader, bool &got)
{
	DWORD streamIndex = 0;
	DWORD flags = 0;
	LONGLONG llTimeStamp = 0;
	IMFSample *pSample = nullptr;

	HRESULT hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIndex, &flags, &llTimeStamp, &pSample);
	if (FAILED(hr))
		return hr;

	if (flags & MF_SOURCE_READERF_STREAMTICK)
		return S_OK;

	if (!pSample)
		return S_OK;

	IMFMediaBuffer *pBuffer = nullptr;
	hr = pSample->ConvertToContiguousBuffer(&pBuffer);
	if (SUCCEEDED(hr)) {
		BYTE *pData = nullptr;
		DWORD cbMaxLength = 0, cbCurrentLength = 0;
		hr = pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength);
		if (SUCCEEDED(hr)) {
			static FILE *fp = NULL;
			if (!fp)
				fopen_s(&fp, "input.pcm", "wb+");
			if (fp) {
				fwrite(pData, 1, cbCurrentLength, fp);
				fflush(fp);
			}

			pBuffer->Unlock();
		}
		pBuffer->Release();
	}

	pSample->Release();
	got = true;
	return S_OK;
}

HRESULT CaptureAudio(IMFMediaSource *pSource)
{
	IMFSourceReader *pReader = nullptr;
	if (FAILED(CreateAudioSourceReader(pSource, &pReader)))
		return E_FAIL;

	if (FAILED(SetAudioReaderMediaType(pReader))) {
		pReader->Release();
		return E_FAIL;
	}

	int cnt = 0;
	while (cnt < 200) {
		bool got = false;
		if (FAILED(ReadAudioFrame(pReader, got)))
			break;
		if (got)
			++cnt;
	}

	pReader->Release();
	return S_OK;
}