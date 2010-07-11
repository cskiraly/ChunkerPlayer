include ../common.mak

#YOUR config starts here
LOCAL_ABS_SDL ?= /mnt/hgfs/triplo/dipartimento/napawine/StreamerPlayerChunker.bak/ExternalDependancies/SDL-1.2.14/my_SDL_deploy
LOCAL_SDLIMAGE ?= /mnt/hgfs/triplo/dipartimento/napawine/StreamerPlayerChunker.bak/ExternalDependancies/SDL-1.2.14/my_SDL_deploy
LOCAL_MHD ?= ../../../../../StreamerPlayerChunker.bak/ExternalDependancies/libmicrohttpd
#YOUR config ends here

#SDL config here
#LOCAL_SDL_CPPFLAGS = $(shell $(LOCAL_SDL)/bin/sdl-config --static-libs --cflags;)
LOCAL_SDL_CPPFLAGS = -Wl,-rpath,$(LOCAL_ABS_SDL)/lib -I$(LOCAL_ABS_SDL)/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
LOCAL_SDL_LDFLAGS = -L$(LOCAL_ABS_SDL)/lib
LOCAL_SDL_LDLIBS = $(LOCAL_ABS_SDL)/lib/libSDL.a -ldl
LOCAL_SDL_CPPFLAGS += -I$(LOCAL_SDLIMAGE)/include/SDL
LOCAL_SDL_LDFLAGS += -L$(LOCAL_SDLIMAGE)/lib
LOCAL_SDL_LDLIBS += $(LOCAL_SDLIMAGE)/lib/libSDL_image.a

#microhttpd config here
LOCAL_MHD_CPPFLAGS = -I$(LOCAL_MHD) -I$(LOCAL_MHD)/src/daemon -I$(LOCAL_MHD)/src/include
LOCAL_MHD_LDFLAGS = -L$(LOCAL_MHD)/src/daemon
#LOCAL_MHD_LDLIBS = -lmicrohttpd
LOCAL_MHD_LDLIBS = $(LOCAL_MHD)/src/daemon/.libs/libmicrohttpd.a

CPPFLAGS += $(LOCAL_SDL_CPPFLAGS) $(LOCAL_MHD_CPPFLAGS)
LDFLAGS += $(LOCAL_SDL_LDFLAGS) $(LOCAL_MHD_LDFLAGS)
LDLIBS += $(LOCAL_SDL_LDLIBS) $(LOCAL_MHD_LDLIBS)

all: chunker_player

chunker_player: ../chunk_transcoding/external_chunk_transcoding.o	chunk_puller.o chunker_player.o

clean:
	rm -f chunker_player
	rm -f *.o
