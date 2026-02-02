#include "mf-enum.h"

std::vector<MFDevice> EnumDevices(bool video)
{
	std::vector<MFDevice> devices;

	ComPtr<IMFAttributes> pAttributes = nullptr;
	HRESULT hr = MFCreateAttributes(&pAttributes, 1); // 1: only use to set MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE
	if (FAILED(hr))
		return devices;

	hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
	if (FAILED(hr))
		return devices;

	UINT32 count = 0;
	IMFActivate **ppDevices = nullptr;
	hr = MFEnumDeviceSources(pAttributes.Get(), &ppDevices, &count);
	if (FAILED(hr))
		return devices;

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
				ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_ENDPOINT_ID, &szAudioEndpoint, &chSymbolicLink);
			}

			if (SUCCEEDED(hr)) {
				MFDevice dev;
				dev.name = szFriendlyName ? szFriendlyName : L"";
				dev.path = szSymbolicLink ? szSymbolicLink : L"";
				devices.push_back(dev);

				if (video) {
					wprintf(L"Video Device [%u/%u]\n%s \nSymbolicLink: %s\n\n", i + 1, count, szFriendlyName, szSymbolicLink);
				} else {
					wprintf(L"Audio Device [%u/%u]\n%s \nSymbolicLink: %s\nendpoint: %ls  \n\n", i + 1, count, szFriendlyName, szSymbolicLink,
						szAudioEndpoint ? szAudioEndpoint : L"");
				}
			}
		}

		ComPtr<IMFMediaSource> pSource = NULL;
		hr = ppDevices[i]->ActivateObject(__uuidof(IMFMediaSource), (void **)&pSource);
		if (SUCCEEDED(hr)) {
			EnumCapability(pSource, video);
		}

		printf("\n\n");

		if (szFriendlyName)
			CoTaskMemFree(szFriendlyName);
		if (szSymbolicLink)
			CoTaskMemFree(szSymbolicLink);
		if (szAudioEndpoint)
			CoTaskMemFree(szAudioEndpoint);

		ppDevices[i]->Release();
	}

	CoTaskMemFree(ppDevices);
	return devices;
}

HRESULT EnumCapability(ComPtr<IMFMediaSource> pSource, bool video)
{
	if (!pSource)
		return E_POINTER;

	auto destMajorType = video ? MFMediaType_Video : MFMediaType_Audio;

	ComPtr<IMFPresentationDescriptor> pPD = nullptr;
	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
		return hr;

	DWORD streamCount = 0;
	hr = pPD->GetStreamDescriptorCount(&streamCount);
	if (FAILED(hr))
		return hr;

	for (DWORD i = 0; i < streamCount; ++i) {
		BOOL selected = FALSE;
		ComPtr<IMFStreamDescriptor> pSD = nullptr;
		hr = pPD->GetStreamDescriptorByIndex(i, &selected, &pSD);
		if (FAILED(hr))
			continue;

		ComPtr<IMFMediaTypeHandler> pHandler = nullptr;
		hr = pSD->GetMediaTypeHandler(&pHandler);
		if (FAILED(hr))
			continue;

		GUID majorType{0};
		hr = pHandler->GetMajorType(&majorType);
		if (FAILED(hr) || majorType != destMajorType)
			continue;

		DWORD typeCount = 0;
		hr = pHandler->GetMediaTypeCount(&typeCount);
		if (FAILED(hr))
			continue;

		for (DWORD j = 0; j < typeCount; ++j) {
			ComPtr<IMFMediaType> pType = nullptr;
			hr = pHandler->GetMediaTypeByIndex(j, &pType);
			if (FAILED(hr))
				continue;

			GUID majorType2{0};
			hr = pType->GetGUID(MF_MT_MAJOR_TYPE, &majorType2);
			if (FAILED(hr) || majorType2 != destMajorType)
				continue;

			if (video) {
				// format
				GUID subtype = {0};
				hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				std::string guidStr = GetVideoSubtypeString(subtype);
				if (guidStr.empty())
					continue;

				// resolution
				UINT32 width = 0, height = 0;
				hr = MFGetAttributeSize(pType.Get(), MF_MT_FRAME_SIZE, &width, &height);
				if (FAILED(hr) || !width || !height) {
					assert(false);
					continue;
				}

				// framerate
				UINT32 numerator, denominator = 0;
				hr = MFGetAttributeRatio(pType.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				// 视频设备的每个 IMFMediaType 支持的 framerate（帧率）通常是一个固定值 MF_MT_FRAME_RATE，但有些设备/驱动会用“帧率范围”来描述其能力:MF_MT_FRAME_RATE_RANGE_MIN, MF_MT_FRAME_RATE_RANGE_MAX
				// 如果 MF_MT_FRAME_RATE_RANGE_MIN 和 MF_MT_FRAME_RATE_RANGE_MAX 存在，说明该格式支持一个帧率区间。
				// 如果是帧率范围 可以设置这个mediatype使用某个固定framerate：pType->SetItem(MF_MT_FRAME_RATE, var), 参考 https://learn.microsoft.com/en-us/windows/win32/medfound/how-to-set-the-video-capture-frame-rate
				UINT32 rangeMaxNum = 0, rangeMaxDen = 0, rangeMinNum = 0, rangeMinDen = 0;
				MFGetAttributeRatio(pType.Get(), MF_MT_FRAME_RATE_RANGE_MAX, &rangeMaxNum, &rangeMaxDen);
				MFGetAttributeRatio(pType.Get(), MF_MT_FRAME_RATE_RANGE_MIN, &rangeMinNum, &rangeMinDen);

				// log
				printf("\tcapability stream[%lu/%lu] mediaType[%lu/%lu] >> %lux%lu, Format=%s  fps:%lu/%lu （%.2ffps） \n", i + 1, streamCount, j + 1, typeCount, width, height,
				       guidStr.c_str(), numerator, denominator, double(numerator) / double(denominator));
			} else {
				// 格式
				GUID subtype = {0};
				hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				std::string guidStr = GetAudioSubtypeString(subtype);
				if (guidStr.empty())
					continue;

				// 采样率
				UINT32 sampleRate = 0;
				hr = pType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				// 通道数
				UINT32 channels = 0;
				hr = pType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				// 位深
				UINT32 bitsPerSample = 0;
				hr = pType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
				if (FAILED(hr)) {
					assert(false);
					continue;
				}

				// if there are 2 channels, layput is always interleaved(not planar): LRLRLRLRLR
				printf("\tcapability stream[%lu/%lu] mediaType[%lu/%lu] >> chn=%lu, Format=%s %luHZ %lubit\n", i + 1, streamCount, j + 1, typeCount, channels, guidStr.c_str(),
				       sampleRate, bitsPerSample);
			}
		}
	}

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
		{MFAudioFormat_MP3, "MP3"},
#if 0
		{MFAudioFormat_DTS, "DTS"},
		{MFAudioFormat_Dolby_AC3, "Dolby_AC3"},
		{MFAudioFormat_ADTS, "ADTS"},
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
#endif
	};

	for (const auto &entry : subtypeMap) {
		if (IsEqualGUID(subtype, entry.guid)) {
			return entry.str;
		}
	}

	return "";
}
