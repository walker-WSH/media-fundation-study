#include "mf-capture.h"
#include "mf-util.hpp"
#include <cmath>
#include <cstdio>
#include <shlwapi.h> // for using QITAB

#pragma comment(lib, "shlwapi.lib")

ComPtr<CMFCapture> CMFCapture::CreateInstance(bool video, const WCHAR *name, const WCHAR *path)
{
	auto source = CreateMediaSource(video, name, path);
	if (!source)
		return nullptr;

	CMFCapture *ins = new (std::nothrow) CMFCapture(source, video);
	auto obj = ComPtr<CMFCapture>(ins);
	obj->Release();
	return obj;
}

CMFCapture::CMFCapture(ComPtr<IMFMediaSource> source, bool video)
	: m_pSource(source), m_bIsVideo(video), m_dwReaderStream(video ? (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM : (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM)
{
}

CMFCapture::~CMFCapture()
{
	StopCapture();
}

bool CMFCapture::StartCapture()
{
	CAutoLockCS lock(m_lock);

	if (m_pReader || !m_pSource) {
		assert(false);
		return false;
	}

	// Create an attribute store to hold initialization settings.
	ComPtr<IMFAttributes> pAttributes = nullptr;
	HRESULT hr = MFCreateAttributes(&pAttributes, 2);
	if (FAILED(hr)) {
		assert(false);
		return false;
	}

	hr = pAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
	if (FAILED(hr)) {
		assert(false);
		return false;
	}

	// Set the callback pointer.
	hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
	if (FAILED(hr)) {
		assert(false);
		return false;
	}

	hr = MFCreateSourceReaderFromMediaSource(m_pSource.Get(), pAttributes.Get(), &m_pReader);
	if (FAILED(hr)) {
		assert(false);
		return false;
	}

	if (!SelectMediaType()) {
		assert(false);
		return false;
	}

	// Ask for the first sample.
	hr = m_pReader->ReadSample(m_dwReaderStream, 0, NULL, NULL, NULL, NULL);
	if (FAILED(hr)) {
		// NOTE: The source reader shuts down the media source
		// by default, but we might not have gotten that far.
		m_pSource->Shutdown();

		StopCapture();
		return false;
	}

	return true;
}

void CMFCapture::StopCapture()
{
	CAutoLockCS lock(m_lock);

	m_pSource = nullptr;
	m_pReader = nullptr;
}

ULONG CMFCapture::AddRef()
{
	return InterlockedIncrement(&m_nRefCount);
}

ULONG CMFCapture::Release()
{
	ULONG uCount = InterlockedDecrement(&m_nRefCount);
	if (uCount == 0) {
		delete this;
	}
	// For thread safety, return a temporary variable.
	return uCount;
}

HRESULT CMFCapture::QueryInterface(REFIID riid, void **ppv)
{
	static const QITAB qit[] = {
		QITABENT(CMFCapture, IMFSourceReaderCallback),
		{0},
	};
	return QISearch(this, qit, riid, ppv);
}

// Called when the IMFMediaSource::ReadSample method completes.
HRESULT CMFCapture::OnReadSample(HRESULT hrStatus, DWORD /* dwStreamIndex */, DWORD /* dwStreamFlags */, LONGLONG /* llTimestamp */, IMFSample *pSample /*Can be NULL*/)
{
	CAutoLockCS lock(m_lock);

	HRESULT hr = S_OK;
	if (FAILED(hrStatus)) {
		hr = hrStatus;
	}

	if (SUCCEEDED(hr)) {
		if (pSample) { // it may be NULL
			// Get the video frame buffer from the sample.
			ComPtr<IMFMediaBuffer> pBuffer = NULL;
			hr = pSample->GetBufferByIndex(0, &pBuffer);

			// read the frame.
			if (SUCCEEDED(hr)) {
				OnData(pBuffer);
			}
		}
	}

	// Request the next frame.
	if (SUCCEEDED(hr)) {
		hr = m_pReader->ReadSample(m_dwReaderStream, 0,
					   NULL, // actual
					   NULL, // flags
					   NULL, // timestamp
					   NULL  // sample
		);
	}

	if (FAILED(hr)) {
		NotifyException(hr);
	}

	return hr;
}

bool CMFCapture::SelectMediaType()
{
	bool found = false;

	DWORD index = 0;
	while (!found) {
		ComPtr<IMFMediaType> pNativeType = nullptr;
		HRESULT hr = m_pReader->GetNativeMediaType(m_dwReaderStream, index, &pNativeType);
		if (FAILED(hr))
			break;

		found = m_bIsVideo ? TestVideoMediaType(pNativeType) : TestAudioMediaType(pNativeType);
		++index;
	}

	assert(found);
	return found;
}

bool CMFCapture::TestVideoMediaType(ComPtr<IMFMediaType> pNativeType)
{
	GUID subtype = {0};
	if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype)) && IsEqualGUID(subtype, DEST_VIDEO_SUBTYPE)) {
		UINT32 width = 0, height = 0;
		if (SUCCEEDED(MFGetAttributeSize(pNativeType.Get(), MF_MT_FRAME_SIZE, &width, &height)) && width == DEST_VIDEO_WIDTH && height == DEST_VIDEO_HEIGHT) {
			UINT32 frNum = 0, frDen = 0;
			if (SUCCEEDED(MFGetAttributeRatio(pNativeType.Get(), MF_MT_FRAME_RATE, &frNum, &frDen))) {
				if (frDen != 0 && std::abs(double(frNum) / double(frDen) - DEST_VIDEO_FPS) < 0.1) {
					auto hr = m_pReader->SetCurrentMediaType(m_dwReaderStream, nullptr, pNativeType.Get());
					assert(SUCCEEDED(hr));
					if (SUCCEEDED(hr)) {
						GetDefaultStride(pNativeType.Get(), &m_yStride);
						assert(m_yStride > 0);
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool CMFCapture::TestAudioMediaType(ComPtr<IMFMediaType> pNativeType)
{
	GUID subtype = {0};
	if (SUCCEEDED(pNativeType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
		if (IsEqualGUID(subtype, MFAudioFormat_PCM) || IsEqualGUID(subtype, MFAudioFormat_Float)) {
			auto hr = m_pReader->SetCurrentMediaType(m_dwReaderStream, nullptr, pNativeType.Get());
			assert(SUCCEEDED(hr));
			if (SUCCEEDED(hr)) {
				pNativeType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &m_dwChannels);
				pNativeType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &m_dwSampleRate);
				pNativeType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &m_dwBitsPerSample);
				assert(m_dwChannels > 0 && m_dwSampleRate > 0 && m_dwBitsPerSample > 0);
				return true;
			}
		}
	}

	return false;
}

void CMFCapture::OnData(ComPtr<IMFMediaBuffer> pBuffer)
{
	if (m_bIsVideo)
		OnVideoData(pBuffer);
	else
		OnAudioData(pBuffer);
}

void CMFCapture::OnVideoData(ComPtr<IMFMediaBuffer> pBuffer)
{
	VideoBufferLock helper(pBuffer);

	BYTE *pData = NULL;
	LONG lStride = 0;
	if (FAILED(helper.LockBuffer(m_yStride, DEST_VIDEO_HEIGHT, &pData, &lStride))) {
		assert(false);
		return;
	}

	{
		// for test
		assert(lStride == DEST_VIDEO_WIDTH); // test NV12
		printf("V");

		FILE *fp = NULL;
		fopen_s(&fp, "input.nv12", "wb+");
		if (fp) {
			fwrite(pData, 1, DEST_VIDEO_WIDTH * DEST_VIDEO_HEIGHT * 3 / 2, fp);
			fclose(fp);
		}
	}

	helper.UnlockBuffer();
}

void CMFCapture::OnAudioData(ComPtr<IMFMediaBuffer> pBuffer)
{
	BYTE *pData = nullptr;
	DWORD cbMaxLength = 0, cbCurrentLength = 0;
	if (FAILED(pBuffer->Lock(&pData, &cbMaxLength, &cbCurrentLength))) {
		assert(false);
		return;
	}

	{
		// for test
		printf("A");
		static FILE *fp = NULL;
		if (!fp)
			fopen_s(&fp, "input.pcm", "wb+");

		if (fp) {
			fwrite(pData, 1, cbCurrentLength, fp);
			fflush(fp);
		}
	}

	pBuffer->Unlock();
}
