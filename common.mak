#the following external libraries are needed by both
#the chunker_streamer and the chunker_player
#basically these are ffmpeg-related

#default stuff from here on
ifeq ($(LD),i586-mingw32msvc-ld)
WINDOWS = 1
endif

ifdef WINDOWS
CFLAGS = -g -O0 -Wall
else
CFLAGS = -pthread -g -O0 -Wall
LDFLAGS += -pthread
DYNAMIC_LDLIBS += -lm
endif

NAPA ?= ../../../NAPA-BASELIBS

CFLAGS += -DHAVE_OPENGL -Wl,--warn-common -Wl,--as-needed -Wl,-Bsymbolic
CPPFLAGS += -I../chunk_transcoding -I../ -I$(NAPA)/include

#default fmmpeg here
LOCAL_FFMPEG_CPPFLAGS = -I$(LOCAL_FFMPEG)/include
#LOCAL_FFMPEG_LDFLAGS = -L$(LOCAL_FFMPEG)/lib

ifdef WINDOWS
LOCAL_FFMPEG_LDLIBS = $(LOCAL_FFMPEG)/lib/libavdevice.a $(LOCAL_FFMPEG)/lib/libavformat.a $(LOCAL_FFMPEG)/lib/libavcodec.a $(LOCAL_FFMPEG)/lib/libavutil.a $(LOCAL_FFMPEG)/lib/libswscale.a -lws2_32
else
LOCAL_FFMPEG_LDLIBS = $(LOCAL_FFMPEG)/lib/libavdevice.a $(LOCAL_FFMPEG)/lib/libavformat.a $(LOCAL_FFMPEG)/lib/libavcodec.a $(LOCAL_FFMPEG)/lib/libavutil.a $(LOCAL_FFMPEG)/lib/libswscale.a
endif

LOCAL_COMMON_CPPFLAGS = -I$(LOCAL_X264)/include -I$(LOCAL_BZ2)/include -I$(LOCAL_Z)/include -I$(LOCAL_MP3LAME)/include
#LOCAL_COMMON_LDFLAGS = -L$(LOCAL_X264)/lib -L$(LOCAL_BZ2)/lib -L$(LOCAL_MP3LAME)/lib
LOCAL_COMMON_LDLIBS = $(LOCAL_X264)/lib/libx264.a $(LOCAL_BZ2)/lib/libbz2.a $(LOCAL_Z)/lib/libz.a $(LOCAL_MP3LAME)/lib/libmp3lame.a

cc-option = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null \
              > /dev/null 2>&1; then echo "$(1)"; fi ;)

ld-option = $(shell if echo "int main(){return 0;}" | \
		$(CC) $(LDFLAGS) $(CFLAGS) $(1) -o /dev/null -xc - \
		> /dev/null 2>&1; then echo "$(1)"; fi ;)

LDLIBS += $(call ld-option, -lva)
LDLIBS += $(call ld-option, -lvga)
