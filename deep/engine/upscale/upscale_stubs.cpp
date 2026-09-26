// Milestone 5 staging: vendor backends land one file at a time (upscale_fsr.cpp, upscale_dlss.cpp,
// upscale_xess.cpp). Until a backend exists its factory returns nullptr and the renderer logs
// "not built" and falls through to the next candidate. Remove the matching line when the real
// backend lands; build.bat compiles upscale\*.cpp so nothing else changes.
#include "upscale.h"

namespace up {
#ifndef UP_HAVE_DLSS
Upscaler* createDLSS() { return nullptr; }
#endif
#ifndef UP_HAVE_FSR
Upscaler* createFSR()  { return nullptr; }
#endif
#ifndef UP_HAVE_XESS
Upscaler* createXeSS() { return nullptr; }
#endif
}
