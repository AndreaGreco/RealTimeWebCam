#ifndef PCH_H
#define PCH_H

#include "framework.h"

// Force WINTRACE active in all configurations for VCamSampleSource.
// Traces are visible with TraceSpy (ETW provider 964d4572-adb9-4f3a-8170-fcbecec27467).
// Include WinTrace.h first so its #ifdef _DEBUG guard is in scope, then override.
#include "WinTrace.h"
#undef WINTRACE
#define WINTRACE(...) WinTraceFormat(0, 0, __VA_ARGS__)

#endif //PCH_H
