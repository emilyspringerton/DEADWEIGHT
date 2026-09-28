/* Stand-in for <SDL.h> on the wasm32-unknown-unknown freestanding target -- the real "SDL layer
 * for wasm" the founder asked for (2026-09-28: "write an sdl layer for wasm for when we dont have
 * SDL abstract our shit so we can use PARENA to write the same exact logic"). apps/gui/fx.c's own
 * SDL footprint is tiny and already funneled through a handful of primitive wrappers at the top of
 * that file (setc/frect/fline/fcircle/fring/fpoly) -- exactly five real SDL calls, no textures, no
 * events, no window/renderer creation (fx.c never creates R itself, it's handed one via FxHost).
 * That means this header only has to declare that same five-function surface; nothing else in
 * fx.c/fx.h touches SDL at all. (sfx.c's own SDL audio-device calls are compiled out entirely via
 * its existing -DDW_SFX_NO_SDL build flag -- see apps/gui/sfx.c's own #ifndef DW_SFX_NO_SDL guards
 * -- so this header is never even included for sfx.c.)
 *
 * Real SDL2 signatures are matched exactly (including the SDL_Renderer opacity and the int return
 * codes fx.c already ignores) so this stays a genuine drop-in, not a divergent lookalike -- the
 * same discipline apps/wasm/string.h already set for <string.h>. The five function bodies are
 * intentionally left undeclared as *implementations* here: apps/wasm's build always links with
 * --allow-undefined, so an undefined-but-called extern becomes a real wasm import (module "env",
 * name = the C symbol) that the browser host (web/src/fxWasm.ts) implements by drawing to a
 * Canvas2D 2D context -- Windows implements the identical five calls via real SDL2. Same C source,
 * two hosts, no reimplementation. */
#ifndef DW_WASM_SDL_H
#define DW_WASM_SDL_H
#include <stdint.h>

typedef uint8_t Uint8;
typedef uint32_t Uint32;

typedef struct SDL_Renderer SDL_Renderer;   /* opaque -- fx.c never dereferences it, only passes it through */

typedef struct { int x, y, w, h; } SDL_Rect;

typedef enum { SDL_BLENDMODE_NONE = 0x00000000, SDL_BLENDMODE_BLEND = 0x00000001 } SDL_BlendMode;

int SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_RenderDrawLine(SDL_Renderer *renderer, int x1, int y1, int x2, int y2);
int SDL_RenderDrawPoint(SDL_Renderer *renderer, int x, int y);

#endif
