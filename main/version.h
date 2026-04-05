#pragma once

// Version format: YYYY.MINOR.PATCH
// YYYY  — year of release
// MINOR — feature bump within the year
// PATCH — bug fix bump
//
// Build number is auto-generated at compile time from __DATE__ and __TIME__
// to produce a unique-ish integer (MMDDHHMM).

#define VERSION_YEAR    2026
#define VERSION_MINOR   1
#define VERSION_PATCH   0

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

#define FIRMWARE_VERSION \
    STRINGIFY(VERSION_YEAR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)
