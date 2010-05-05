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

GRAPES ?= GRAPES

CPPFLAGS = -I$(GRAPES)/include
CPPFLAGS += -I$(GRAPES)/som


ifdef DEBUGOUT
CPPFLAGS += -DDEBUGOUT
endif

ifdef ML
LDFLAGS += -L$(GRAPES)/som -L$(GRAPES)/ml -L$(LIBEVENT)/lib
LDLIBS += -lsom -lml -lm
CPPFLAGS += -I$(LIBEVENT)/include
ifdef MONL
LDFLAGS += -L$(GRAPES)/dclog -L$(GRAPES)/rep -L$(GRAPES)/monl -L$(GRAPES)/common
LDLIBS += -lstdc++ -lmon -lrep -ldclog -lcommon
CPPFLAGS += -DMONL
ifdef STATIC
CC=g++
endif
endif
LDLIBS += -levent
else
LDFLAGS = -L$(GRAPES)/som/TopologyManager -L$(GRAPES)/som/ChunkTrading -L$(GRAPES)/som/ChunkBuffer  -L$(GRAPES)/som/Scheduler -L$(GRAPES)/som/PeerSet -L$(GRAPES)/som/ChunkIDSet
LDLIBS = -ltrading -lcb -ltopman -lsched -lpeerset -lsignalling
endif

OBJS = streaming.o 
OBJS += net_helpers.o 
OBJS += topology.o
OBJS += chunk_signaling.o
OBJS += chunklock.o
ifdef THREADS
OBJS += loop-mt.o
CFLAGS += -pthread
LDFLAGS += -pthread
else
ifndef HTTP
OBJS += loop.o
else
OBJS += loop-http.o
CFLAGS += -pthread
LDFLAGS += -pthread
endif
endif

ifdef MONL
OBJS += measures.o
endif

#ifndef NHIO
ifndef HTTP
OBJS += input.o
OBJS += output.o
ifndef DUMMY
FFDIR ?= ffmpeg
FFSRC ?= $(FFDIR)
OBJS += Chunkiser/input-stream-avs.o out-stream-avf.o
LDFLAGS += -L$(FFDIR)/libavcodec -L$(FFDIR)/libavformat -L$(FFDIR)/libavutil
LDLIBS += -lavformat -lavcodec -lavutil
LDLIBS += -lm
LDLIBS += $(call ld-option, -lz)
LDLIBS += $(call ld-option, -lbz2)
else
OBJS += input-stream-dummy.o out-stream-dummy.o
endif
else
OBJS += chunk_puller.o
OBJS += chunk_pusher_curl.o
OBJS += input-http.o
OBJS += output-http.o
LOCAL_MHD="../StreamerPlayerChunker/ExternalDependancies/libmicrohttpd"
CPPFLAGS += -I$(LOCAL_MHD) -I$(LOCAL_MHD)/src/daemon -I$(LOCAL_MHD)/src/include
CPPFLAGS += -I../StreamerPlayerChunker
CPPFLAGS += -I$(GRAPES)/ul
LDFLAGS += -L$(LOCAL_MHD)/src/daemon
LDLIBS += -lmicrohttpd
LDLIBS += -lcurl
endif

ifdef DEBUG
CFLAGS += -O0 -g3
CPPFLAGS += -DDEBUG
OBJS += dbg.o
endif

EXECTARGET = offerstreamer
ifdef ML
EXECTARGET := $(EXECTARGET)-ml
endif
ifdef THREADS
EXECTARGET := $(EXECTARGET)-threads
endif

ifdef HTTP
EXECTARGET := $(EXECTARGET)-http
endif

ifdef STATIC
LDFLAGS += -static
EXECTARGET := $(EXECTARGET)-static
endif

ifdef RELEASE
EXECTARGET := $(EXECTARGET)-$(RELEASE)
endif


all: $(EXECTARGET)

ifndef ML
$(EXECTARGET): $(OBJS) $(GRAPES)/som/net_helper.o
else
$(EXECTARGET): $(OBJS) $(GRAPES)/som/net_helper-ml.o
endif

$(EXECTARGET).o: streamer.o
	ln -sf streamer.o $(EXECTARGET).o

out-stream-avf.o Chunkiser/input-stream-avs.o: CPPFLAGS += -I$(FFSRC) 

GRAPES:
	git clone http://www.disi.unitn.it/~kiraly/PublicGits/GRAPES.git
	cd GRAPES; git checkout -b for-streamer-0.8.3 origin/for-streamer-0.8.3

ffmpeg:
	(wget http://ffmpeg.org/releases/ffmpeg-checkout-snapshot.tar.bz2; tar xjf ffmpeg-checkout-snapshot.tar.bz2; mv ffmpeg-checkout-20* ffmpeg) || svn checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	cd ffmpeg; ./configure

prepare: $(GRAPES) $(FFSRC)
ifndef ML
	$(MAKE) -C $(GRAPES)/som -f Makefile.som
else
	cd $(GRAPES); ./autogen.sh; $(MAKE)
endif
	$(MAKE) -C $(FFSRC)

clean:
	rm -f $(EXECTARGET)
	rm -f $(GRAPES)/som/net_helper-ml.o
	rm -f $(GRAPES)/som/net_helper.o
	rm -f *.o
