#pragma once

// HOMETILES_ASSIST_DATA_SCREENS: Assist screens drawn from screen files
// (assist_screen.h). On by default on every panel with Assist (a speaker
// board): Sentinel, Phosphor, Light Disc, Lens and Iris exist only as files.
// A build may set it explicitly (the desktop lab and the host tests do).

#ifndef HOMETILES_ASSIST_DATA_SCREENS
#include "src/audio/speaker_select.h"
#if defined(HOMETILES_SPEAKER)
#define HOMETILES_ASSIST_DATA_SCREENS 1
#else
#define HOMETILES_ASSIST_DATA_SCREENS 0
#endif
#endif
