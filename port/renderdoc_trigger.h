#pragma once

/**
 * renderdoc_trigger.h — In-process RenderDoc capture trigger for the SSB64 port.
 *
 * When enabled via the SSB64_RENDERDOC_FRAMES env var, the port loads
 * renderdoc.dll (if RenderDoc is installed), retrieves the in-application
 * API, and calls TriggerCapture() on specified frame numbers. RenderDoc
 * captures the next Present-to-Present interval to a .rdc file.
 *
 * Env vars:
 *   SSB64_RENDERDOC_FRAMES = "10,55,100"  — comma list of frames to capture.
 *                            Use "all" to capture every frame (heavy).
 *   SSB64_RENDERDOC_DIR    = "some/dir"   — output directory (default:
 *                            debug_traces/renderdoc). RenderDoc appends
 *                            _<datetime>_frame<N>.rdc to the template.
 *
 * If SSB64_RENDERDOC_FRAMES is unset/empty, init is skipped — zero overhead.
 *
 * Call order:
 *   portRenderDocInit()          — once at app start, BEFORE D3D11 device
 *                                  is created (i.e. before PortInit).
 *   portRenderDocOnFrame(count)  — once per frame, before the frame's Present.
 *   portRenderDocShutdown()      — once at app shutdown (no-op, provided
 *                                  for symmetry).
 */

#ifdef __EMSCRIPTEN__

/* WASM: renderdoc_trigger.cpp is not compiled (renderdoc_app.h has no
 * Emscripten branch and there is no RenderDoc in a browser anyway). */
static inline void portRenderDocInit(void) {}
static inline void portRenderDocOnFrame(unsigned int frame_count) { (void)frame_count; }
static inline void portRenderDocShutdown(void) {}

#else

#ifdef __cplusplus
extern "C" {
#endif

void portRenderDocInit(void);
void portRenderDocOnFrame(unsigned int frame_count);
void portRenderDocShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __EMSCRIPTEN__ */
