#the following external libraries are needed by both
#the chunker_streamer and the chunker_player
#basically these are ffmpeg-related

#bz2 lib location should come from outside
LOCAL_BZ2 ?= /usr/lib/libbz2.a
#libmp3lame lib location should come from outside
LOCAL_MP3LAME ?= /usr/lib/libmp3lame.a
#ffmpeg location should come from outside
LOCAL_FFMPEG ?= ../../../../../StreamerPlayerChunker.bak/ExternalDependancies/ffmpeg-export-2010-01-04
#x264 location should come from outside
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
CFLAGS += -Wl,--warn-common -Wl,--as-needed -Wl,-Bsymbolic

LDFLAGS += -L$(LOCAL_AVCODEC) -L$(LOCAL_AVDEVICE) -L$(LOCAL_AVFILTER) -L$(LOCAL_AVFORMAT) -L$(LOCAL_AVUTIL) -L$(LOCAL_POSTPROC) -L$(LOCAL_SWSCALE) -L$(LOCAL_X264)

LDLIBS += $(LOCAL_AVDEVICE)/libavdevice.a $(LOCAL_AVFORMAT)/libavformat.a $(LOCAL_AVCODEC)/libavcodec.a $(LOCAL_AVUTIL)/libavutil.a $(LOCAL_SWSCALE)/libswscale.a $(LOCAL_X264)/libx264.a
LDLIBS += -lz -lm -lpthread -DHAVE_OPENGL
#LDLIBS += -lz -lm -lpthread -ldl -DHAVE_OPENGL
LDLIBS += $(LOCAL_BZ2) $(LOCAL_MP3LAME)
