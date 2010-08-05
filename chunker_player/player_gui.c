#include "player_gui.h"
// #include "player_commons.h"

#define SCREEN_BOTTOM_PADDING (BUTTONS_LAYER_OFFSET + BUTTONS_CONTAINER_HEIGHT + STATS_BOX_HEIGHT)

SDL_Cursor *InitSystemCursor(const char *image[]);
void AspectRatioResize(float aspect_ratio, int width, int height, int* out_width, int* out_height);
void UpdateOverlaySize(float aspect_ratio, int width, int height);
void RedrawButtons();
void RedrawChannelName();
void RedrawStats();
void SetupGUI();

static char AudioStatsText[255];
static char VideoStatsText[255];

SDL_Surface *ChannelTitleSurface = NULL;
//SDL_Surface *AudioStatisticsSurface = NULL, *VideoStatisticsSurface = NULL;
SDL_Rect ChannelTitleRect, AudioStatisticsRect, VideoStatisticsRect, tmpRect;
SDL_Color ChannelTitleColor = { 255, 0, 0 }, StatisticsColor = { 255, 255, 255 };
SDL_Color ChannelTitleBgColor = { 0, 0, 0 }, StatisticsBgColor = { 0, 0, 0 };
TTF_Font *MainFont = NULL;
TTF_Font *StatisticsFont = NULL;

/* XPM */
static const char *handXPM[] = {
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

int ChunkerPlayerGUI_Init()
{
	// some initializations
	ratio = DEFAULT_RATIO;
	FullscreenWidth = 0;
	FullscreenHeight = 0;
	
	UpdateOverlaySize(ratio, DEFAULT_WIDTH, DEFAULT_HEIGHT);
	
	if(!SilentMode)
		SetupGUI();
	
	return 0;
}

void SetVideoMode(int width, int height, int fullscreen)
{
	if(SilentMode)
		return;
		
	// printf("SetVideoMode(%d, %d, %d)\n", width, height, fullscreen);
	SDL_LockMutex(OverlayMutex);

	if(fullscreen)
	{
#ifndef __DARWIN__
		MainScreen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE | SDL_NOFRAME | SDL_FULLSCREEN);
#else
		MainScreen = SDL_SetVideoMode(width, height, 24, SDL_SWSURFACE | SDL_NOFRAME | SDL_FULLSCREEN);
#endif
	}
	else
	{
#ifndef __DARWIN__
		MainScreen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE | SDL_RESIZABLE);
#else
		MainScreen = SDL_SetVideoMode(width, height, 24, SDL_SWSURFACE | SDL_RESIZABLE);
#endif
	}

	if(!MainScreen) {
		fprintf(stderr, "SDL_SetVideoMode returned null: could not set video mode - exiting\n");
		exit(1);
	}
	SDL_UnlockMutex(OverlayMutex);
}

void ChunkerPlayerGUI_HandleResize(int resize_w, int resize_h)
{
	if(SilentMode)
		return;
		
	// printf("ChunkerPlayerGUI_HandleResize(%d, %d)\n", resize_w, resize_h);
	SetVideoMode(resize_w, resize_h, FullscreenMode?1:0);
	
	window_width = resize_w;
	window_height = resize_h;
	
	// update the overlay surface size, mantaining the aspect ratio
	UpdateOverlaySize(ratio, resize_w, resize_h);
	
	// update each button coordinates
	int i;
	for(i=0; i<NBUTTONS; i++)
	{
		if(Buttons[i].XOffset > 0)
			Buttons[i].ButtonIconBox.x = Buttons[i].XOffset;
		else
			Buttons[i].ButtonIconBox.x = (resize_w + Buttons[i].XOffset);
			
		Buttons[i].ButtonIconBox.y = resize_h - Buttons[i].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
	}
	
	RedrawButtons();
	RedrawChannelName();
	RedrawStats();
}

void ChunkerPlayerGUI_HandleGetFocus()
{
	if(SilentMode)
		return;

	RedrawButtons();
	RedrawChannelName();
	RedrawStats();
}

void ChunkerPlayerGUI_HandleMouseMotion(int x, int y)
{
	if(SilentMode)
		return;
		
	int i;
	for(i=0; i<NBUTTONS; i++)
	{
		//If the mouse is over the button
		if(
			( x > Buttons[i].ButtonIconBox.x ) && ( x < Buttons[i].ButtonIconBox.x + Buttons[i].ButtonIcon->w )
			&& ( y > Buttons[i].ButtonIconBox.y ) && ( y < Buttons[i].ButtonIconBox.y + Buttons[i].ButtonIcon->h )
		)
		{
			Buttons[i].Hover = 1;
			SDL_SetCursor(handCursor);
			break;
		}
		
		else
		{
			Buttons[i].Hover = 0;
			SDL_SetCursor(defaultCursor);
		}
	}
}

void ChunkerPlayerGUI_HandleLButton(int x, int y)
{
	if(SilentMode)
		return;
		
	int i;
	for(i=0; i<NBUTTONS; i++)
	{
		//If the mouse is over the button
		if(
			( x > Buttons[i].ButtonIconBox.x ) && ( x < Buttons[i].ButtonIconBox.x + Buttons[i].ButtonIcon->w )
			&& ( y > Buttons[i].ButtonIconBox.y ) && ( y < Buttons[i].ButtonIconBox.y + Buttons[i].ButtonIcon->h )
		)
		{
			Buttons[i].LButtonUpCallback();
			break;
		}
	}
}

void ChunkerPlayerGUI_HandleKey()
{
	static Uint32 LastTime=0;
	static int LastKey=-1;

	Uint32 Now=SDL_GetTicks();
	Uint8* keystate=SDL_GetKeyState(NULL);
	if(keystate[SDLK_ESCAPE] &&
	  (LastKey!=SDLK_ESCAPE || (LastKey==SDLK_ESCAPE && (Now-LastTime>1000))))
	{
		LastKey=SDLK_ESCAPE;
		LastTime=Now;
		quit=1;
	}
}

void ChunkerPlayerGUI_Close()
{
	if(ChannelTitleSurface != NULL)
		SDL_FreeSurface( ChannelTitleSurface );
	
	TTF_CloseFont( MainFont );
	TTF_CloseFont( StatisticsFont );
	TTF_Quit();
	IMG_Quit();
}

void RedrawButtons()
{
	if(SilentMode)
		return;
		
	int i;
	for(i=0; i<NBUTTONS; i++)
	{
		if(Buttons[i].Visible)
		{
			if(!Buttons[i].Hover)
			{
				SDL_LockMutex(OverlayMutex);
				SDL_BlitSurface(Buttons[i].ButtonIcon, NULL, MainScreen, &Buttons[i].ButtonIconBox);
				SDL_UpdateRects(MainScreen, 1, &Buttons[i].ButtonIconBox);
				SDL_UnlockMutex(OverlayMutex);
			}
			else
			{
				SDL_LockMutex(OverlayMutex);
				SDL_BlitSurface(Buttons[i].ButtonHoverIcon, NULL, MainScreen, &(Buttons[i].ButtonIconBox));
				SDL_UpdateRects(MainScreen, 1, &(Buttons[i].ButtonIconBox));
				SDL_UnlockMutex(OverlayMutex);
			}
		}
	}
}

void RedrawChannelName()
{
	if(SilentMode)
		return;
		
	if(ChannelTitleSurface != NULL)
	{
		ChannelTitleRect.w = ChannelTitleSurface->w;
		ChannelTitleRect.h = ChannelTitleSurface->h;
		ChannelTitleRect.x = ((FullscreenMode?FullscreenWidth:window_width) - ChannelTitleRect.w)/2;
		ChannelTitleRect.y = Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.y+5;
		SDL_LockMutex(OverlayMutex);
		SDL_BlitSurface(ChannelTitleSurface, NULL, MainScreen, &ChannelTitleRect);
		SDL_UpdateRects(MainScreen, 1, &ChannelTitleRect);
		SDL_UnlockMutex(OverlayMutex);
	}
}

void ChunkerPlayerGUI_ToggleFullscreen()
{
	if(SilentMode)
		return;
		
	int i;
	//If the screen is windowed
	if( !FullscreenMode )
	{
		// printf("SETTING FULLSCREEN ON\n");
		SetVideoMode(FullscreenWidth, FullscreenHeight, 1);
		
		// update the overlay surface size, mantaining the aspect ratio
		UpdateOverlaySize(ratio, FullscreenWidth, FullscreenHeight);
		
		// update each button coordinates
		for(i=0; i<NBUTTONS; i++)
		{
			if(Buttons[i].XOffset > 0)
				Buttons[i].ButtonIconBox.x = Buttons[i].XOffset;
			else
				Buttons[i].ButtonIconBox.x = (FullscreenWidth + Buttons[i].XOffset);
				
			Buttons[i].ButtonIconBox.y = FullscreenHeight - Buttons[i].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
		}

		//Set the window state flag
		FullscreenMode = 1;
		
		Buttons[FULLSCREEN_BUTTON_INDEX].Visible = 0;
		Buttons[NO_FULLSCREEN_BUTTON_INDEX].Visible = 1;
	}
	
	//If the screen is fullscreen
	else
	{
		// printf("ToggleFullscreen callback, setting WINDOWED\n");
		SetVideoMode(window_width, window_height, 0);
		
		// update the overlay surface size, mantaining the aspect ratio
		UpdateOverlaySize(ratio, window_width, window_height);
		
		// update each button coordinates
		for(i=0; i<NBUTTONS; i++)
		{
			if(Buttons[i].XOffset > 0)
				Buttons[i].ButtonIconBox.x = Buttons[i].XOffset;
			else
				Buttons[i].ButtonIconBox.x = (window_width + Buttons[i].XOffset);
				
			Buttons[i].ButtonIconBox.y = window_height - Buttons[i].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
		}
		
		//Set the window state flag
		FullscreenMode = 0;
		
		Buttons[FULLSCREEN_BUTTON_INDEX].Visible = 1;
		Buttons[NO_FULLSCREEN_BUTTON_INDEX].Visible = 0;
	}

	RedrawButtons();
	RedrawChannelName();
	RedrawStats();
}

void AspectRatioResize(float aspect_ratio, int width, int height, int* out_width, int* out_height)
{
	int h,w;
	h = (int)((float)width/aspect_ratio);
	if(h<=height)
	{
		w = width;
	}
	else
	{
		w = (int)((float)height*aspect_ratio);
		h = height;
	}
	*out_width = w;
	*out_height = h;
}

/**
 * Updates the overlay surface size, mantaining the aspect ratio
 */
void UpdateOverlaySize(float aspect_ratio, int width, int height)
{
	// printf("UpdateOverlaySize(%f, %d, %d)\n", aspect_ratio, width, height);
	// height -= (BUTTONS_LAYER_OFFSET + BUTTONS_CONTAINER_HEIGHT);
	height -= SCREEN_BOTTOM_PADDING;
	int h = 0, w = 0, x, y;
	AspectRatioResize(aspect_ratio, width, height, &w, &h);
	x = (width - w) / 2;
	y = (height - h) / 2;
	SDL_LockMutex(OverlayMutex);
	OverlayRect.x = x;
	OverlayRect.y = y;
	OverlayRect.w = w;
	OverlayRect.h = h;
	// SDL_FillRect( SDL_GetVideoSurface(), NULL, SDL_MapRGB(SDL_GetVideoSurface()->format, 0,0,0) );
	SDL_UpdateRect(MainScreen, 0, 0, 0, 0);
	SDL_UnlockMutex(OverlayMutex);
}

void GetScreenSizeFromOverlay(int overlayWidth, int overlayHeight, int* screenWidth, int* screenHeight)
{
	*screenHeight = overlayHeight + SCREEN_BOTTOM_PADDING;
	*screenWidth = overlayWidth;
}

/* From SDL documentation. */
SDL_Cursor *InitSystemCursor(const char *image[])
{
	int i, row, col;
	Uint8 data[4*32];
	Uint8 mask[4*32];
	int hot_x, hot_y;

	i = -1;
	for ( row=0; row<32; ++row ) {
		for ( col=0; col<32; ++col ) {
			if ( col % 8 ) {
				data[i] <<= 1;
				mask[i] <<= 1;
			} else {
				++i;
				data[i] = mask[i] = 0;
			}
			
			switch (image[4+row][col]) {
				case ' ':
					data[i] |= 0x01;
					mask[i] |= 0x01;
					break;
				case '.':
					mask[i] |= 0x01;
					break;
				case 'X':
					break;
			}
		}
	}
	
	sscanf(image[4+row], "%d,%d", &hot_x, &hot_y);
	return SDL_CreateCursor(data, mask, 32, 32, hot_x, hot_y);
}

void SetupGUI()
{
	//Initialize SDL_ttf 
	if( TTF_Init() == -1 )
	{
		printf("TTF_Init: Failed to init SDL_ttf library!\n");
		printf("TTF_Init: %s\n", TTF_GetError());
		exit(1);
	}
	
	//Open the font
	MainFont = TTF_OpenFont(MAIN_FONT_FILE , MAIN_FONT_SIZE );
	StatisticsFont = TTF_OpenFont(STATS_FONT_FILE, STATS_FONT_SIZE );
	
	//If there was an error in loading the font
	if( MainFont == NULL)
	{
		printf("Cannot initialize GUI, %s file not found\n", MAIN_FONT_FILE);
		exit(1);
	}
	if( StatisticsFont == NULL )
	{
		printf("Cannot initialize GUI, %s file not found\n", STATS_FONT_FILE);
		exit(1);
	}
	
	// init SDL_image
	int flags=IMG_INIT_JPG|IMG_INIT_PNG;
	int initted=IMG_Init(flags);
	if(initted&flags != flags)
	{
		printf("IMG_Init: Failed to init required jpg and png support!\n");
		printf("IMG_Init: %s\n", IMG_GetError());
		exit(1);
	}
	
	SDL_VideoInfo* InitialVideoInfo = SDL_GetVideoInfo();
	FullscreenWidth = InitialVideoInfo->current_w;
	FullscreenHeight = InitialVideoInfo->current_h;

	SDL_Surface *temp;
	int screen_w = 0, screen_h = 0;

	if(OverlayRect.w > BUTTONS_CONTAINER_WIDTH)
		screen_w = OverlayRect.w;
	else
		screen_w = BUTTONS_CONTAINER_WIDTH;
	screen_h = OverlayRect.h + SCREEN_BOTTOM_PADDING;

	SDL_WM_SetCaption("Filling buffer...", NULL);
	// Make a screen to put our video
	
	// printf("screen_w = %d, screen_h = %d\n", screen_w, screen_h);
	
	SetVideoMode(screen_w, screen_h, 0);
	
	window_width = screen_w;
	window_height = screen_h;
	
	/** Setting up cursors */
	defaultCursor = SDL_GetCursor();
	handCursor = InitSystemCursor(handXPM);
	
	/** Init Buttons */
	int i;
	for(i=0; i<NBUTTONS; i++)
	{
		SButton* tmp = &(Buttons[i]);
		tmp->Hover = 0;
		tmp->ToggledButton = NULL;
		tmp->Visible = 1;
		tmp->HoverCallback = NULL;
		tmp->LButtonUpCallback = NULL;
	}
	
	/** Loading icons */
	
	// fullscreen
	temp = IMG_Load(FULLSCREEN_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", FULLSCREEN_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIcon = SDL_DisplayFormatAlpha(temp);
	if(Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIcon == NULL)
	{
		printf("ERROR in SDL_DisplayFormatAlpha, cannot load fullscreen button, error message: '%s'\n", SDL_GetError());
		exit(1);
	}
	SDL_FreeSurface(temp);
	
	// fullscreen hover
	temp = IMG_Load(FULLSCREEN_HOVER_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", FULLSCREEN_HOVER_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	// no fullscreen
	temp = IMG_Load(NOFULLSCREEN_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", NOFULLSCREEN_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	// no fullscreen hover
	temp = IMG_Load(NOFULLSCREEN_HOVER_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", NOFULLSCREEN_HOVER_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);
	
	// channel up
	temp = IMG_Load(CHANNEL_UP_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", CHANNEL_UP_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIcon = SDL_DisplayFormatAlpha(temp);
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);
	
	// channel down
	temp = IMG_Load(CHANNEL_DOWN_ICON_FILE);
	if (temp == NULL) {
		fprintf(stderr, "Error loading %s: %s\n", CHANNEL_DOWN_ICON_FILE, SDL_GetError());
		exit(1);
	}
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIcon = SDL_DisplayFormatAlpha(temp);
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonHoverIcon = SDL_DisplayFormatAlpha(temp);
	SDL_FreeSurface(temp);

	/** Setting up icon boxes */
	Buttons[FULLSCREEN_BUTTON_INDEX].XOffset = Buttons[NO_FULLSCREEN_BUTTON_INDEX].XOffset = 20;
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.x = 20;
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.w = Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIcon->w;
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.h = Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIcon->h;
	Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.y = screen_h - Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
	
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIconBox.x = 20;
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIconBox.w = Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIcon->w;
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIconBox.h = Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIcon->h;
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIconBox.y = screen_h - Buttons[NO_FULLSCREEN_BUTTON_INDEX].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
	
	Buttons[CHANNEL_UP_BUTTON_INDEX].XOffset = -61;
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIconBox.w = Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIcon->w;
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIconBox.h = Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIcon->h;
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIconBox.x = (screen_w + Buttons[CHANNEL_UP_BUTTON_INDEX].XOffset);
	Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIconBox.y = screen_h - Buttons[CHANNEL_UP_BUTTON_INDEX].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
	
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].XOffset = -36;
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIconBox.w = Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIcon->w;
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIconBox.h = Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIcon->h;
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIconBox.x = (screen_w + Buttons[CHANNEL_DOWN_BUTTON_INDEX].XOffset);
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIconBox.y = screen_h - Buttons[CHANNEL_DOWN_BUTTON_INDEX].ButtonIconBox.h - (SCREEN_BOTTOM_PADDING/2);
	
	// Setting up buttons events
	Buttons[FULLSCREEN_BUTTON_INDEX].ToggledButton = &(Buttons[NO_FULLSCREEN_BUTTON_INDEX]);
	Buttons[FULLSCREEN_BUTTON_INDEX].LButtonUpCallback = &ChunkerPlayerGUI_ToggleFullscreen;
	Buttons[NO_FULLSCREEN_BUTTON_INDEX].LButtonUpCallback = &ChunkerPlayerGUI_ToggleFullscreen;
	Buttons[CHANNEL_UP_BUTTON_INDEX].LButtonUpCallback = &ZapUp;
	Buttons[CHANNEL_DOWN_BUTTON_INDEX].LButtonUpCallback = &ZapDown;
}

void ChunkerPlayerGUI_SetChannelTitle(char* title)
{
	if(SilentMode)
		return;
		
	SDL_LockMutex(OverlayMutex);
	
	SDL_FreeSurface( ChannelTitleSurface );
	// ChannelTitleSurface = TTF_RenderText_Solid( MainFont, channel->Title, ChannelTitleColor );
	ChannelTitleSurface = TTF_RenderText_Shaded( MainFont, title, ChannelTitleColor, ChannelTitleBgColor );
	if(ChannelTitleSurface == NULL)
	{
		printf("WARNING: CANNOT RENDER CHANNEL TITLE\n");
	}
	
	SDL_UnlockMutex(OverlayMutex);
	
	RedrawChannelName();
}

void ChunkerPlayerGUI_SetStatsText(char* audio_text, char* video_text)
{
	if(SilentMode)
		return;
		
	if(audio_text == NULL)
		audio_text = AudioStatsText;
	
	if(video_text == NULL)
		video_text = VideoStatsText;

	if((strlen(audio_text) > 255) || (strlen(video_text) > 255))
	{
		printf("WARNING IN player_gui.c: stats text too long, could not refresh text\n");
		return;
	}
	
	strcpy(AudioStatsText, audio_text);
	strcpy(VideoStatsText, video_text);
	
	RedrawStats();
}

void RedrawStats()
{
	if(SilentMode)
		return;
		
	SDL_Surface *text_surface;

	SDL_LockMutex(OverlayMutex);
	
	// clear stats text
	SDL_FillRect( MainScreen, &VideoStatisticsRect, SDL_MapRGB(MainScreen->format, 0, 0, 0) );
	SDL_FillRect( MainScreen, &AudioStatisticsRect, SDL_MapRGB(MainScreen->format, 0, 0, 0) );
	SDL_UpdateRect(MainScreen, VideoStatisticsRect.x, VideoStatisticsRect.y, VideoStatisticsRect.w, VideoStatisticsRect.h);
	SDL_UpdateRect(MainScreen, AudioStatisticsRect.x, AudioStatisticsRect.y, AudioStatisticsRect.w, AudioStatisticsRect.h);
	
	
	text_surface = TTF_RenderText_Shaded( StatisticsFont, AudioStatsText, StatisticsColor, StatisticsBgColor );
    if (text_surface != NULL)
    {
		AudioStatisticsRect.w = text_surface->w;
		AudioStatisticsRect.h = text_surface->h;
		AudioStatisticsRect.x = ((FullscreenMode?FullscreenWidth:window_width) - AudioStatisticsRect.w)>>1;
		AudioStatisticsRect.y = Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.y+(STATS_FONT_SIZE<<1)+STATS_BOX_HEIGHT;

        SDL_BlitSurface(text_surface, NULL, MainScreen, &AudioStatisticsRect);
        SDL_FreeSurface(text_surface);
    }
    else
    {
        // report error
    }
    
	text_surface = TTF_RenderText_Shaded( StatisticsFont, VideoStatsText, StatisticsColor, StatisticsBgColor );
    if (text_surface != NULL)
    {
		VideoStatisticsRect.w = text_surface->w;
		VideoStatisticsRect.h = text_surface->h;
		VideoStatisticsRect.x = ((FullscreenMode?FullscreenWidth:window_width) - VideoStatisticsRect.w)>>1;
		VideoStatisticsRect.y = Buttons[FULLSCREEN_BUTTON_INDEX].ButtonIconBox.y+(STATS_FONT_SIZE)+STATS_BOX_HEIGHT;

        SDL_BlitSurface(text_surface, NULL, MainScreen, &VideoStatisticsRect);
        SDL_FreeSurface(text_surface);
    }
    else
    {
        // report error
    }
    
    SDL_UpdateRect(MainScreen, VideoStatisticsRect.x, VideoStatisticsRect.y, VideoStatisticsRect.w, VideoStatisticsRect.h);
	SDL_UpdateRect(MainScreen, AudioStatisticsRect.x, AudioStatisticsRect.y, AudioStatisticsRect.w, AudioStatisticsRect.h);
	
	SDL_UnlockMutex(OverlayMutex);
}

void ChunkerPlayerGUI_SetupOverlayRect(SChannel* channel)
{
	ratio = channel->Ratio;
	int w, h;
	GetScreenSizeFromOverlay(channel->Width, channel->Height, &w, &h);
	// printf("CALLING UpdateOverlaySize(%f, %d, %d)\n", ratio, w, h);
	UpdateOverlaySize(ratio, w, h);
	// UpdateOverlaySize(ratio, channel->Width, channel->Height);
}

void ChunkerPlayerGUI_ForceResize(int width, int height)
{
	FullscreenMode = 0;
	int w, h;
	GetScreenSizeFromOverlay(width, height, &w, &h);
	ChunkerPlayerGUI_HandleResize(w, h);
}
