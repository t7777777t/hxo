#ifndef HXO_ENGINE_H
#define HXO_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize SDL3 window and Vulkan
// Returns 0 on success, non-zero on failure
int engine_init(const char* title, int width, int height);

// Cleanup and shutdown
void engine_shutdown(void);

// Poll events, returns true if should quit
bool engine_poll_events(void);

// Get current tick count in milliseconds
uint64_t engine_get_ticks(void);

// Render a frame with clear color (RGBA 0-1 range)
// Returns 0 on success, non-zero on failure (e.g., swapchain out of date)
int engine_render_frame(float r, float g, float b, float a);

// Handle window resize (recreates swapchain)
int engine_handle_resize(void);

#ifdef __cplusplus
}
#endif

#endif // HXO_ENGINE_H
