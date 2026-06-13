#include "pch.h"
#include "CppUnitTest.h"
#include "../VirtualCamera/MediaStream.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace VirtualCameraPerfTests
{
TEST_CLASS(MediaStreamCopyPerfTests)
{
public:
TEST_METHOD(CopyRtspBufferToTargetSample_NV12_1080p)
{
Assert::AreEqual((HRESULT)S_OK, MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));

const UINT32 width = 1920;
const UINT32 height = 1080;
const DWORD sourceSize = (width * height * 3) / 2;
std::vector<BYTE> source(sourceSize);
for (DWORD i = 0; i < sourceSize; ++i)
{
source[i] = static_cast<BYTE>(i);
}

wil::com_ptr_nothrow<IMFSample> sample;
wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
Assert::AreEqual(S_OK, MFCreateSample(&sample));
Assert::AreEqual(S_OK, MFCreateMemoryBuffer(sourceSize, &buffer));
Assert::AreEqual(S_OK, sample->AddBuffer(buffer.get()));

for (int iteration = 0; iteration < 500; ++iteration)
{
Assert::AreEqual(S_OK, CopyRtspBufferToTargetSample(source.data(), sourceSize, sample.get(), width, height, MFVideoFormat_NV12));
}

Assert::AreEqual((HRESULT)S_OK, MFShutdown());
}
};
}
