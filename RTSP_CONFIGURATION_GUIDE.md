# Soluzione finale: IMFVirtualCamera::AddProperty per passare URL RTSP

## Panoramica
Invece di usare variabili di ambiente o shared memory, usiamo `IMFVirtualCamera::AddProperty()` 
che è il metodo standard Microsoft per passare configurazioni custom alla virtual camera.

## File da modificare

### 1. VirtualCamera\SharedConfig.h
```cpp
#pragma once

// Custom GUID for RTSP URL property
// {B8E0A5C1-2D3F-4A6B-9C8D-1E2F3A4B5C6D}
DEFINE_GUID(MF_VCAM_RTSP_URL,
    0xb8e0a5c1, 0x2d3f, 0x4a6b, 0x9c, 0x8d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d);
```

### 2. MFPipeline\VirtualCamera.cpp

**All'inizio del file, dopo gli includes:**
```cpp
// Custom GUID for RTSP URL property (must match VirtualCamera project)
// {B8E0A5C1-2D3F-4A6B-9C8D-1E2F3A4B5C6D}
static const GUID MF_VCAM_RTSP_URL = { 0xb8e0a5c1, 0x2d3f, 0x4a6b, {0x9c, 0x8d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d} };
```

**Nel metodo SetRTSPUrl() - rimpiazza completamente:**
```cpp
void VirtualCamera::SetRTSPUrl(const wchar_t* url)
{
	if (url != nullptr)
	{
		_rtspUrl = url;
		
		// If camera is already registered, set the property immediately
		if (_isRegistered && _vcam != nullptr)
		{
			HRESULT hr = _vcam->AddProperty(MF_VCAM_RTSP_URL, url, (UINT32)((wcslen(url) + 1) * sizeof(WCHAR)));
			if (SUCCEEDED(hr))
			{
				DebugLog("VirtualCamera::SetRTSPUrl - RTSP URL property set successfully via AddProperty");
			}
			else
			{
				std::ostringstream oss;
				oss << "VirtualCamera::SetRTSPUrl - AddProperty failed with HRESULT: 0x" << std::hex << hr;
				DebugLog(oss.str().c_str());
			}
		}
		else
		{
			DebugLog("VirtualCamera::SetRTSPUrl - RTSP URL stored, will be set when camera is registered");
		}
	}
}
```

**Nel metodo RegisterVirtualCamera(), DOPO `_isRegistered = true;` aggiungi:**
```cpp
	// If RTSP URL was set before registration, add it as a property now
	if (!_rtspUrl.empty())
	{
		HRESULT hrProp = _vcam->AddProperty(MF_VCAM_RTSP_URL, _rtspUrl.c_str(), (UINT32)((_rtspUrl.length() + 1) * sizeof(WCHAR)));
		if (SUCCEEDED(hrProp))
		{
			DebugLog("VirtualCamera: RTSP URL property added successfully via AddProperty");
		}
		else
		{
			std::ostringstream oss;
			oss << "VirtualCamera: AddProperty failed with HRESULT: 0x" << std::hex << hrProp;
			DebugLog(oss.str().c_str());
		}
	}
```

### 3. VirtualCamera\MediaSource.cpp

**All'inizio dopo gli includes, aggiungi:**
```cpp
#include "SharedConfig.h"
```

**Nel metodo Initialize(), rimpiazza la sezione che legge l'URL con:**
```cpp
		// Try to get RTSP URL from custom property (set via IMFVirtualCamera::AddProperty)
		UINT32 urlLength = 0;
		HRESULT hr = attributes->GetStringLength(MF_VCAM_RTSP_URL, &urlLength);
		if (SUCCEEDED(hr) && urlLength > 0)
		{
			_rtspUrl.resize(urlLength + 1);
			hr = attributes->GetString(MF_VCAM_RTSP_URL, &_rtspUrl[0], urlLength + 1, &urlLength);
			if (SUCCEEDED(hr))
			{
				_rtspUrl.resize(urlLength);
				WINTRACE(L"MediaSource::Initialize - RTSP URL from property: %s", _rtspUrl.c_str());
			}
			else
			{
				_rtspUrl.clear();
				WINTRACE(L"MediaSource::Initialize - Failed to get RTSP URL string");
			}
		}
		else
		{
			WINTRACE(L"MediaSource::Initialize - No RTSP URL property found, will use black frames");
		}
```

**RIMUOVI completamente** il blocco che cerca la variabile di ambiente (GetEnvironmentVariableW).

## Come funziona

1. L'applicazione chiama `SetVirtualCameraRTSPUrl(vcam, L"rtsp://...")`
2. La VirtualCamera salva l'URL in `_rtspUrl`
3. Quando viene chiamato `RegisterVCam()`:
   - Viene creata la virtual camera con `MFCreateVirtualCamera`
   - Subito dopo, viene chiamato `_vcam->AddProperty(MF_VCAM_RTSP_URL, _rtspUrl.c_str(), ...)`
4. Quando Windows istanzia il COM server (MediaSource):
   - Il framework chiama `MediaSource::Initialize(attributes)`
   - Gli attributes contengono automaticamente le proprietà aggiunte con `AddProperty`
   - Il MediaSource legge l'URL con `attributes->GetString(MF_VCAM_RTSP_URL, ...)`
5. Il MediaSource passa l'URL ai MediaStream che inizializzano il lettore RTSP

## Vantaggi
? **Metodo standard Microsoft** - usa API documentate
? **Nessun problema di permessi** - tutto interno al processo
? **Nessuna variabile globale** - configurazione per-camera
? **Thread-safe** - gli attributes MF gestiscono automaticamente il threading
? **Pulito** - non lascia tracce nel sistema

## Uso da C#
```csharp
VirtualCamera vcam = CreateVirtualCamera(videoPlayer);
SetVirtualCameraName(vcam, "My RTSP Camera");
SetVirtualCameraRTSPUrl(vcam, "rtsp://192.168.1.100:554/stream"); // Prima di RegisterVCam!
RegisterVCam(vcam);  // Qui viene passato l'URL tramite AddProperty
StartVCam(vcam);
```

## Note importanti
- `SetVirtualCameraRTSPUrl` DEVE essere chiamato PRIMA di `RegisterVCam` 
  oppure DOPO (la funzione gestisce entrambi i casi)
- Se chiamato dopo registrazione, usa `AddProperty` immediatamente
- Il GUID `MF_VCAM_RTSP_URL` deve essere IDENTICO in entrambi i progetti
