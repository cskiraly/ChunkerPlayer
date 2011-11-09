include ../common.mak

OBJECTS += dbg.o
OBJECTS += chunker_filtering.o
ifdef USE_AVFILTER
CPPFLAGS += -DUSE_AVFILTER
endif

ifeq ($(IO), httpevent)
CPPFLAGS += -DHTTPIO
OBJECTS += chunk_pusher.o chunk_pusher_curl.o
endif

ifeq ($(IO), tcp)
CPPFLAGS += -DTCPIO
OBJECTS += chunk_pusher.o chunk_pusher_curl.o
endif

ifeq ($(IO), udp)
CPPFLAGS += -DUDPIO
OBJECTS += chunk_pusher_udp.o
endif

CPPFLAGS += -I$(LOCAL_CONFUSE)/include -I$(LOCAL_CURL)/include
LDLIBS += $(LOCAL_CURL)/lib/libcurl.a $(LIBRT)
LDLIBS += $(LOCAL_CONFUSE)/lib/libconfuse.a

CPPFLAGS += $(LOCAL_FFMPEG_CPPFLAGS) $(LOCAL_COMMON_CPPFLAGS)
LDFLAGS += $(LOCAL_FFMPEG_LDFLAGS) $(LOCAL_COMMON_LDFLAGS)
LDLIBS += $(LOCAL_FFMPEG_LDLIBS) $(LOCAL_COMMON_LDLIBS)

LDLIBS += $(call ld-option, -lrt)

LDLIBS += $(DYNAMIC_LDLIBS)

all: chunker_streamer

chunker_streamer: ../chunk_transcoding/external_chunk_transcoding.o chunker_metadata.o chunker_streamer.o $(OBJECTS)

clean:
	rm -f chunker_streamer
	rm -f *.o

### Automatic generation of headers dependencies ###
%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MF $@ $<

%.o: %.d

-include $(OBJS:.o=.d)
