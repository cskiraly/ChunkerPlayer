#ifndef _CHUNKER_PLAYER_H
#define _CHUNKER_PLAYER_H

#include "player_defines.h"
#include <SDL.h>
#include <SDL_mutex.h>

typedef struct SChannel
{
	char LaunchString[255];
	char Title[255];
	int Width;
	int Height;
	float Ratio;
	int SampleRate;
	short int AudioChannels;
	int Index;
} SChannel;

SDL_mutex *OverlayMutex;
SDL_Overlay *YUVOverlay;
SDL_Rect OverlayRect;
SDL_Surface *MainScreen;
int SilentMode;
int queue_filling_threshold;
char YUVFileName[256];
int SaveYUV;
int quit;
short int QueueFillingMode;
int P2PProcessID;

#ifdef __WIN32__
#define KILL_PROCESS(pid) {char command_name[255]; sprintf(command_name, "taskkill /pid %d /F", pid); system(command_name);}
#define KILLALL(pname) {char command_name[255]; sprintf(command_name, "taskkill /im %s /F", pname); system(command_name);}
#endif
#ifdef __LINUX__
#define KILL_PROCESS(pid) {char command_name[255]; sprintf(command_name, "kill %d", pid); system(command_name);}
#define KILLALL(pname) {char command_name[255]; sprintf(command_name, "killall %s -9", pname); system(command_name);}
#endif
#ifdef __MACOS__
#define KILL_PROCESS(pid) {char command_name[255]; sprintf(command_name, "kill %d", pid); system(command_name);}
#define KILLALL(pname) {char command_name[255]; sprintf(command_name, "killall %s -9", pname); system(command_name);}
#endif




SChannel Channels[MAX_CHANNELS_NUM];
int NChannels;
int SelectedChannel;
char OfferStreamerFilename[255];
int FullscreenMode; // fullscreen vs windowized flag
int window_width, window_height;
int HttpPort;

void ZapDown();
void ZapUp();
int ParseConf();
int SwitchChannel(SChannel* channel);

#endif // _CHUNKER_PLAYER_H
