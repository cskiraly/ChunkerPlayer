#!/bin/bash
SCRIPT=$(readlink -f $0)
BASE_UL_DIR=`dirname $SCRIPT`
EXTERN_DIR="external_libs"
MAKE="make -j 4"
cd "$BASE_UL_DIR"

LIBTOOLIZE_PATH=`whereis -b libtoolize`
if [ "$LIBTOOLIZE_PATH" = "libtoolize:" ]; then
	echo "Can't find libtoolize. Try sudo apt-get install libtool"
	exit
fi

YASM_PATH=`whereis -b yasm`
if [ "$YASM_PATH" = "yasm:" ]; then
	echo "Can't find yasm assembler. Try sudo apt-get install yasm"
	exit
fi

#try to find libbz2 in your system
LOCAL_BZ2_A=`locate -l 1 libbz2.a`
if [ $LOCAL_BZ2_A = "" ]; then
    if [ -f "/usr/lib/libbz2.a" ]; then
	echo "You have file libbz2.a in default system"
	echo "setting path for it to default /usr/lib/libbz2.a"
	LOCAL_BZ2="/usr"
    else
	echo "you seem not to have file libbz2.a. EXITING."
	exit
    fi
else
    LOCAL_BZ2=`dirname $LOCAL_BZ2_A`/..
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
	./configure --prefix="$BASE_UL_DIR/$EXTERN_DIR/x264/temp_x264_install"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_MP3LAME" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f mp3lame
	#get and compile latest mp3lame library
	rm -f lame-3.98.4.tar.gz
	wget http://sourceforge.net/projects/lame/files/lame/3.98.4/lame-3.98.4.tar.gz/download; tar xzf lame-3.98.4.tar.gz; mv lame-3.98.4 mp3lame;
	cd mp3lame
	#make and simulate install in local folder
	./configure --disable-gtktest --disable-frontend --prefix="$BASE_UL_DIR/$EXTERN_DIR/mp3lame/temp_mp3lame_install"
	$MAKE; $MAKE install
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
	./configure --enable-gpl --enable-nonfree --enable-version3 --enable-libmp3lame --enable-libx264 --enable-pthreads --extra-cflags="-I../x264/temp_x264_install/include -I../mp3lame/temp_mp3lame_install/include" --extra-ldflags="-L../x264/temp_x264_install/lib -L../mp3lame/temp_mp3lame_install/lib"
	$MAKE
fi

if [ -n "$BUILD_MHD" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f libmicrohttpd
	#get and compile libmicrohttpd lib
	svn --trust-server-cert --non-interactive checkout https://ng.gnunet.org/svn/libmicrohttpd
	cd libmicrohttpd
	autoreconf -fi
	./configure --disable-curl --disable-https --enable-messages --disable-client-side --prefix="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd/temp_mhd_install"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_SDL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdl
	#get and compile SDL lib
	rm -f SDL-1.2.14.tar.gz
	wget http://www.libsdl.org/release/SDL-1.2.14.tar.gz; tar xzf SDL-1.2.14.tar.gz; mv SDL-1.2.14 sdl
	cd sdl
	#make and simulate install in local folder
	./configure --disable-video-directfb --prefix="$BASE_UL_DIR/$EXTERN_DIR/sdl/temp_sdl_install"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_SDLIMAGE" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdlimage
	#get and compile SDLIMAGE lib
	rm -f SDL_image-1.2.10.tar.gz
	wget http://www.libsdl.org/projects/SDL_image/release/SDL_image-1.2.10.tar.gz; tar xzf SDL_image-1.2.10.tar.gz; mv SDL_image-1.2.10 sdlimage
	cd sdlimage
	#make and simulate install in local folder
	./configure --prefix="$BASE_UL_DIR/$EXTERN_DIR/sdlimage/temp_sdlimage_install"
	$MAKE; $MAKE install
fi

# SDL_ttf depends on freetype
if [ -n "$BUILD_FREETYPE" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r freetype
	#get and compile SDLTTF lib
	rm -f freetype-2.1.10.tar.gz
	wget http://mirror.lihnidos.org/GNU/savannah/freetype/freetype-2.1.10.tar.gz; tar xzf freetype-2.1.10.tar.gz; mv freetype-2.1.10 freetype
	cd freetype
	#make and simulate install in local folder
	./configure --prefix="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_SDLTTF" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdlttf
	#get and compile SDLTTF lib
	rm -f SDL_ttf-2.0.10.tar.gz
	wget http://www.libsdl.org/projects/SDL_ttf/release/SDL_ttf-2.0.10.tar.gz; tar xzf SDL_ttf-2.0.10.tar.gz; mv SDL_ttf-2.0.10 sdlttf
	cd sdlttf
	#make and simulate install in local folder
	./configure --with-freetype-prefix="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install" --with-sdl-prefix="$BASE_UL_DIR/$EXTERN_DIR/sdl/temp_sdl_install" --prefix="$BASE_UL_DIR/$EXTERN_DIR/sdlttf/temp_sdlttf_install"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_CURL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r curl
	#get and compile CURL lib
	rm -f curl-7.21.0.tar.bz2
	wget http://curl.haxx.se/download/curl-7.21.0.tar.bz2; tar xjf curl-7.21.0.tar.bz2; mv curl-7.21.0 curl
	cd curl
	#make and simulate install in local folder
	./configure --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --without-libssh2 --without-ssl --without-krb4 --enable-static --disable-shared --without-zlib --without-libidn --prefix="$BASE_UL_DIR/$EXTERN_DIR/curl/temp_curl_install"
	$MAKE; $MAKE install
fi

#set needed paths to external libraries
echo "-----"
LOCAL_X264="$BASE_UL_DIR/$EXTERN_DIR/x264/temp_x264_install"
echo "path for X264 dependancy set to $LOCAL_X264"
LOCAL_MP3LAME="$BASE_UL_DIR/$EXTERN_DIR/mp3lame/temp_mp3lame_install"
echo "path for MP3LAME dependancy set to $LOCAL_MP3LAME"
LOCAL_FFMPEG="$BASE_UL_DIR/$EXTERN_DIR/ffmpeg"
echo "path for FFMPEG dependancy set to $LOCAL_FFMPEG"
LOCAL_MHD="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd/temp_mhd_install"
echo "path for LIBMICROHTTPD dependancy set to $LOCAL_MHD"
LOCAL_ABS_SDL="$BASE_UL_DIR/$EXTERN_DIR/sdl/temp_sdl_install"
echo "path for SDL dependancy set to $LOCAL_ABS_SDL"
LOCAL_SDLIMAGE="$BASE_UL_DIR/$EXTERN_DIR/sdlimage/temp_sdlimage_install"
echo "path for SDLIMAGE dependancy set to $LOCAL_SDLIMAGE"
LOCAL_SDLTTF="$BASE_UL_DIR/$EXTERN_DIR/sdlttf/temp_sdlttf_install"
echo "path for SDLTTF dependancy set to $LOCAL_SDLTTF"
LOCAL_FREETYPE="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install"
echo "path for FREETYPE dependancy set to $LOCAL_FREETYPE"
LOCAL_CURL="$BASE_UL_DIR/$EXTERN_DIR/curl/temp_curl_install"
echo "path for CURL dependancy set to $LOCAL_CURL"

echo "path for BZ2 dependancy is set to $LOCAL_BZ2"
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
    LOCAL_CONFUSE_A=`locate -l 1 libconfuse.a`
    if [ "$LOCAL_CONFUSE_A" = "" ]; then
        if [ -f "/usr/lib/libconfuse.a" ]; then
	    echo "You have file libconfuse.a in default system"
	    echo "setting path for it to default /usr/lib/libconfuse.a"
	    LOCAL_CONFUSE="/usr"
	else
	    echo "you seem not to have file libconfuse.a. EXITING."
	    exit
	fi
    else
	LOCAL_CONFUSE=`dirname $LOCAL_CONFUSE_A`/..
    fi
fi
$MAKE clean
LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_CURL=$LOCAL_CURL LOCAL_CONFUSE=$LOCAL_CONFUSE $MAKE
echo "----------------FINISHED COMPILING CHUNKER STREAMER"
#CHUNKER_PLAYER
echo "----------------COMPILING CHUNKER PLAYER"
cd "$BASE_UL_DIR"
cd chunker_player
$MAKE clean
LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_MHD=$LOCAL_MHD LOCAL_ABS_SDL=$LOCAL_ABS_SDL LOCAL_SDLIMAGE=$LOCAL_SDLIMAGE LOCAL_FREETYPE=$LOCAL_FREETYPE LOCAL_SDLTTF=$LOCAL_SDLTTF LOCAL_CONFUSE=$LOCAL_CONFUSE $MAKE
echo "----------------FINISHED COMPILING CHUNKER PLAYER"

#compile a version of offerstreamer with UL enabled
#static needs fix??
cd "$BASE_UL_DIR/../OfferStreamer"
$MAKE clean
if [ -d "$BASE_UL_DIR/../../3RDPARTY-LIBS/libevent" ]; then
    LOCAL_EVENT="$BASE_UL_DIR/../../3RDPARTY-LIBS/libevent"
    echo "found LIBEVENT in $LOCAL_EVENT"
else
    LOCAL_EVENT_A=`locate -l 1 libevent.a`
    if [ "$LOCAL_EVENT_A" = "" ]; then
        if [ -f "/usr/lib/libevent.a" ]; then
	    echo "You have file libevent.a in default system"
	    echo "setting path for it to default /usr/lib/libevent.a"
	    LOCAL_EVENT="/usr"
	else
	    echo "you seem not to have file libevent.a. EXITING."
	    exit
	fi
    else
	LOCAL_EVENT=`dirname $LOCAL_EVENT_A`/..
    fi
fi

ULPLAYER=$BASE_UL_DIR ULPLAYER_EXTERNAL_LIBS=$EXTERN_DIR LIBEVENT_DIR=$LOCAL_EVENT ML=1 STATIC= MONL=1 IO=http DEBUG=1 $MAKE
