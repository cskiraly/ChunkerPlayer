include utils.mak

CFLAGS = -g -Wall
CFLAGS += $(call cc-option, -Wdeclaration-after-statement)
CFLAGS += $(call cc-option, -Wno-switch)
CFLAGS += $(call cc-option, -Wdisabled-optimization)
CFLAGS += $(call cc-option, -Wpointer-arith)
CFLAGS += $(call cc-option, -Wredundant-decls)
CFLAGS += $(call cc-option, -Wno-pointer-sign)
CFLAGS += $(call cc-option, -Wcast-qual)
CFLAGS += $(call cc-option, -Wwrite-strings)
CFLAGS += $(call cc-option, -Wtype-limits)
CFLAGS += $(call cc-option, -Wundef)

CFLAGS += $(call cc-option, -funit-at-a-time)

NAPA ?= ../../NAPA-BASELIBS
GRAPES ?= ../../GRAPES
ULPLAYER ?= ../StreamerPlayerChunker
ULPLAYER_EXTERNAL_LIBS ?= external_libs

CPPFLAGS = -I$(NAPA)/include
CPPFLAGS += -I$(GRAPES)/include

ifdef DEBUG
CFLAGS += -O0
CPPFLAGS += -DDEBUG
OBJS += dbg.o
endif


ifdef DEBUGOUT
CPPFLAGS += -DDEBUGOUT
endif

LDFLAGS += -L$(GRAPES)
LDLIBS += -lgrapes
ifdef ML
LDFLAGS += -L$(NAPA)/ml -L$(LIBEVENT_DIR)/lib
LDLIBS += -lml -lm
CPPFLAGS += -I$(NAPA)/ml/include -I$(LIBEVENT_DIR)/include
ifdef MONL
LDFLAGS += -L$(NAPA)/dclog -L$(NAPA)/rep -L$(NAPA)/monl -L$(NAPA)/common
LDLIBS += -lstdc++ -lmon -lrep -ldclog -lcommon
CPPFLAGS += -DMONL
ifdef STATIC
CC=g++
endif
endif
#LDLIBS += -levent -lrt
LDLIBS += $(LIBEVENT_DIR)/lib/libevent.a -lrt
endif

OBJS += streaming.o
ifndef HTTPIO
OBJS += input.o
OBJS += output.o 
else
OBJS += input-http.o
OBJS += output-http.o
endif
OBJS += net_helpers.o 
OBJS += topology.o
OBJS += chunk_signaling.o
OBJS += chunklock.o
OBJS += channel.o
ifdef THREADS
OBJS += loop-mt.o
CFLAGS += -pthread
LDFLAGS += -pthread
else
OBJS += loop.o
endif

ifdef MONL
OBJS += measures-monl.o
else
OBJS += measures.o
endif

ifndef DUMMY
ifndef HTTPIO
OBJS += Chunkiser/input-stream-avs.o out-stream-avf.o
CPPFLAGS += -I$(FFMPEG_DIR)/include
LDFLAGS += -L$(FFMPEG_DIR)/lib
LDLIBS += -lavformat -lavcodec -lavutil
LDLIBS += -lm
LDLIBS += $(call ld-option, -lz)
LDLIBS += $(call ld-option, -lbz2)
else
CPPFLAGS += -DHTTPIO
OBJS += $(ULPLAYER)/chunker_player/chunk_puller.o
OBJS += $(ULPLAYER)/chunker_streamer/chunk_pusher_curl.o
CPPFLAGS += -I$(ULPLAYER) -I$(ULPLAYER)/chunk_transcoding
CFLAGS += -pthread
LDFLAGS += -pthread

LOCAL_MHD=$(ULPLAYER)/$(ULPLAYER_EXTERNAL_LIBS)/libmicrohttpd
CPPFLAGS += -I$(LOCAL_MHD) -I$(LOCAL_MHD)/src/daemon -I$(LOCAL_MHD)/src/include
LDFLAGS += -L$(LOCAL_MHD)/src/daemon
LDLIBS += $(LOCAL_MHD)/src/daemon/.libs/libmicrohttpd.a

LOCAL_CURL=$(ULPLAYER)/$(ULPLAYER_EXTERNAL_LIBS)/curl-7.21.0/temp_curl_install
LDLIBS += $(LOCAL_CURL)/lib/libcurl.a -lrt
endif
else
OBJS += input-stream-dummy.o out-stream-dummy.o
endif

EXECTARGET = offerstreamer
ifdef ML
EXECTARGET := $(EXECTARGET)-ml
endif
ifdef MONL
EXECTARGET := $(EXECTARGET)-monl
endif
ifdef THREADS
EXECTARGET := $(EXECTARGET)-threads
endif
ifdef HTTPIO
EXECTARGET := $(EXECTARGET)-http
endif

ifdef STATIC
LDFLAGS += -static -v
EXECTARGET := $(EXECTARGET)-static
endif

ifdef RELEASE
EXECTARGET := $(EXECTARGET)-$(RELEASE)
endif

all: $(EXECTARGET)

ifndef ML
$(EXECTARGET): $(OBJS) $(GRAPES)/net_helper.o
else
$(EXECTARGET): $(OBJS) $(GRAPES)/net_helper-ml.o
endif

$(EXECTARGET).o: streamer.o
	ln -sf streamer.o $(EXECTARGET).o

out-stream-avf.o Chunkiser/input-stream-avs.o: CPPFLAGS += -I$(FFMPEG_DIR)/include 

GRAPES:
	git clone http://www.disi.unitn.it/~kiraly/PublicGits/GRAPES.git
	cd GRAPES; git checkout -b for-streamer-0.8.3 origin/for-streamer-0.8.3

ffmpeg:
	(wget http://ffmpeg.org/releases/ffmpeg-checkout-snapshot.tar.bz2; tar xjf ffmpeg-checkout-snapshot.tar.bz2; mv ffmpeg-checkout-20* ffmpeg) || svn checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	cd ffmpeg; ./configure

prepare: $(GRAPES) $(FFSRC)
	$(MAKE) -C $(GRAPES) -f Makefile
ifdef ML
	cd $(NAPA); ./autogen.sh; $(MAKE)
endif
	$(MAKE) -C $(FFSRC)

clean:
	rm -f $(EXECTARGET)
	rm -f $(GRAPES)/net_helper-ml.o
	rm -f $(GRAPES)/net_helper.o
	rm -f *.o
	rm -f Chunkiser/*.o
