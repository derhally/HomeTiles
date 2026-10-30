#pragma once

// Assist screens from Home Assistant (docs-dev/assist-screens.md): the
// screen files the Bridge offers ({base}/assist/screens) and the chosen one
// ({base}/assist/screen/<id>), kept in the internal storage so it also shows
// while the Bridge is away. The choice is stored across restarts.
//
// Compiled only with HOMETILES_ASSIST_DATA_SCREENS=1; otherwise every call is
// an empty inline stub. Loop task only.

#include <stddef.h>
#include <stdint.h>

#include "src/ui/assist/assist_screen.h"

namespace assist_screen_store {

#if HOMETILES_ASSIST_DATA_SCREENS

// Reads the stored choice; once at boot, after the storage is mounted.
void begin();
// The catalog and chosen file topics, after every (re)connect.
void subscribeTopics();
// Handles {base}/assist/screens and {base}/assist/screen/<id>; true when the
// topic belonged here.
bool handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);

// The Bridge's screen files, in its order.
size_t count();
const char* id(size_t index);
const char* name(size_t index);
int indexOf(const char* id);
// The ids, for the Assist status ("screen_styles").
const char* const* ids();

// The chosen file screen, or nullptr for a built-in style. A choice whose
// file the Bridge no longer offers stays until a style is picked, so a
// restarting Bridge does not lose it.
const char* chosen();
// Picks a file screen (an id from the catalog) or nullptr for the built-in
// styles; false when the id is unknown. Stores the choice and follows its
// topic.
bool choose(const char* id);
// Copies the chosen file's kept text into buffer (kMaxFileBytes); false when
// there is none yet.
bool readChosen(char* buffer, size_t capacity, size_t* length);
// Why the chosen file was refused (empty when it loaded); set by the overlay.
void setError(const char* reason);
const char* error();

#else

inline void begin() {}
inline void subscribeTopics() {}
inline bool handleMqttMessage(const char*, const uint8_t*, size_t) { return false; }
inline size_t count() { return 0; }
inline const char* id(size_t) { return ""; }
inline const char* name(size_t) { return ""; }
inline int indexOf(const char*) { return -1; }
inline const char* const* ids() { return nullptr; }
inline const char* chosen() { return nullptr; }
inline bool choose(const char*) { return false; }
inline bool readChosen(char*, size_t, size_t*) { return false; }
inline void setError(const char*) {}
inline const char* error() { return ""; }

#endif

}  // namespace assist_screen_store
