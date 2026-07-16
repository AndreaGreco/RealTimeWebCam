#ifndef PCH_H
#define PCH_H

#include "framework.h"

// WINTRACE is compiled into all configurations (Release included) but is gated
// at runtime by WinTraceEnabled() (RTVCAM_TRACE=1); off by default in production.
// Traces are visible with TraceSpy (ETW provider 964d4572-adb9-4f3a-8170-fcbecec27467).
#include "WinTrace.h"

#endif //PCH_H
