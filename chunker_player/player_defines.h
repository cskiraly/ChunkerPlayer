#ifndef _CHUNKER_PLAYER_DEFINES_H
#define _CHUNKER_PLAYER_DEFINES_H

#include "codec_definitions.h"

#define PLAYER_FAIL_RETURN -1
#define PLAYER_OK_RETURN 0

#define MAX_CHANNELS_NUM 255

#define SDL_AUDIO_BUFFER_SIZE 0 //auto-set by SDL (to 46ms), or set by SDL_AUDIO_SAMPLES

#define MAX_TOLLERANCE 40
#define AUDIO	1
#define VIDEO	2
#define QUEUE_MAX_GROW_FACTOR 1000
#define CHANNEL_SCORE_HISTORY_SIZE 1000

#define FULLSCREEN_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_ICON_FILE "icons/nofullscreen32.png"
#define FULLSCREEN_HOVER_ICON_FILE "icons/fullscreen32.png"
#define NOFULLSCREEN_HOVER_ICON_FILE "icons/nofullscreen32.png"
#define AUDIO_ON_ICON_FILE "icons/audio_on.png"
#define AUDIO_OFF_ICON_FILE "icons/audio_off.png"
#define PSNR_LED_RED_ICON_FILE "icons/red_led.png"
#define PSNR_LED_YELLOW_ICON_FILE "icons/yellow_led.png"
#define PSNR_LED_GREEN_ICON_FILE "icons/green_led.png"

#define CHANNEL_UP_ICON_FILE "icons/up_16.png"
#define CHANNEL_DOWN_ICON_FILE "icons/down_16.png"

#ifdef __WIN32__
#define DEFAULT_CONF_FILENAME "channels.conf"
#elif defined MAC_OS
#define DEFAULT_CONF_FILENAME "../Resources/channels.conf"
#else
#define DEFAULT_CONF_FILENAME "~/.peerstreamer/channels.conf"
#endif
#define DEFAULT_CONF_URI "http://peerstreamer.org/~napawine/release/channels.conf"
#define DEFAULT_PEEREXECNAME_FILENAME "peer_exec_name.conf"
#define DEFAULT_PEER_EXEC_NAME "streamer"

#define DEFAULT_WIDTH 704
#define DEFAULT_HEIGHT 576
#define DEFAULT_RATIO 1.22

#define BUTTONS_LAYER_OFFSET 20
#define BUTTONS_CONTAINER_HEIGHT 40
#define BUTTONS_CONTAINER_WIDTH 100

#define FULLSCREEN_BUTTON_INDEX 0
#define NO_FULLSCREEN_BUTTON_INDEX 1
#define CHANNEL_UP_BUTTON_INDEX 2
#define CHANNEL_DOWN_BUTTON_INDEX 3
#define AUDIO_OFF_BUTTON_INDEX 4
#define AUDIO_ON_BUTTON_INDEX 5
#define PSNR_LED_RED_BUTTON_INDEX 6
#define PSNR_LED_YELLOW_BUTTON_INDEX 7
#define PSNR_LED_GREEN_BUTTON_INDEX 8

#define LED_RED     0
#define LED_YELLOW  1
#define LED_GREEN   2
#define LED_NONE    3

#define LED_THRS_RED    33.0f
#define LED_THRS_YELLOW 36.0f

#define NBUTTONS 9
#define MAIN_FONT_FILE "mainfont.ttf"
#define MAIN_FONT_SIZE 18

#define STATS_FONT_FILE "stats_font.ttf"
#define STATS_FONT_SIZE 16
#define STATS_BOX_HEIGHT 20

#define RESTORE_SCREEN_ON_ZAPPING
#define RESTART_FRAME_NUMBER_THRESHOLD 200

// how long (in seconds) is the statistics buffer
#define STATISTICS_WINDOW_SIZE 30

// milliseconds
#define STATS_THREAD_GRANULARITY 5
#define MAIN_STATS_WINDOW 1000
#define GUI_PRINTSTATS_INTERVAL 500
#define EVAL_QOE_INTERVAL 500

#define MAX_FPS 50
#define QOE_REFERENCE_FRAME_WIDTH 352
#define QOE_REFERENCE_FRAME_HEIGHT 288

//~ #define SAVE_YUV

//#define DEBUG_SYNC
//#define DEBUG_AUDIO
//#define DEBUG_VIDEO
//#define DEBUG_QUEUE
//#define DEBUG_QUEUE_DEEP
//#define DEBUG_SOURCE
//#define DEBUG_STATS
//#define DEBUG_STATS_DEEP
//#define DEBUG_AUDIO_BUFFER
//#define DEBUG_CHUNKER
//#define DEBUG_PSNR
//#define EMULATE_CHUNK_LOSS

#define VIDEO_DEINTERLACE

// seconds
#define PSNR_REPO_UPDATE_INTERVALL   10

#endif // _CHUNKER_PLAYER_DEFINES_H
