#the following external libraries are needed by both
#the chunker_streamer and the chunker_player
#basically these are ffmpeg-related

#bz2 lib location should come from outside, default here
LOCAL_BZ2 ?= /usr/lib
#libmp3lame lib location should come from outside, default here
LOCAL_MP3LAME ?= /usr/lib
#ffmpeg location should come from outside, default here
LOCAL_FFMPEG ?= ../../../../../StreamerPlayerChunker.bak/ExternalDependancies/ffmpeg-export-2010-01-04
#x264 location should come from outside, default here
LOCAL_X264 ?= ../../../../../StreamerPlayerChunker.bak/ExternalDependancies/x264

#default stuff from here on
CFLAGS = -pthread -g -O0 -Wall
CPPFLAGS += -I../chunk_transcoding -I../

#default FFMPEG setup here
LOCAL_AVCODEC = $(LOCAL_FFMPEG)/libavcodec
LOCAL_AVDEVICE = $(LOCAL_FFMPEG)/libavdevice
LOCAL_AVFILTER = $(LOCAL_FFMPEG)/libavfilter
LOCAL_AVFORMAT = $(LOCAL_FFMPEG)/libavformat
LOCAL_AVUTIL = $(LOCAL_FFMPEG)/libavutil
LOCAL_POSTPROC = $(LOCAL_FFMPEG)/libpostproc
LOCAL_SWSCALE = $(LOCAL_FFMPEG)/libswscale

CPPFLAGS += -I$(LOCAL_FFMPEG)
CPPFLAGS += -I$(LOCAL_AVCODEC) -I$(LOCAL_AVDEVICE) -I$(LOCAL_AVFILTER) -I$(LOCAL_AVFORMAT) -I$(LOCAL_AVUTIL) -I$(LOCAL_POSTPROC) -I$(LOCAL_SWSCALE)
CPPFLAGS += -I$(LOCAL_X264)/include -I$(LOCAL_BZ2)/include -I$(LOCAL_MP3LAME)/include
CFLAGS += -Wl,--warn-common -Wl,--as-needed -Wl,-Bsymbolic

LDFLAGS += -L$(LOCAL_AVCODEC) -L$(LOCAL_AVDEVICE) -L$(LOCAL_AVFILTER) -L$(LOCAL_AVFORMAT) -L$(LOCAL_AVUTIL) -L$(LOCAL_POSTPROC) -L$(LOCAL_SWSCALE)
LDFLAGS += -L$(LOCAL_X264)/lib -L$(LOCAL_BZ2)/lib -L$(LOCAL_MP3LAME)/lib

LDLIBS += $(LOCAL_AVDEVICE)/libavdevice.a $(LOCAL_AVFORMAT)/libavformat.a $(LOCAL_AVCODEC)/libavcodec.a $(LOCAL_AVUTIL)/libavutil.a $(LOCAL_SWSCALE)/libswscale.a
LDLIBS += $(LOCAL_X264)/lib/libx264.a $(LOCAL_BZ2)/lib/libbz2.a $(LOCAL_MP3LAME)/lib/libmp3lame.a

LDLIBS += -lz -lm -lpthread -DHAVE_OPENGL
