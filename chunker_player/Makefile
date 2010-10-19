include ../common.mak

ifdef WINDOWS
OUTPUTFILE = chunker_player.exe
else
OUTPUTFILE = chunker_player
endif

#SDL config here
LOCAL_SDL_CPPFLAGS = -I$(LOCAL_ABS_SDL)/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
#LOCAL_SDL_LDFLAGS = -L$(LOCAL_ABS_SDL)/lib -Wl,-framework,Cocoa
ifdef MAC_OS
LOCAL_SDL_LDFLAGS = -L/Users/birke/napawine-software2/trunk/napawine-software3/trunk/Applications/StreamerPlayerChunker/external_libs/sdl/temp_sdl_install_linux/lib /Users/birke/napawine-software2/trunk/napawine-software3/trunk/Applications/StreamerPlayerChunker/external_libs/sdl/temp_sdl_install_linux/lib/libSDLmain.a /Users/birke/napawine-software2/trunk/napawine-software3/trunk/Applications/StreamerPlayerChunker/external_libs/sdl/temp_sdl_install_linux/lib/libSDL.a -Wl,-framework,OpenGL -Wl,-framework,Cocoa -Wl,-framework,ApplicationServices -Wl,-framework,Carbon -Wl,-framework,AudioToolbox -Wl,-framework,AudioUnit -Wl,-framework,IOKit
else
LOCAL_SDL_LDFLAGS = -L$(LOCAL_ABS_SDL)/lib -Wl,-framework,Cocoa 
endif
ifndef WINDOWS
ifdef MAC_OS
#LOCAL_SDL_LDLIBS = -lSDL  -lSDLmain
else
LOCAL_SDL_LDLIBS = $(LOCAL_ABS_SDL)/lib/libSDL.a  -ldl $(LOCAL_ABS_SDL)/lib/libSDLmain.a
endif
endif

LOCAL_SDLIMAGE_CPPFLAGS = -I$(LOCAL_SDLIMAGE)/include/SDL
ifdef WINDOWS
LOCAL_SDLIMAGE_LDLIBS = $(LOCAL_SDLIMAGE)/lib/libSDL_image.a $(LOCAL_LIBPNG)/lib/libpng.a $(LOCAL_LIBINTL)/lib/libintl.*.a $(LOCAL_LIBICONV)/lib/libiconv.a
else
LOCAL_SDLIMAGE_LDLIBS = $(LOCAL_SDLIMAGE)/lib/libSDL_image.a $(LOCAL_LIBPNG)/lib/libpng.a
endif

LOCAL_FREETYPE_CPPFLAGS = -I$(LOCAL_FREETYPE)/include/ -I$(LOCAL_FREETYPE)/include/freetype2
LOCAL_FREETYPE_LDFLAGS = -Wl,-rpath -Wl,$(LOCAL_FREETYPE)/lib
#LOCAL_FREETYPE_LDFLAGS = -Wl,$(LOCAL_FREETYPE)/lib
LOCAL_FREETYPE_LDLIBS = $(LOCAL_FREETYPE)/lib/libfreetype.a

LOCAL_SDLTTF_CPPFLAGS = -I$(LOCAL_SDLTTF)/include/SDL
LOCAL_SDLTTF_LDLIBS = $(LOCAL_SDLTTF)/lib/libSDL_ttf.a

#microhttpd config here
LOCAL_MHD_CPPFLAGS = -I$(LOCAL_MHD)/include
ifdef WINDOWS
LOCAL_MHD_LDLIBS = $(LOCAL_MHD)/lib/libmicrohttpd.a $(LOCAL_PLIBC)/lib/libplibc.dll.a $(LOCAL_PTHREAD)/lib/libpthread.a
else
LOCAL_MHD_LDLIBS = $(LOCAL_MHD)/lib/libmicrohttpd.a
endif

LOCAL_CONFUSE_CPPFLAGS = -I$(LOCAL_CONFUSE)/include
LOCAL_CONFUSE_LDLIBS = $(LOCAL_CONFUSE)/lib/libconfuse.a

#put them in the correct order
CPPFLAGS += $(LOCAL_SDLTTF_CPPFLAGS) $(LOCAL_SDLIMAGE_CPPFLAGS) $(LOCAL_SDL_CPPFLAGS)
CPPFLAGS += $(LOCAL_MHD_CPPFLAGS) $(LOCAL_CONFUSE_CPPFLAGS) $(LOCAL_FREETYPE_CPPFLAGS)
CPPFLAGS += $(LOCAL_FFMPEG_CPPFLAGS) $(LOCAL_COMMON_CPPFLAGS)

LDFLAGS += $(LOCAL_SDLTTF_LDFLAGS) $(LOCAL_SDLIMAGE_LDFLAGS) $(LOCAL_SDL_LDFLAGS)
LDFLAGS += $(LOCAL_MHD_LDFLAGS) $(LOCAL_CONFUSE_LDFLAGS) $(LOCAL_FREETYPE_LDFLAGS)
LDFLAGS += $(LOCAL_FFMPEG_LDFLAGS) $(LOCAL_COMMON_LDFLAGS)

LDLIBS += $(LOCAL_SDLTTF_LDLIBS) $(LOCAL_SDLIMAGE_LDLIBS) $(LOCAL_SDL_LDLIBS)
LDLIBS += $(LOCAL_MHD_LDLIBS) $(LOCAL_CONFUSE_LDLIBS) $(LOCAL_FREETYPE_LDLIBS)
LDLIBS += $(LOCAL_FFMPEG_LDLIBS) $(LOCAL_COMMON_LDLIBS)

ifdef WINDOWS
LDLIBS += $(DYNAMIC_LDLIBS) -lmingw32 $(LOCAL_ABS_SDL)/lib/libSDLmain.a $(LOCAL_ABS_SDL)/lib/libSDL.dll.a -lwinmm -lgdi32
else
LDLIBS += $(DYNAMIC_LDLIBS)
endif

LINKER = $(CC)

all: $(OUTPUTFILE)

$(OUTPUTFILE): ../chunk_transcoding/external_chunk_transcoding.o chunk_puller.o chunker_player.o player_core.o player_gui.o
	$(LINKER) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(OUTPUTFILE)
	rm -f *.o
