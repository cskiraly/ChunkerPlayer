include ../common.mak

#some defaults here
LOCAL_ABS_SDL ?= /mnt/hgfs/triplo/dipartimento/napawine/StreamerPlayerChunker.bak/ExternalDependancies/SDL-1.2.14/my_SDL_deploy
LOCAL_SDLIMAGE ?= /mnt/hgfs/triplo/dipartimento/napawine/StreamerPlayerChunker.bak/ExternalDependancies/SDL-1.2.14/my_SDL_deploy
LOCAL_MHD ?= ../../../../../StreamerPlayerChunker.bak/ExternalDependancies/libmicrohttpd
LOCAL_CONFUSE ?= "/usr"

#SDL config here
#LOCAL_SDL_CPPFLAGS = $(shell $(LOCAL_ABS_SDL)/bin/sdl-config --cflags;)
#LOCAL_SDL_LDFLAGS = $(shell $(LOCAL_ABS_SDL)/bin/sdl-config --static-libs;)
LOCAL_SDL_CPPFLAGS = -I$(LOCAL_ABS_SDL)/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
LOCAL_SDL_LDFLAGS = -L$(LOCAL_ABS_SDL)/lib -Wl,-rpath,$(LOCAL_ABS_SDL)/lib
LOCAL_SDL_LDLIBS = $(LOCAL_ABS_SDL)/lib/libSDL.a -ldl

LOCAL_SDLIMAGE_CPPFLAGS = -I$(LOCAL_SDLIMAGE)/include/SDL
LOCAL_SDLIMAGE_LDFLAGS = -L$(LOCAL_SDLIMAGE)/lib
LOCAL_SDLIMAGE_LDLIBS = $(LOCAL_SDLIMAGE)/lib/libSDL_image.a

#LOCAL_FREETYPE_CPPFLAGS = $(shell $(LOCAL_FREETYPE)/bin/freetype-config --cflags;)
#LOCAL_FREETYPE_LDFLAGS = $(shell $(LOCAL_FREETYPE)/bin/freetype-config --libs;)
LOCAL_FREETYPE_CPPFLAGS = -I$(LOCAL_FREETYPE)/include/ -I$(LOCAL_FREETYPE)/include/freetype2
LOCAL_FREETYPE_LDFLAGS = -L$(LOCAL_FREETYPE)/lib -Wl,--rpath -Wl,$(LOCAL_FREETYPE)/lib
LOCAL_FREETYPE_LDLIBS = $(LOCAL_FREETYPE)/lib/libfreetype.a -lz

LOCAL_SDLTTF_CPPFLAGS = -I$(LOCAL_SDLTTF)/include/SDL
LOCAL_SDLTTF_LDFLAGS = -L$(LOCAL_SDLTTF)/lib
LOCAL_SDLTTF_LDLIBS = $(LOCAL_SDLTTF)/lib/libSDL_ttf.a

#microhttpd config here
#LOCAL_MHD_CPPFLAGS = -I$(LOCAL_MHD) -I$(LOCAL_MHD)/src/daemon -I$(LOCAL_MHD)/src/include
#LOCAL_MHD_LDFLAGS = -L$(LOCAL_MHD)/src/daemon
#LOCAL_MHD_LDLIBS = -lmicrohttpd
#LOCAL_MHD_LDLIBS = $(LOCAL_MHD)/src/daemon/.libs/libmicrohttpd.a
LOCAL_MHD_CPPFLAGS = -I$(LOCAL_MHD)/include
LOCAL_MHD_LDFLAGS = -L$(LOCAL_MHD)/lib
LOCAL_MHD_LDLIBS = $(LOCAL_MHD)/lib/libmicrohttpd.a

LOCAL_CONFUSE_CPPFLAGS = -I$(LOCAL_CONFUSE)/include
LOCAL_CONFUSE_LDLIBS = $(LOCAL_CONFUSE)/lib/libconfuse.a

CPPFLAGS += $(LOCAL_FREETYPE_CPPFLAGS) $(LOCAL_SDLTTF_CPPFLAGS) $(LOCAL_SDLIMAGE_CPPFLAGS) $(LOCAL_SDL_CPPFLAGS) $(LOCAL_MHD_CPPFLAGS)
LDFLAGS += $(LOCAL_FREETYPE_LDFLAGS) $(LOCAL_SDLTTF_LDFLAGS) $(LOCAL_SDLIMAGE_LDFLAGS) $(LOCAL_SDL_LDFLAGS) $(LOCAL_MHD_LDFLAGS) $(LOCAL_CONFUSE_LDFLAGS)
LDLIBS += $(LOCAL_SDLTTF_LDLIBS) $(LOCAL_SDLIMAGE_LDLIBS) $(LOCAL_SDL_LDLIBS) $(LOCAL_MHD_LDLIBS) $(LOCAL_CONFUSE_LDLIBS) $(LOCAL_FREETYPE_LDLIBS)

all: chunker_player

chunker_player: ../chunk_transcoding/external_chunk_transcoding.o chunk_puller.o chunker_player.o player_core.o player_gui.o

clean:
	rm -f chunker_player
	rm -f *.o
