#ifndef HXO_ENGINE_H
#define HXO_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize SDL3 and create window
// Returns 0 on success, non-zero on failure
int engine_init(const char* title, int width, int height);

// Cleanup and shutdown
void engine_shutdown(void);

// Poll events, returns true if should quit
bool engine_poll_events(void);

// Get current tick count in milliseconds
uint64_t engine_get_ticks(void);

#ifdef __cplusplus
}
#endif

#endif // HXO_ENGINE_H
