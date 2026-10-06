#pragma once

class FrameGenerator
{
	UINT _width;
	UINT _height;
	ULONGLONG _frame;
	MFTIME _prevTime;
	UINT _fps;
	HANDLE _deviceHandle;
	wil::com_ptr_nothrow<ID3D11Texture2D> _texture;
	wil::com_ptr_nothrow<ID2D1RenderTarget> _renderTarget;
	wil::com_ptr_nothrow<ID2D1SolidColorBrush> _whiteBrush;
	wil::com_ptr_nothrow<IDWriteTextFormat> _textFormat;
	wil::com_ptr_nothrow<IDWriteFactory> _dwrite;
	wil::com_ptr_nothrow<IMFTransform> _converter;
	wil::com_ptr_nothrow<IWICBitmap> _bitmap;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager;

	// The user's offline image (path from MF_VCAM_OFFLINE_IMAGE, see Shared/VCamConfig.h).
	// Bound to _renderTarget; the file is re-checked at most every few seconds so the
	// user can change it while the camera is running.
	std::wstring _offlineImagePath;
	wil::com_ptr_nothrow<IWICImagingFactory> _wicFactory;
	wil::com_ptr_nothrow<ID2D1Bitmap> _offlineImage;
	FILETIME _offlineImageWriteTime;
	ULONGLONG _offlineImageSize;
	ULONGLONG _offlineImageNextCheck;

	HRESULT CreateRenderTargetResources(UINT width, UINT height);
	void RefreshOfflineImage();
	HRESULT LoadOfflineImage(const std::wstring& path);

public:
	FrameGenerator() :
		_width(0),
		_height(0),
		_frame(0),
		_fps(0),
		_deviceHandle(nullptr),
		_prevTime(MFGetSystemTime()),
		_offlineImageWriteTime{},
		_offlineImageSize(0),
		_offlineImageNextCheck(0)
	{

	}

	~FrameGenerator()
	{
		if (_dxgiManager && _deviceHandle)
		{
			auto hr = _dxgiManager->CloseDeviceHandle(_deviceHandle); // don't report error at that point
			if (FAILED(hr))
			{
				WINTRACE(L"FrameGenerator CloseDeviceHandle: 0x%08X", hr);
			}
		}
	}

	// Full path of the user's offline image; empty = default text frame. Takes effect
	// on the next Generate().
	void SetOfflineImagePath(const wchar_t* path);
	HRESULT SetD3DManager(IUnknown* manager, UINT width, UINT height);
	const bool HasD3DManager() const;
	HRESULT EnsureRenderTarget(UINT width, UINT height);
	// drawCounter/counter: diagnostic overlay — when true, the given value is drawn on
	// the synthetic frame so it advances at the same "render" rate as the real path.
	HRESULT Generate(IMFSample* sample, REFGUID format, IMFSample** outSample,
	                 bool drawCounter = false, UINT64 counter = 0);
};