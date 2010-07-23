#ifndef _CHUNKER_PLAYER_H
#define _CHUNKER_PLAYER_H


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <microhttpd.h>
#include "external_chunk_transcoding.h"
#include "frame.h"

#define PLAYER_FAIL_RETURN -1
#define PLAYER_OK_RETURN 0

// #define FULLSCREEN_WIDTH 640
// #define FULLSCREEN_HEIGHT 480

int FullscreenWidth = 0;
int FullscreenHeight = 0;

AVCodecContext  *aCodecCtx;
SDL_Thread *video_thread;
uint8_t *outbuf_audio;

int window_width, window_height;

#define SDL_AUDIO_BUFFER_SIZE 1024

#define MAX_TOLLERANCE 40
#define AUDIO	1
#define VIDEO	2
#define QUEUE_MAX_SIZE 3000

#define FULLSCREEN_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_ICON_FILE "icons/nofullscreen32.png"
#define FULLSCREEN_HOVER_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_HOVER_ICON_FILE "icons/nofullscreen32.png"

#define CHANNEL_UP_ICON_FILE "icons/up_16.png"
#define CHANNEL_DOWN_ICON_FILE "icons/down_16.png"

#define DEFAULT_CHANNEL_EXEC_PATH "../OfferStreamer/"
#define DEFAULT_CHANNEL_EXEC_NAME "offerstreamer-ml-monl-http"
#define DEFAULT_CONF_FILENAME "channels.conf"

#define BUTTONS_LAYER_OFFSET 10
#define BUTTONS_CONTAINER_HEIGHT 40
#define BUTTONS_CONTAINER_WIDTH 100

// typedef enum Status { STOPPED, RUNNING, PAUSED } Status;

#define DEBUG_AUDIO
#define DEBUG_VIDEO
#define DEBUG_QUEUE
#define DEBUG_SOURCE
//#define DEBUG_STATS
//#define DEBUG_AUDIO_BUFFER
#define DEBUG_CHUNKER


short int QueueFillingMode=1;
short int QueueStopped=0;

typedef struct PacketQueue {
	AVPacketList *first_pkt;
	AVPacketList *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	short int queueType;
	int last_frame_extracted; //HINT THIS SHOULD BE MORE THAN 4 BYTES
	int total_lost_frames;
	double density;
} PacketQueue;

typedef struct threadVal {
	int width;
	int height;
	float aspect_ratio;
} ThreadVal;

ThreadVal VideoCallbackThreadParams;

int AudioQueueOffset=0;
PacketQueue audioq;
PacketQueue videoq;
AVPacket AudioPkt, VideoPkt;
int quit = 0;
int SaveYUV=0;
int AVPlaying = 0;
char YUVFileName[256];

int queue_filling_threshold = 0;

SDL_Surface *screen;
SDL_Overlay *yuv_overlay;
SDL_Rect    rect;
SDL_Rect *initRect = NULL;
SDL_AudioSpec spec;

struct SwsContext *img_convert_ctx = NULL;
float ratio;

//SDL_mutex *timing_mutex;

int got_sigint = 0;

long long DeltaTime;
short int FirstTimeAudio=1, FirstTime = 1;

int dimAudioQ;
float deltaAudioQ;
float deltaAudioQError=0;

void SaveFrame(AVFrame *pFrame, int width, int height);

// other GUI staff
SDL_Cursor *init_system_cursor(const char *image[]);
void refresh_fullscreen_button(int hover);
void toggle_fullscreen();
void aspect_ratio_resize(float aspect_ratio, int width, int height, int* out_width, int* out_height);
int fullscreen = 0; // fullscreen vs windowized flag

SDL_Cursor *defaultCursor;
SDL_Cursor *handCursor;
int silentMode = 0;

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

#define FULLSCREEN_BUTTON_INDEX 0
#define NO_FULLSCREEN_BUTTON_INDEX 1
#define CHANNEL_UP_BUTTON_INDEX 2
#define CHANNEL_DOWN_BUTTON_INDEX 3

#define NBUTTONS 4

SButton Buttons[NBUTTONS];

#define MAIN_FONT_FILE "mainfont.ttf"
#define MAIN_FONT_SIZE 18
SDL_Surface *ChannelTitleSurface = NULL;
SDL_Rect ChannelTitleRect;
SDL_Color ChannelTitleColor = { 255, 255, 255 }; 
SDL_Color ChannelTitleBgColor = { 0, 0, 0 };
TTF_Font *MainFont = NULL;

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

typedef struct SChannel
{
	char LaunchString[255];
	char Title[255];
	int Width;
	int Height;
	float Ratio;
	int SampleRate;
	short int AudioChannels;
} SChannel;

SChannel Channels[255];
int NChannels = 0;
int SelectedChannel = -1;
char OfferStreamerPath[255];
char OfferStreamerFilename[255];

int parse_conf();
int switch_channel(SChannel* channel);
int P2PProcessID = -1;

void refresh_channel_buttons(int hover);

void zap_up();
void zap_down();
void redraw_buttons();

SDL_mutex *RedrawMutex;

#endif
