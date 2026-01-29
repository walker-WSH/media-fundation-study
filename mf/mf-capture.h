#pragma once
#include "mf-util.hpp"

// for test
#define DEST_VIDEO_SUBTYPE MFVideoFormat_NV12
#define DEST_VIDEO_WIDTH 1280
#define DEST_VIDEO_HEIGHT 720
#define DEST_VIDEO_FPS 30.0

class CMFCapture : public IMFSourceReaderCallback {
protected:
	CMFCapture(ComPtr<IMFMediaSource> source, bool video);
	virtual ~CMFCapture();

public:
	static ComPtr<CMFCapture> CreateInstance(bool video, const WCHAR *name, const WCHAR *path);

	bool StartCapture();
	void StopCapture();

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID iid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();
	// IMFSourceReaderCallback methods
	STDMETHODIMP OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags, LONGLONG llTimestamp, IMFSample *pSample);
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) { return S_OK; }
	STDMETHODIMP OnFlush(DWORD) { return S_OK; }

private:
	bool SelectMediaType();
	bool TestVideoMediaType(ComPtr<IMFMediaType> pNativeType);
	bool TestAudioMediaType(ComPtr<IMFMediaType> pNativeType);

	void OnData(ComPtr<IMFMediaBuffer> pBuffer);
	void OnVideoData(ComPtr<IMFMediaBuffer> pBuffer);
	void OnAudioData(ComPtr<IMFMediaBuffer> pBuffer);

	void NotifyException(HRESULT /*hr*/) {} // hr = 0xc00d3ea2: device lost

private:
	long m_nRefCount = 1; // Reference count.

	const bool m_bIsVideo;
	const DWORD m_dwReaderStream;

	CWinSection m_lock;
	ComPtr<IMFMediaSource> m_pSource = nullptr;
	ComPtr<IMFSourceReader> m_pReader = nullptr;

	// video
	LONG m_yStride = 0;

	// audio
	UINT32 m_dwChannels = 0;
	UINT32 m_dwSampleRate = 0;
	UINT32 m_dwBitsPerSample = 0;
};
