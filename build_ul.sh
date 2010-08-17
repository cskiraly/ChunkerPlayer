#!/bin/bash
SCRIPT=$(readlink -f $0)
BASE_UL_DIR=`dirname $SCRIPT`
EXTERN_DIR="external_libs"
MAKE="make -j `grep processor /proc/cpuinfo | wc -l`"
cd "$BASE_UL_DIR"

#set some defaults
IO=${IO:-"httpevent"}
MONL=${MONL:-1}
ML=${ML:-1}

#by default build an check
BUILD_ALL=1

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
if [ "$LOCAL_BZ2_A" = "" ]; then
	if [ -f "/usr/lib/libbz2.a" ]; then
		echo "You have file libbz2.a in default system"
		echo "setting path for it to default /usr/lib/libbz2.a"
		LOCAL_BZ2="/usr"
	else
		echo "you seem not to have file libbz2.a. Will be built locally."
		BUILD_BZ2=1
	fi
else
	LOCAL_BZ2=`dirname $LOCAL_BZ2_A`/..
	if [ ! -e "$LOCAL_BZ2/lib/libbz2.a" ]; then
		#wrong location and/or folders structure
		LOCAL_BZ2=""
		BUILD_BZ2=1
	fi
fi

#try to find libz in your system
LOCAL_Z_A=`locate -l 1 libz.a`
if [ "$LOCAL_Z_A" = "" ]; then
	if [ -f "/usr/lib/libz.a" ]; then
		echo "You have file libz.a in default system"
		echo "setting path for it to default /usr/lib/libz.a"
		LOCAL_Z="/usr"
	else
		echo "you seem not to have file libz.a. Will be built locally."
		BUILD_Z=1
	fi
else
	LOCAL_Z=`dirname $LOCAL_Z_A`/..
	if [ ! -e "$LOCAL_Z/lib/libz.a" ]; then
		#wrong location and/or folders structure
		LOCAL_Z=""
		BUILD_Z=1
	fi
fi

#clean all external libraries if CLEAN_EXTERNAL_BUILD=1
if [ -n "$CLEAN_EXTERNAL_BUILD" ]; then
	#remove previuos versions of external libs builds
	rm -r -f $EXTERN_DIR
fi

mkdir $EXTERN_DIR

TEMP_BZ2="$BASE_UL_DIR/$EXTERN_DIR/bzip2/temp_bzip2_install"
if [ -n "$BUILD_BZ2" ]; then
	#we have to build it either because of user will or because does not exist system wide
	if [ -z "$LOCAL_BZ2" ]; then
		#does not exist systemwide
		if [ -n "$BUILD_ALL" -a ! -e "$TEMP_BZ2" ]; then
			#we erase and rebuild since the installation is not there
			cd "$BASE_UL_DIR/$EXTERN_DIR"
			rm -r -f bzip2
			rm -f bzip2-1.0.5.tar.gz
			#get and compile latest bzip2 library
			wget http://www.bzip.org/1.0.5/bzip2-1.0.5.tar.gz; tar xzf bzip2-1.0.5.tar.gz; mv bzip2-1.0.5 bzip2
			cd bzip2
			#make and install in local folder
			$MAKE; $MAKE install PREFIX="$TEMP_BZ2"
		fi
		#previous local install is existing, just set pointer to it
		#or override location if it was set as systemwide
		LOCAL_BZ2=$TEMP_BZ2
	fi
fi

TEMP_Z="$BASE_UL_DIR/$EXTERN_DIR/zlib/temp_zlib_install"
if [ -n "$BUILD_Z" ]; then
	#we have to build it either because of user will or because does not exist system wide
	if [ -z "$LOCAL_Z" ]; then
		#does not exist systemwide
		if [ -n "$BUILD_ALL" -a ! -e "$TEMP_Z" ]; then
			cd "$BASE_UL_DIR/$EXTERN_DIR"
			rm -r -f zlib
			rm -f zlib-1.2.5.tar.gz
			#get and compile latest zlib library
			wget http://zlib.net/zlib-1.2.5.tar.gz; tar xzf zlib-1.2.5.tar.gz; mv zlib-1.2.5 zlib
			cd zlib
			#make and install in local folder
			./configure --prefix="$TEMP_Z"
			$MAKE; $MAKE install
		fi
		#previous local install is existing, just set pointer to it
		#or override location if it was set as systemwide
		LOCAL_Z=$TEMP_Z
	fi
fi

TEMP_X264="$BASE_UL_DIR/$EXTERN_DIR/x264/temp_x264_install"
if [ -n "$BUILD_X264" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_X264" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f x264
	#get and compile latest x264 library
	git clone git://git.videolan.org/x264.git
	cd x264
	#make and install in local folder
	./configure --prefix="$TEMP_X264"
	$MAKE; $MAKE install
fi

TEMP_MP3LAME="$BASE_UL_DIR/$EXTERN_DIR/mp3lame/temp_mp3lame_install"
if [ -n "$BUILD_MP3LAME" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_MP3LAME" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f mp3lame
	#get and compile latest mp3lame library
	rm -f lame-3.98.4.tar.gz
	wget http://sourceforge.net/projects/lame/files/lame/3.98.4/lame-3.98.4.tar.gz/download; tar xzf lame-3.98.4.tar.gz; mv lame-3.98.4 mp3lame;
	cd mp3lame
	#make and install in local folder
	./configure --disable-gtktest --disable-frontend --prefix="$TEMP_MP3LAME"
	$MAKE; $MAKE install
fi

TEMP_FFMPEG="$BASE_UL_DIR/$EXTERN_DIR/ffmpeg/temp_ffmpeg_install"
if [ ! -e "$TEMP_X264/lib/libx264.a" ] || [ ! -e "$TEMP_MP3LAME/lib/libmp3lame.a" ] || [ ! -e "$LOCAL_Z/lib/libz.a" ] || [ ! -e "$LOCAL_BZ2/lib/libbz2.a" ]; then
	echo "Compilation of ffmpeg dependancies failed. Check your internet connection and errors. Exiting."
fi
if [ -n "$BUILD_FFMPEG" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_FFMPEG" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f ffmpeg
	#get and compile ffmpeg with x264 support
	#get latest snapshot
	#rm -f ffmpeg-checkout-snapshot.tar.bz2
	#wget http://ffmpeg.org/releases/ffmpeg-checkout-snapshot.tar.bz2; tar xjf ffmpeg-checkout-snapshot.tar.bz2; mv ffmpeg-checkout-20* ffmpeg
	#do not get latest snapshot
	#get a release tarball instead
	rm -f ffmpeg-0.6.tar.bz2
	wget http://ffmpeg.org/releases/ffmpeg-0.6.tar.bz2; tar xjf ffmpeg-0.6.tar.bz2; mv ffmpeg-0.6 ffmpeg
	#svn -r 21010 checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	#do not get latest snapshot
	#get instead a specific one because allows output video rate resampling
	#svn -r 21010 checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
	cd ffmpeg
	./configure --enable-gpl --enable-nonfree --enable-version3 --enable-libmp3lame --enable-libx264 --enable-pthreads --extra-cflags="-I../x264/temp_x264_install/include -I../mp3lame/temp_mp3lame_install/include -I$LOCAL_BZ2/include  -I$LOCAL_Z/include" --extra-ldflags="-L../x264/temp_x264_install/lib -L../mp3lame/temp_mp3lame_install/lib -L$LOCAL_BZ2/lib -L$LOCAL_Z/lib" --disable-doc --disable-ffplay --disable-ffprobe --disable-ffserver --prefix="$TEMP_FFMPEG"
	$MAKE; $MAKE install
fi

TEMP_MHD="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd/temp_mhd_install"
if [ -n "$BUILD_MHD" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_MHD" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -f libmicrohttpd
	#get and compile libmicrohttpd lib
	svn --non-interactive checkout https://ng.gnunet.org/svn/libmicrohttpd
	cd libmicrohttpd
	autoreconf -fi
	./configure --disable-curl --disable-https --enable-messages --disable-client-side --prefix="$TEMP_MHD"
	$MAKE; $MAKE install
fi

TEMP_SDL="$BASE_UL_DIR/$EXTERN_DIR/sdl/temp_sdl_install"
if [ -n "$BUILD_SDL" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdl
	#get and compile SDL lib
	rm -f SDL-1.2.14.tar.gz
	wget http://www.libsdl.org/release/SDL-1.2.14.tar.gz; tar xzf SDL-1.2.14.tar.gz; mv SDL-1.2.14 sdl
	cd sdl
	#make and install in local folder
	./configure --disable-video-directfb --prefix="$TEMP_SDL"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_SDLIMAGE" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL/lib/libSDL_image.a" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdlimage
	#get and compile SDLIMAGE lib
	rm -f SDL_image-1.2.10.tar.gz
	wget http://www.libsdl.org/projects/SDL_image/release/SDL_image-1.2.10.tar.gz; tar xzf SDL_image-1.2.10.tar.gz; mv SDL_image-1.2.10 sdlimage
	cd sdlimage
	#make and install in local SDL folder
	./configure --prefix="$TEMP_SDL"
	$MAKE; $MAKE install
fi

# SDL_ttf depends on freetype
TEMP_FREETYPE="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install"
if [ -n "$BUILD_FREETYPE" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_FREETYPE" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r freetype
	#get and compile SDLTTF lib
	rm -f freetype-2.1.10.tar.gz
	wget http://mirror.lihnidos.org/GNU/savannah/freetype/freetype-2.1.10.tar.gz; tar xzf freetype-2.1.10.tar.gz; mv freetype-2.1.10 freetype
	cd freetype
	#make and install in local folder
	./configure --prefix="$TEMP_FREETYPE"
	$MAKE; $MAKE install
fi

if [ -n "$BUILD_SDLTTF" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL/lib/libSDL_ttf.a" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r sdlttf
	#get and compile SDLTTF lib
	rm -f SDL_ttf-2.0.10.tar.gz
	wget http://www.libsdl.org/projects/SDL_ttf/release/SDL_ttf-2.0.10.tar.gz; tar xzf SDL_ttf-2.0.10.tar.gz; mv SDL_ttf-2.0.10 sdlttf
	cd sdlttf
	#make and install in local SDL folder
	./configure --with-freetype-prefix="$TEMP_FREETYPE" --with-sdl-prefix="$TEMP_SDL" --prefix="$TEMP_SDL"
	$MAKE; $MAKE install
fi

TEMP_CURL="$BASE_UL_DIR/$EXTERN_DIR/curl/temp_curl_install"
if [ -n "$BUILD_CURL" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_CURL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -r -r curl
	#get and compile CURL lib
	rm -f curl-7.21.0.tar.bz2
	wget http://curl.haxx.se/download/curl-7.21.0.tar.bz2; tar xjf curl-7.21.0.tar.bz2; mv curl-7.21.0 curl
	cd curl
	#make and install in local folder
	./configure --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --without-libssh2 --without-ssl --without-krb4 --enable-static --disable-shared --without-zlib --without-libidn --prefix="$TEMP_CURL"
	$MAKE; $MAKE install
fi

#set needed paths to external libraries
echo "-----"
LOCAL_X264=$TEMP_X264
echo "path for X264 dependancy set to $LOCAL_X264"
LOCAL_MP3LAME=$TEMP_MP3LAME
echo "path for MP3LAME dependancy set to $LOCAL_MP3LAME"
LOCAL_FFMPEG=$TEMP_FFMPEG
echo "path for FFMPEG dependancy set to $LOCAL_FFMPEG"
LOCAL_MHD=$TEMP_MHD
echo "path for LIBMICROHTTPD dependancy set to $LOCAL_MHD"
LOCAL_ABS_SDL=$TEMP_SDL
echo "path for SDL dependancy set to $LOCAL_ABS_SDL"
LOCAL_SDLIMAGE=$TEMP_SDL
echo "path for SDLIMAGE dependancy set to $LOCAL_SDLIMAGE"
LOCAL_SDLTTF=$TEMP_SDL
echo "path for SDLTTF dependancy set to $LOCAL_SDLTTF"
LOCAL_FREETYPE=$TEMP_FREETYPE
echo "path for FREETYPE dependancy set to $LOCAL_FREETYPE"
LOCAL_CURL=$TEMP_CURL
echo "path for CURL dependancy set to $LOCAL_CURL"

echo "path for BZ2 dependancy is set to $LOCAL_BZ2"
echo "path for ZLIB dependancy is set to $LOCAL_Z"
echo "-----"

#compile the UL external applications
#chunker_streamer and chunker_player
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

#CHUNKER_STREAMER
echo "----------------COMPILING CHUNKER STREAMER"
cd "$BASE_UL_DIR"
cd chunker_streamer
$MAKE clean
LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_Z=$LOCAL_Z LOCAL_CONFUSE=$LOCAL_CONFUSE LOCAL_CURL=$LOCAL_CURL $MAKE
echo "----------------FINISHED COMPILING CHUNKER STREAMER"

#CHUNKER_PLAYER
echo "----------------COMPILING CHUNKER PLAYER"
cd "$BASE_UL_DIR"
cd chunker_player
$MAKE clean
LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_Z=$LOCAL_Z LOCAL_CONFUSE=$LOCAL_CONFUSE LOCAL_MHD=$LOCAL_MHD LOCAL_ABS_SDL=$LOCAL_ABS_SDL LOCAL_SDLIMAGE=$LOCAL_SDLIMAGE LOCAL_FREETYPE=$LOCAL_FREETYPE LOCAL_SDLTTF=$LOCAL_SDLTTF $MAKE
echo "----------------FINISHED COMPILING CHUNKER PLAYER"

#compile a version of offerstreamer with UL enabled
#static needs fix??
cd "$BASE_UL_DIR/../OfferStreamer"
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
$MAKE clean
ULPLAYER=$BASE_UL_DIR ULPLAYER_EXTERNAL_LIBS=$EXTERN_DIR LIBEVENT_DIR=$LOCAL_EVENT ML=1 STATIC=1 MONL=$MONL IO=$IO DEBUG= THREADS=$THREADS $MAKE

#check if all is ok
echo "============== RESULTS ==================="

cd "$BASE_UL_DIR/chunker_streamer"
if [ -f "chunker_streamer" ]; then
	echo "chunker_streamer OK"
	C_STREAMER_EXE=`ls -la chunker_streamer`
	echo "$C_STREAMER_EXE"
	#now we want the bare name
	C_STREAMER_EXE=`ls chunker_streamer`
else
	echo "chunker_streamer FAIL"
fi

cd "$BASE_UL_DIR/chunker_player"
if [ -f "chunker_player" ]; then
	echo "chunker_player OK"
	C_PLAYER_EXE=`ls -la chunker_player`
	echo "$C_PLAYER_EXE"
	#now we want the bare name
	C_PLAYER_EXE=`ls chunker_player`
else
	echo "chunker_player FAIL"
fi

cd "$BASE_UL_DIR/../OfferStreamer"
echo "Check if the http binary is among these offerstreamers:"
ls -la offerstreamer* | grep -v ".o$"
echo "Your UL-enabled offerstreamer should be..."
O_TARGET_EXE=`ls offerstreamer* | grep -v ".o$" | grep $IO`
echo "$O_TARGET_EXE"
if [ -f "$O_TARGET_EXE" ]; then
	echo "...and i found it."
else
	echo "...but sadly it appears build FAILED!."
fi

#packaging for distribution
cd "$BASE_UL_DIR/chunker_player"
if [ -f "$BASE_UL_DIR/../OfferStreamer/$O_TARGET_EXE" -a -f "$C_PLAYER_EXE" ]; then
	echo "============== PACKAGING NAPAPLAYER ==================="
	rm -f -r napaplayer
	rm -f -r napaplayer.tar.gz
	mkdir napaplayer
	mkdir napaplayer/icons
	cp icons/* napaplayer/icons/
	cp channels.conf napaplayer/
	cp README napaplayer/
	cp napalogo*.bmp napaplayer/
	cp *.ttf napaplayer/
	cp "$C_PLAYER_EXE" napaplayer/
	cp "$BASE_UL_DIR/../OfferStreamer/$O_TARGET_EXE" napaplayer/
	cd napaplayer/
	ln -s "$O_TARGET_EXE" ./offerstreamer
	cd ..
	tar cvfz napaplayer.tar.gz napaplayer
	if [ -s "napaplayer.tar.gz" ]; then
		#it has size>0. OK!
		echo "Here is your napaplayer tarball. Enjoy!"
		ls -la napaplayer.tar.gz
	else
		echo "Sorry something went wrong during packaging napaplayer."
	fi
else
	echo ""
	echo "Not packaging napaplayer since build failed."
fi

echo "Finished build UL script"

