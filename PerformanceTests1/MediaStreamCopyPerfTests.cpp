#include "pch.h"
#include "CppUnitTest.h"
#include "../VirtualCamera/RtspSessionManager.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// TODO: CopyRtspFrame is a private member of MediaStream; these tests need refactoring.
// Options:
//   A) Extract the NV12 copy logic into a testable free function
//   B) Use a friend-test pattern
// For now the tests are placeholders that verify MF infrastructure only.

namespace VirtualCameraPerfTests
{
TEST_CLASS(MediaStreamCopyPerfTests)
{
public:
TEST_METHOD(MFStartupShutdown_Roundtrip)
{
	Assert::AreEqual((HRESULT)S_OK, MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
	Assert::AreEqual((HRESULT)S_OK, MFShutdown());
}

TEST_METHOD(CreateContiguousBuffer_NV12_1080p)
{
	Assert::AreEqual((HRESULT)S_OK, MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));

	const UINT32 width = 1920;
	const UINT32 height = 1080;
	const DWORD nv12Size = (width * height * 3) / 2;

	wil::com_ptr_nothrow<IMFSample> sample;
	wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
	Assert::AreEqual(S_OK, MFCreateSample(&sample));
	Assert::AreEqual(S_OK, MFCreateMemoryBuffer(nv12Size, &buffer));
	Assert::AreEqual(S_OK, sample->AddBuffer(buffer.get()));

	// Verify the buffer is accessible for writing (simulates what CopyRtspFrame does)
	BYTE* dst = nullptr; DWORD maxLen = 0;
	Assert::AreEqual(S_OK, buffer->Lock(&dst, &maxLen, nullptr));
	Assert::IsTrue(dst != nullptr);
	Assert::IsTrue(maxLen >= nv12Size);
	FillMemory(dst, nv12Size, 0x80);
	Assert::AreEqual(S_OK, buffer->Unlock());
	Assert::AreEqual(S_OK, buffer->SetCurrentLength(nv12Size));

	Assert::AreEqual((HRESULT)S_OK, MFShutdown());
}
};
}
