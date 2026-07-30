// Single source of truth for the installer version.
//
// Plain preprocessor defines only, no C++: this header is included both by the
// C++ sources and by the resource scripts, and rc.exe understands nothing else.
#pragma once

#define BCD_VERSION_MAJOR 1
#define BCD_VERSION_MINOR 0
#define BCD_VERSION_PATCH 0
#define BCD_VERSION_BUILD 0

#define BCD_VERSION_STR  "1.0.0"
#define BCD_VERSION_WSTR L"1.0.0"
