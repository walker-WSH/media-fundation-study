#pragma once
#include "mf-util.hpp"
#include <string>
#include <vector>

struct MFDevice {
	std::wstring name = L"";
	std::wstring path = L"";
};

std::vector<MFDevice> EnumDevices(bool video);
HRESULT EnumCapability(ComPtr<IMFMediaSource> pSource, bool video);

std::string GetVideoSubtypeString(const GUID &subtype);
std::string GetAudioSubtypeString(const GUID &subtype);
