// eez/flow/lvgl_api.h — shim for UI‑only builds
#pragma once

// Some EEZ exports include this even when Flow is off.
// This stub keeps compilation/linking happy. Nothing here runs.

#ifdef __cplusplus
extern "C" {
#endif

// ---- Minimal placeholders that some UI-only exports reference ----

// If your ui.c calls ui_tick(), you can provide a no-op here:
static inline void ui_tick(void) {}

// If anything expects a generic init, provide a no-op:
static inline void eez_flow_init(void) {}

// Add more no-ops only if the compiler later complains about missing symbols.

#ifdef __cplusplus
}
#endif
