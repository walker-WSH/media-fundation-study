#pragma once
// for mf
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <uuids.h>

// for common header
#include <wrl\client.h>
#include <assert.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "mfuuid.lib")

template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

class CWinSection {
public:
	CWinSection() { InitializeCriticalSection(&m_cs); }
	~CWinSection() { DeleteCriticalSection(&m_cs); }
	CRITICAL_SECTION m_cs;
};

class CAutoLockCS {
public:
	explicit CAutoLockCS(CWinSection &cs) : m_cs(cs.m_cs) { EnterCriticalSection(&m_cs); }
	~CAutoLockCS() { LeaveCriticalSection(&m_cs); }
	CRITICAL_SECTION &m_cs;
};

//  Locks a video buffer that might or might not support IMF2DBuffer.
class VideoBufferLock {
public:
	VideoBufferLock(ComPtr<IMFMediaBuffer> pBuffer) : m_pBuffer(pBuffer)
	{
		// Query for the 2-D buffer interface. OK if this fails.
		(void)m_pBuffer->QueryInterface(IID_PPV_ARGS(&m_p2DBuffer));
	}

	~VideoBufferLock() { UnlockBuffer(); }

	//-------------------------------------------------------------------
	// Locks the buffer. Returns a pointer to scan line 0 and returns the stride.
	// The caller must provide the default stride as an input parameter, in case
	// the buffer does not expose IMF2DBuffer. You can calculate the default stride
	// from the media type.
	//-------------------------------------------------------------------
	HRESULT LockBuffer(LONG lDefaultStride,    // Minimum stride (with no padding).
			   DWORD dwHeightInPixels, // Height of the image, in pixels.
			   BYTE **ppbScanLine0,    // Receives a pointer to the start of scan line 0.
			   LONG *plStride)         // Receives the actual stride.
	{
		HRESULT hr = S_OK;

		// Use the 2-D version if available.
		if (m_p2DBuffer) {
			hr = m_p2DBuffer->Lock2D(ppbScanLine0, plStride);
		} else {
			// Use non-2D version.
			BYTE *pData = NULL;

			hr = m_pBuffer->Lock(&pData, NULL, NULL);
			if (SUCCEEDED(hr)) {
				*plStride = lDefaultStride;
				if (lDefaultStride < 0) {
					// Bottom-up orientation. Return a pointer to the start of the
					// last row *in memory* which is the top row of the image.
					*ppbScanLine0 = pData + abs(lDefaultStride) * (dwHeightInPixels - 1);
				} else {
					// Top-down orientation. Return a pointer to the start of the
					// buffer.
					*ppbScanLine0 = pData;
				}
			}
		}

		m_bLocked = (SUCCEEDED(hr));

		return hr;
	}

	void UnlockBuffer()
	{
		if (m_bLocked) {
			if (m_p2DBuffer) {
				(void)m_p2DBuffer->Unlock2D();
			} else {
				(void)m_pBuffer->Unlock();
			}
			m_bLocked = FALSE;
		}
	}

private:
	ComPtr<IMFMediaBuffer> m_pBuffer = nullptr;
	ComPtr<IMF2DBuffer> m_p2DBuffer = nullptr;
	BOOL m_bLocked = FALSE;
};

// 测试发现，同一个camera，使用directshow的“DevicePath” 和 mf枚举到的设备id 是不一样的
// 但是两个不同的id，调用 CreateVideoSource 都可以成功创建mediasource
// mf的官方demo，通过WM_DEVICECHANGE监控设备移除，其中的设备id字符串，和dshow的一致
static ComPtr<IMFMediaSource> CreateMediaSource(bool video, const WCHAR *name, const WCHAR *path)
{
	if (!path)
		return nullptr;

	ComPtr<IMFAttributes> sourceAttributes = nullptr;
	ComPtr<IMFMediaSource> source = nullptr;

	HRESULT hr = MFCreateAttributes(&sourceAttributes, 3); // 3: SetGUID, SetString, SetString
	if (SUCCEEDED(hr)) {
		hr = sourceAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);
		if (SUCCEEDED(hr)) {
			hr = sourceAttributes->SetString(video ? MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK : MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_SYMBOLIC_LINK,
							 reinterpret_cast<LPCWSTR>(path));
			if (SUCCEEDED(hr)) {
				if (name)
					hr = sourceAttributes->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, reinterpret_cast<LPCWSTR>(name));

				if (SUCCEEDED(hr)) {
					hr = MFCreateDeviceSource(sourceAttributes.Get(), &source);
					if (SUCCEEDED(hr))
						return source;
				}
			}
		}
	}

	return nullptr;
}

static HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
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
