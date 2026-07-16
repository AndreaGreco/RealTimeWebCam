#pragma once

HRESULT GetTraceId(GUID* pGuid);

ULONG WinTraceRegister();
void WinTraceUnregister();

// Runtime gate for ETW tracing. Off by default (production); enable by setting
// the system environment variable RTVCAM_TRACE=1 (the Frame Server runs as
// Local Service, so it must be a machine-level variable). Read once and cached.
bool WinTraceEnabled();

void WinTrace(UCHAR Level, ULONGLONG Keyword, PCWSTR String);
void WinTraceFormat(UCHAR Level, ULONGLONG Keyword, PCWSTR pszFormat, ...);

void WinTrace(UCHAR Level, ULONGLONG Keyword, PCSTR String);
void WinTraceFormat(UCHAR Level, ULONGLONG Keyword, PCSTR pszFormat, ...);

// Gated so that, when tracing is disabled, the trace arguments are never even
// evaluated (many call sites build strings/GUIDs inline). Default-off means
// production pays only a single cached-bool check per call site.
#define WINTRACE(...) do { if (WinTraceEnabled()) WinTraceFormat(0, 0, __VA_ARGS__); } while (0)
#pragma once
