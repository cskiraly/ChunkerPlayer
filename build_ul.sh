#!/bin/bash
SCRIPT=$(readlink -f $0)
BASE_UL_DIR=`dirname $SCRIPT`
EXTERN_DIR="external_libs"
cd "$BASE_UL_DIR"

#try to find libbz2 and mp3lame in your system
LOCAL_BZ2=`locate libbz2.a`
if [ "$LOCAL_BZ2" = "" ]; then
	echo "locate command could not find file libbz2.a"
	echo "setting path for it to default /usr/lib/libbz2.a"
	LOCAL_BZ2="/usr/liblib/bz2.a"
fi
LOCAL_MP3LAME=`locate libmp3lame.a`
if [ "$LOCAL_MP3LAME" = "" ]; then
	echo "locate command could not find file libmp3lame.a"
	echo "setting path for it to default /usr/lib/libmp3lame.a"
	LOCAL_MP3LAME="/usr/liblib/libmp3lame.a"
fi

#clean all external libraries if CLEAN_EXTERNAL_BUILD=1
if [ -n "$CLEAN_EXTERNAL_BUILD" ]; then
	#remove previuos versions of external libs builds
	rm -r -f $EXTERN_DIR
fi

mkdir $EXTERN_DIR

if [ -n "$BUILD_X264" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f x264
	#get and compile latest x264 library
	git clone git://git.videolan.org/x264.git
	cd x264
	#make and simulate install in local folder
	./configure --prefix=./temp_x264_install
	make; make install
fi

if [ -n "$BUILD_FFMPEG" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f ffmpeg
	#get and compile ffmpeg with x264 support
	#get latest snapshot
	rm -f ffmpeg-checkout-snapshot.tar.bz2
	wget http://ffmpeg.org/releases/ffmpeg-checkout-snapshot.tar.bz2; tar xjf ffmpeg-checkout-snapshot.tar.bz2; mv ffmpeg-checkout-20* ffmpeg
	#do not get latest snapshot
	#get instead a specific one because allows output video rate resampling
	#svn -r 21010 checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	cd ffmpeg
	./configure --enable-gpl --enable-nonfree --enable-version3 --enable-libmp3lame --enable-libx264 --enable-pthreads --extra-cflags=-I../x264/temp_x264_install/include --extra-ldflags=-L../x264/temp_x264_install/lib
	make
fi

if [ -n "$BUILD_MHD" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f libmicrohttpd
	#get and compile libmicrohttpd lib
	svn --trust-server-cert --non-interactive checkout https://ng.gnunet.org/svn/libmicrohttpd
	cd libmicrohttpd
	autoreconf -fi
	./configure --disable-curl --disable-https --enable-messages --disable-client-side
	make
fi

if [ -n "$BUILD_SDL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r SDL-1.2.14
	#get and compile SDL lib
	rm -f SDL-1.2.14.tar.gz
	wget http://www.libsdl.org/release/SDL-1.2.14.tar.gz; tar xzf SDL-1.2.14.tar.gz
	cd SDL-1.2.14
	#make and simulate install in local folder
	./configure --disable-video-directfb --prefix="$BASE_UL_DIR/$EXTERN_DIR/SDL-1.2.14/temp_sdl_install"
	make; make install
fi

if [ -n "$BUILD_SDLIMAGE" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r SDL_image-1.2.10
	#get and compile SDLIMAGE lib
	rm -f SDL_image-1.2.10.tar.gz
	wget http://www.libsdl.org/projects/SDL_image/release/SDL_image-1.2.10.tar.gz; tar xzf SDL_image-1.2.10.tar.gz
	cd SDL_image-1.2.10
	#make and simulate install in local folder
	./configure --prefix="$BASE_UL_DIR/$EXTERN_DIR/SDL_image-1.2.10/temp_sdlimage_install"
	make; make install
fi

if [ -n "$BUILD_CURL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r curl-7.21.0
	#get and compile CURL lib
	rm -f curl-7.21.0.tar.bz2
	wget http://curl.haxx.se/download/curl-7.21.0.tar.bz2; tar xjf curl-7.21.0.tar.bz2
	cd curl-7.21.0
	#make and simulate install in local folder
./configure --disable-ldap --without-libssh2 --without-ssl --without-krb4 --enable-static --disable-shared --without-zlib --without-libidn --prefix="$BASE_UL_DIR/$EXTERN_DIR/curl-7.21.0/temp_curl_install"
	make; make install
fi

#set needed paths to external libraries
echo "-----"
LOCAL_X264="$BASE_UL_DIR/$EXTERN_DIR/x264"
echo "path for X264 dependancy set to $LOCAL_X264"
LOCAL_FFMPEG="$BASE_UL_DIR/$EXTERN_DIR/ffmpeg"
echo "path for FFMPEG dependancy set to $LOCAL_FFMPEG"
LOCAL_MHD="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd"
echo "path for LIBMICROHTTPD dependancy set to $LOCAL_MHD"
LOCAL_ABS_SDL="$BASE_UL_DIR/$EXTERN_DIR/SDL-1.2.14/temp_sdl_install"
echo "path for SDL dependancy set to $LOCAL_ABS_SDL"
LOCAL_SDLIMAGE="$BASE_UL_DIR/$EXTERN_DIR/SDL_image-1.2.10/temp_sdlimage_install"
echo "path for SDLIMAGE dependancy set to $LOCAL_SDLIMAGE"
LOCAL_CURL="$BASE_UL_DIR/$EXTERN_DIR/curl-7.21.0/temp_curl_install"
echo "path for CURL dependancy set to $LOCAL_CURL"

echo "path for BZ2 dependancy is set to $LOCAL_BZ2"
echo "path for MP3LAME dependancy is set to $LOCAL_MP3LAME"
echo "-----"

#compile the UL external applications
#CHUNKER_STREAMER
echo "----------------COMPILING CHUNKER STREAMER"
cd "$BASE_UL_DIR"
cd chunker_streamer
if [ -d "$BASE_UL_DIR/../../3RDPARTY-LIBS/libconfuse" ]; then
	LOCAL_CONFUSE="$BASE_UL_DIR/../../3RDPARTY-LIBS/libconfuse"
	echo "found LIBCONFUSE in $LOCAL_CONFUSE"
else
	LOCAL_CONFUSE=`locate libconfuse.a`
	if [ "$LOCAL_CONFUSE" = "" ]; then
		echo "you dont have libconfue built in 3DPRTY LIBS of GRAPES nor in system"
		echo "setting path for it to default /usr/lib/libconfuse.a"
		LOCAL_CONFUSE="/usr/lib/libconfuse.a"
	fi
fi
make clean
LOCAL_X264=$LOCAL_X264 LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_CURL=$LOCAL_CURL LOCAL_CONFUSE=$LOCAL_CONFUSE make
echo "----------------FINISHED COMPILING CHUNKER STREAMER"
#CHUNKER_PLAYER
echo "----------------COMPILING CHUNKER PLAYER"
cd "$BASE_UL_DIR"
cd chunker_player
make clean
LOCAL_X264=$LOCAL_X264 LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_MHD=$LOCAL_MHD LOCAL_ABS_SDL=$LOCAL_ABS_SDL LOCAL_SDLIMAGE=$LOCAL_SDLIMAGE make
echo "----------------FINISHED COMPILING CHUNKER PLAYER"

#compile a version of offerstreamer with UL enabled
#static needs fix??
cd "$BASE_UL_DIR/../OfferStreamer"
make clean
ULPLAYER=$BASE_UL_DIR ULPLAYER_EXTERNAL_LIBS=$EXTERN_DIR LIBEVENT_DIR="$BASE_UL_DIR/../../3RDPARTY-LIBS/libevent" ML=1 STATIC= MONL=1 HTTPIO=1 DEBUG=1 make
