// Unavailable vendor factories return nullptr so selection can try another backend.
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
