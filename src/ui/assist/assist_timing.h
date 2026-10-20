#pragma once

// Frame timing of the Assist screen, measured on the panel itself and read on
// the Web Admin diagnostics page (/api/assist-timing), where the numbers
// reach a maintainer without a serial console.
//
// While the overlay shows, every LVGL refresh that drew something is one
// frame: its draw time (refresh start to refresh ready, so rendering plus the
// copies into the display memory), the share spent in the flush callback (the
// copies), the pixels flushed, and whether it ran over the 33 ms frame period.
// The overlay adds its own work per timer tick (the engine's frame, the
// invalidation) and the time between ticks (the frame interval the user sees).
// Everything is kept per screen (a file id or a built-in style id) and phase
// (listening, thinking, answering): count, sum, maximum and a 1 ms histogram
// of the draw time for its 95th percentile.
//
// Cost: a few integer operations per refresh event, a 17 KB block allocated
// from PSRAM at the first showing. No logging, no allocation per frame.
//
// Loop task (LVGL) only.

#include <stdint.h>

#include <WString.h>

// Measuring is a maintainer's tool: compiled into the beta builds and into a
// build with HOMETILES_ASSIST_TIMING; release builds carry no counters, no
// card and no endpoints (the calls below are empty then).
#if (defined(HOMETILES_ISSUE38_BETA) || defined(HOMETILES_TEST_BETA) || defined(HOMETILES_ASSIST_TIMING)) && \
    !defined(HOMETILES_ASSIST_TIMING_ACTIVE)
#define HOMETILES_ASSIST_TIMING_ACTIVE 1
#endif
#ifndef HOMETILES_ASSIST_TIMING_ACTIVE
#define HOMETILES_ASSIST_TIMING_ACTIVE 0
#endif

namespace assist_timing {

enum Phase : uint8_t { kListening, kThinking, kAnswering, kPhaseCount };

#if HOMETILES_ASSIST_TIMING_ACTIVE
// Hooks the default display's refresh events once (idempotent).
void attach();

// The overlay shows the screen id (a file id or a built-in style id, up to 32
// bytes); from_file says which. The refreshes until end() count for it.
void begin(const char* id, bool from_file);
void setPhase(Phase phase);
void end();

// One tick of the overlay's frame timer took work_us of its own work.
void noteFrame(uint32_t work_us);

void reset();

// Every screen's numbers as JSON ({"screens":[...]}, times in microseconds).
void toJson(String& out);
#else
inline void attach() {}
inline void begin(const char*, bool) {}
inline void setPhase(Phase) {}
inline void end() {}
inline void noteFrame(uint32_t) {}
inline void reset() {}
inline void toJson(String& out) { out += "{\"screens\":[]}"; }
#endif

}  // namespace assist_timing
