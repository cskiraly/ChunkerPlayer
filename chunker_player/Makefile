include ../common.mak

ifdef WINDOWS
OUTPUTFILE = chunker_player.exe
#LDLIBS += -lmsvcrt -lwsock32 -lws2_32 -liberty
else
OUTPUTFILE = chunker_player
endif

ifeq ($(IO), httpevent)
#TODO add: or equals httpmhd
CPPFLAGS += -DHTTPIO
endif

ifeq ($(IO), tcp)
CPPFLAGS += -DTCPIO
ifdef WINDOWS
CPPFLAGS += -I$(LOCAL_PTHREAD)/include
LDLIBS += $(LOCAL_PTHREAD)/lib/libpthread.a
endif
endif

#SDL config here
LOCAL_SDL_CPPFLAGS = -I$(LOCAL_ABS_SDL)/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT
ifdef MAC_OS
#force static linking to .a SDL libraries
LOCAL_SDL_LDFLAGS =  -framework OpenGL $(LOCAL_ABS_SDL)/lib/libSDLmain.a $(LOCAL_ABS_SDL)/lib/libSDL.a  -Wl,-framework,Cocoa -Wl,-framework,ApplicationServices -Wl,-framework,Carbon -Wl,-framework,AudioToolbox -Wl,-framework,AudioUnit -Wl,-framework,IOKit
else
LOCAL_SDL_LDFLAGS = -L$(LOCAL_ABS_SDL)/lib  
endif
ifndef WINDOWS
ifdef MAC_OS
LOCAL_SDL_LDLIBS =
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

ifdef PSNR_PUBLICATION
CPPFLAGS += -DPSNR_PUBLICATION
LOCAL_PSNR_CPPFLAGS = -I$(LIBEVENT_DIR)/include -I$(NAPA)/include
LOCAL_PSNR_LDLIBS = $(NAPA)/rep/librep.a $(NAPA)/dclog/libdclog.a $(NAPA)/common/libcommon.a $(NAPA)/ml/libml.a $(LIBEVENT_DIR)/lib/libevent.a
endif

#endif

#put them in the correct order
CPPFLAGS += $(LOCAL_SDLTTF_CPPFLAGS) $(LOCAL_SDLIMAGE_CPPFLAGS) $(LOCAL_SDL_CPPFLAGS)
CPPFLAGS += $(LOCAL_MHD_CPPFLAGS) $(LOCAL_CONFUSE_CPPFLAGS) $(LOCAL_FREETYPE_CPPFLAGS)
CPPFLAGS += $(LOCAL_FFMPEG_CPPFLAGS) $(LOCAL_COMMON_CPPFLAGS)
CPPFLAGS += $(LOCAL_PSNR_CPPFLAGS)

LDFLAGS += $(LOCAL_SDLTTF_LDFLAGS) $(LOCAL_SDLIMAGE_LDFLAGS) $(LOCAL_SDL_LDFLAGS)
LDFLAGS += $(LOCAL_MHD_LDFLAGS) $(LOCAL_CONFUSE_LDFLAGS) $(LOCAL_FREETYPE_LDFLAGS)
LDFLAGS += $(LOCAL_FFMPEG_LDFLAGS) $(LOCAL_COMMON_LDFLAGS)

LDLIBS += $(LOCAL_SDLTTF_LDLIBS) $(LOCAL_SDLIMAGE_LDLIBS) $(LOCAL_SDL_LDLIBS)
LDLIBS += $(LOCAL_MHD_LDLIBS) $(LOCAL_CONFUSE_LDLIBS) $(LOCAL_FREETYPE_LDLIBS)
LDLIBS += $(LOCAL_FFMPEG_LDLIBS) $(LOCAL_COMMON_LDLIBS)
LDLIBS += $(LOCAL_PSNR_LDLIBS)

ifdef LOCAL_CURL
LDLIBS += $(LOCAL_CURL)/lib/libcurl.a
endif

ifdef WINDOWS
LDLIBS += -lmsvcrt -lwsock32 -lws2_32 -liberty
LDLIBS += $(DYNAMIC_LDLIBS) -lmingw32 $(LOCAL_ABS_SDL)/lib/libSDLmain.a $(LOCAL_ABS_SDL)/lib/libSDL.a -lwinmm -lgdi32
else
LDLIBS += $(DYNAMIC_LDLIBS)
LDLIBS += $(call ld-option, -lrt)
endif

LINKER = $(CC)

all: $(OUTPUTFILE)

OBJS = ../chunk_transcoding/external_chunk_transcoding.o
ifeq ($(IO), httpevent)
#TODO add: or equals httpmhd
OBJS += http_chunk_puller.o
endif

ifeq ($(IO), tcp)
OBJS += tcp_chunk_puller.o
endif
OBJS += chunker_player.o QoE_Estimator.o player_stats.o player_core.o player_gui.o

ifdef LOCAL_CURL
CPPFLAGS += -I$(LOCAL_CURL)/include
OBJS += http.o
CPPFLAGS += -DCHANNELS_DOWNLOAD
CFLAGS += -DCURL_STATICLIB	#required by MinGW; based on http://curl.haxx.se/docs/faq.html#Link_errors_when_building_libcur
endif

$(OUTPUTFILE): $(OBJS)
	$(LINKER) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(OUTPUTFILE)
	rm -f *.o

### Automatic generation of headers dependencies ###
%.d: %.c
	$(CC) $(CPPFLAGS) -MM -MF $@ $<

%.o: %.d

-include $(OBJS:.o=.d)
