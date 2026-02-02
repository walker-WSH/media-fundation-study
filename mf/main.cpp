#include "mf-util.hpp"
#include "mf-enum.h"
#include "mf-capture.h"

//---------------------------------------------------------------------------------------------
int main()
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(hr))
		return -1;
	hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		CoUninitialize();
		return -1;
	}

	// 如下两次调用传入的字符串，一个是dshow的DevicePath，一个是mf枚举到的视频设备id：MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK
	CreateMediaSource(true, L"Logitech BRIO", L"\\\\?\\usb#vid_046d&pid_085e&mi_00#9&1aa60e46&0&0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\\global");
	CreateMediaSource(true, L"Logitech BRIO", L"\\\\?\\usb#vid_046d&pid_085e&mi_00#9&1aa60e46&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global");

	auto videoDevices = EnumDevices(true);
	auto audioDevices = EnumDevices(false);

	ComPtr<CMFCapture> vCapture;
	ComPtr<CMFCapture> aCapture;

	for (const auto &dev : videoDevices) {
		if (dev.name.find(L"Logitech") == std::wstring::npos)
			continue;

		vCapture = CMFCapture::CreateInstance(true, dev.name.c_str(), dev.path.c_str());
		if (vCapture) {
			if (vCapture->StartCapture()) {
				printf("succeeded to capture video \n");
			}
		}
	}

	for (const auto &dev : audioDevices) {
		if (dev.name.find(L"Logitech") == std::wstring::npos)
			continue;

		aCapture = CMFCapture::CreateInstance(false, dev.name.c_str(), dev.path.c_str());
		if (aCapture) {
			if (aCapture->StartCapture()) {
				printf("succeeded to capture audio \n");
			}
		}
	}

	Sleep(10000);
	if (vCapture) {
		vCapture->StopCapture();
		vCapture = nullptr;
	}
	if (aCapture) {
		aCapture->StopCapture();
		aCapture = nullptr;
	}

	MFShutdown();
	CoUninitialize();
	return 0;
}
