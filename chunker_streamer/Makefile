include ../common.mak

#these libraries are required by the chunker_streamer
#CURL config here, it should come from outside the script
LOCAL_CURL ?= -lcurl
#confuse here, GRAPES should already have this in 3DPARTY_LIBS
LOCAL_CONFUSE ?= "/usr/lib/libconfuse.a"

LDLIBS += $(LOCAL_CURL)/lib/libcurl.a -lrt
LDLIBS += $(LOCAL_CONFUSE)/lib/libconfuse.a

all: chunker_streamer

chunker_streamer: ../chunk_transcoding/external_chunk_transcoding.o chunker_metadata.o chunker_streamer.o chunk_pusher.o chunk_pusher_curl.o

clean:
	rm -f chunker_streamer
	rm -f *.o