include ../common.mak

CPPFLAGS += -I$(LOCAL_CONFUSE)/include -I$(LOCAL_CURL)/include
LDLIBS += $(LOCAL_CURL)/lib/libcurl.a -lrt
LDLIBS += $(LOCAL_CONFUSE)/lib/libconfuse.a

CPPFLAGS += $(LOCAL_FFMPEG_CPPFLAGS) $(LOCAL_COMMON_CPPFLAGS)
LDFLAGS += $(LOCAL_FFMPEG_LDFLAGS) $(LOCAL_COMMON_LDFLAGS)
LDLIBS += $(LOCAL_FFMPEG_LDLIBS) $(LOCAL_COMMON_LDLIBS)

LDLIBS += $(DYNAMIC_LDLIBS)

all: chunker_streamer

chunker_streamer: ../chunk_transcoding/external_chunk_transcoding.o chunker_metadata.o chunker_streamer.o chunk_pusher.o chunk_pusher_curl.o

clean:
	rm -f chunker_streamer
	rm -f *.o
