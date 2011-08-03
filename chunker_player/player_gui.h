#ifndef _CHUNKER_PLAYER_GUI_H
#define _CHUNKER_PLAYER_GUI_H

#include "player_defines.h"
#include "player_core.h"
#include "chunker_player.h"
#include <SDL.h>
#include <SDL_mutex.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

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

SDL_Cursor *defaultCursor;
SDL_Cursor *handCursor;

int ChunkerPlayerGUI_Init();
void ChunkerPlayerGUI_Close();
void ChunkerPlayerGUI_HandleResize(int w, int h);
void ChunkerPlayerGUI_HandleGetFocus();
void ChunkerPlayerGUI_HandleMouseMotion(int x, int y);
void ChunkerPlayerGUI_HandleLButton(int x, int y);
void ChunkerPlayerGUI_HandleKey();
void ChunkerPlayerGUI_SetupOverlayRect(int w, int h, float r);
void ChunkerPlayerGUI_ForceResize(int w, int h);
void ChunkerPlayerGUI_SetChannelTitle(char* title);
void ChunkerPlayerGUI_SetChannelRatio(float ratio);
void ChunkerPlayerGUI_SetStatsText(char* audio_text, char* video_text, int ledstatus);
void ChunkerPlayerGUI_ToggleFullscreen();
void ChunkerPlayerGUI_ChannelSwitched();
void ChunkerPlayerGUI_ChangeRatio(void);
void GetScreenSizeFromOverlay(int overlayWidth, int overlayHeight, int* screenWidth, int* screenHeight);
void ChunkerPlayerGUI_AspectRatioResize(float aspect_ratio, int width, int height, int* out_width, int* out_height);

SButton Buttons[NBUTTONS];

#endif
