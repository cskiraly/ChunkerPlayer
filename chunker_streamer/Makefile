include ../common.mak

#these libraries are required by the chunker_streamer
#CURL config here
LOCAL_CURL_LDLIBS = -lcurl
#confuse here
LOCAL_CONFUSE_LDLIBS = "/usr/lib/libconfuse.a"

LDLIBS += $(LOCAL_CURL_LDLIBS)
LDLIBS += $(LOCAL_CONFUSE_LDLIBS)

all: chunker_streamer

chunker_streamer: ../chunk_transcoding/external_chunk_transcoding.o chunker_metadata.o chunker_streamer.o chunk_pusher.o chunk_pusher_curl.o

clean:
	rm -f chunker_streamer
	rm -f *.o