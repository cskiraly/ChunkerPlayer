#ifndef _CHUNKER_PLAYER_GUI_H
#define _CHUNKER_PLAYER_GUI_H

#include "player_defines.h"
#include "player_core.h"
#include "chunker_player.h"
#include <SDL.h>
// #include <SDL_thread.h>
#include <SDL_mutex.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
// #include <SDL_video.h>

typedef struct SButton
{
	int Hover;
	int Visible;
	int XOffset;
	SDL_Rect ButtonIconBox;
	SDL_Surface* ButtonIcon;
	SDL_Surface* ButtonHoverIcon;
	struct SButton* ToggledButton;
	void (*HoverCallback)();
	void (*LButtonUpCallback)();
} SButton;

int FullscreenWidth;
int FullscreenHeight;

float ratio;
SDL_Cursor *defaultCursor;
SDL_Cursor *handCursor;

SDL_Cursor *InitSystemCursor(const char *image[]);
void AspectRatioResize(float aspect_ratio, int width, int height, int* out_width, int* out_height);
int ChunkerPlayerGUI_Init();
void ChunkerPlayerGUI_Close();
void ChunkerPlayerGUI_HandleResize(int w, int h);
void ChunkerPlayerGUI_HandleGetFocus();
void ChunkerPlayerGUI_HandleMouseMotion(int x, int y);
void ChunkerPlayerGUI_HandleLButton(int x, int y);
void ChunkerPlayerGUI_HandleKey();
void ChunkerPlayerGUI_SetupOverlayRect(SChannel* channel);
void ChunkerPlayerGUI_ForceResize(int w, int h);
// void ChunkerPlayerGUI_HandleChannelChanged(SChannel* channel);
void ChunkerPlayerGUI_SetChannelTitle(char* title);
void RedrawButtons();
void ChunkerPlayerGUI_ToggleFullscreen();
void UpdateOverlaySize(float aspect_ratio, int width, int height);
void GetScreenSizeFromOverlay(int overlayWidth, int overlayHeight, int* screenWidth, int* screenHeight);
// void ZapDown();
// void ZapUp();

SButton Buttons[NBUTTONS];

/* XPM */
static char *handXPM[] = {
/* columns rows colors chars-per-pixel */
"32 32 3 1",
"  c black",
". c gray100",
"X c None",
/* pixels */
"XXXXX  XXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. XXXXXXXXXXXXXXXXXXXXXXXX",
"XXXX ..   XXXXXXXXXXXXXXXXXXXXXX",
"XXXX .. ..   XXXXXXXXXXXXXXXXXXX",
"XXXX .. .. ..  XXXXXXXXXXXXXXXXX",
"XXXX .. .. .. . XXXXXXXXXXXXXXXX",
"   X .. .. .. .. XXXXXXXXXXXXXXX",
" ..  ........ .. XXXXXXXXXXXXXXX",
" ... ........... XXXXXXXXXXXXXXX",
"X .. ........... XXXXXXXXXXXXXXX",
"XX . ........... XXXXXXXXXXXXXXX",
"XX ............. XXXXXXXXXXXXXXX",
"XXX ............ XXXXXXXXXXXXXXX",
"XXX ........... XXXXXXXXXXXXXXXX",
"XXXX .......... XXXXXXXXXXXXXXXX",
"XXXX .......... XXXXXXXXXXXXXXXX",
"XXXXX ........ XXXXXXXXXXXXXXXXX",
"XXXXX ........ XXXXXXXXXXXXXXXXX",
"XXXXX          XXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
"0,0"
};

#endif
