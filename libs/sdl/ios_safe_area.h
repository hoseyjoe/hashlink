// iOS only, shared by sdl.c and gl.c. An iOS window always covers the whole screen, the status
// bar, Dynamic Island and home indicator included. Heaps is instead shown the safe area as if it
// were the window: win_get_size answers its size, mouse and touch positions are relative to it,
// and what is drawn to the screen (not to a texture) lands inside it - gl_viewport and gl_scissor
// are moved over. A clear is not confined by the viewport, so the bands outside take the clear
// colour. Sdl.setHint("HL_IOS_SAFE_AREA", "0") before the window is made gives the whole screen.
#ifndef HL_IOS_SAFE_AREA_H
#define HL_IOS_SAFE_AREA_H
#if defined(HL_IOS)
#include <SDL3/SDL.h>

// The safe area in pixels, from the top left of the whole drawable (fullh pixels high).
static inline void hl_ios_safe_rect( SDL_Window *win, int *x, int *y, int *w, int *h, int *fullh ) {
	int pw = 0, ph = 0;
	SDL_Rect r;
	if( win ) SDL_GetWindowSizeInPixels(win, &pw, &ph);
	*x = 0; *y = 0; *w = pw; *h = ph;
	if( fullh ) *fullh = ph;
	if( !win || !SDL_GetHintBoolean("HL_IOS_SAFE_AREA", true) || !SDL_GetWindowSafeArea(win, &r) || r.w <= 0 || r.h <= 0 )
		return;
	float d = SDL_GetWindowPixelDensity(win);
	if( d <= 0 ) d = 1;
	*x = (int)(r.x * d + 0.5f);
	*y = (int)(r.y * d + 0.5f);
	*w = (int)(r.w * d + 0.5f);
	*h = (int)(r.h * d + 0.5f);
	if( *x + *w > pw ) *w = pw - *x;
	if( *y + *h > ph ) *h = ph - *y;
}
#endif
#endif
