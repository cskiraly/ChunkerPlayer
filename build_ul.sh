#!/bin/bash
SCRIPT=$0
BASE_UL_DIR=`pwd`
EXTERN_DIR="external_libs"
MAKE="make -j 4"
cd "$BASE_UL_DIR"

VERSION_LIBMICROHTTPD=21651

REBUILD=
[ "$1" == "-r" ] && REBUILD=1

#check the architecture
if [ "$OSTYPE" == "linux-gnu" ]; then
   # do something Linux-y
   echo "Building on Linux"
elif [ "$OSTYPE" == "darwin10.0" -o "$OSTYPE" == "darwin11" ]; then
   # do something OSX-y
   echo "Building on OSX"
   MAC_OS=1
fi

which svn >/dev/null || { echo "CANNOT build UL Applications: svn missing. Please install subversion, then retry!"; exit 1; }
#which libtoolize >/dev/null || { echo "CANNOT build UL Applications: libtool missing. Please install libtool, then retry!"; exit 1; }
which yasm >/dev/null || { echo "CANNOT build UL Applications: yasm missing. Please install yasm, then retry!"; exit 1; }
if [ ! -n "MAC_OS" ]; then
  #we don't have git on osx - so we avoid using it
  which git >/dev/null || { echo "CANNOT build UL Applications: git missing. Please install git, then retry!"; exit 1; }
fi
WGET_OR_CURL=`which wget`
[ -n "$WGET_OR_CURL" ] || { WGET_OR_CURL=`which curl` ; WGET_OR_CURLOPT="-L -O"; }
[ -n "$WGET_OR_CURL" ] || { echo "please install wget or curl!"; exit 1; }

#set some defaults
IO=${IO:-"tcp"}
MONL=${MONL:-1}
ML=${ML:-1}

if [ "$HOSTARCH" = "mingw32" ]; then
	MINGW=1
fi

#by default build an check
BUILD_ALL=1

LIBTOOLIZE_PATH=`whereis libtoolize`
if [ "$LIBTOOLIZE_PATH" = "libtoolize:" ]; then
	echo "Can't find libtoolize. Try sudo apt-get install libtool"
	exit
fi

YASM_PATH=`whereis yasm`
if [ "$YASM_PATH" = "yasm:" ]; then
	echo "Can't find yasm assembler. Try sudo apt-get install yasm"
	exit
fi

mkdir -p $EXTERN_DIR

if [ -n "$MINGW" ]; then
	TEMP_BZ2="$BASE_UL_DIR/$EXTERN_DIR/bzip2_mingw"
	if [ ! -e "$TEMP_BZ2/lib/libbz2.a" ]; then
		#we erase and rebuild since the installation is not there
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -r -f bzip2_mingw
		#get the latest bzip2 library
		$WGET_OR_CURL $WGET_OR_CURLOPT http://switch.dl.sourceforge.net/project/gnuwin32/bzip2/1.0.5/bzip2-1.0.5-bin.zip; unzip bzip2-1.0.5-bin.zip -d bzip2_mingw
		$WGET_OR_CURL $WGET_OR_CURLOPT http://sunet.dl.sourceforge.net/project/gnuwin32/bzip2/1.0.5/bzip2-1.0.5-lib.zip; unzip -o bzip2-1.0.5-lib.zip -d bzip2_mingw
		rm -f bzip2-1.0.5-lib.zip; rm -f bzip2-1.0.5-bin.zip;
	fi
	LOCAL_BZ2=$TEMP_BZ2
else
	#try to find libbz2 in your system
	LOCAL_BZ2_A=/usr/lib/libbz2.a
	if [ ! -e $LOCAL_BZ2_A ]; then
		[ X$LOCAL_BZ2_A != X ] && LOCAL_BZ2=`dirname $LOCAL_BZ2_A`/..
		if [ ! -e "$LOCAL_BZ2/lib/libbz2.a" ]; then
			#wrong location and/or folders structure
			echo "you seem not to have file libbz2.a or the location is non-standard.Will be built locally."
			LOCAL_BZ2=""
			BUILD_BZ2=1
		fi
	else
		echo "You have file libbz2.a in default system"
		echo "setting path for it to default /usr/lib/libbz2.a"
		LOCAL_BZ2="/usr"
	fi
fi

if [ -n "$MINGW" ]; then
	TEMP_Z="$BASE_UL_DIR/$EXTERN_DIR/zlib_mingw"
	if [ ! -e "$TEMP_Z/lib/libz.a" ]; then
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -r -f zlib_mingw;
		#get the latest zlib libraries
		$WGET_OR_CURL $WGET_OR_CURLOPT http://sunet.dl.sourceforge.net/project/gnuwin32/zlib/1.2.3/zlib-1.2.3-bin.zip; unzip zlib-1.2.3-bin.zip -d zlib_mingw
		$WGET_OR_CURL $WGET_OR_CURLOPT http://kent.dl.sourceforge.net/project/gnuwin32/zlib/1.2.3/zlib-1.2.3-lib.zip; unzip -o zlib-1.2.3-lib.zip -d zlib_mingw
		rm -f zlib-1.2.3-bin.zip; rm -f zlib-1.2.3-lib.zip;
	fi
	LOCAL_Z=$TEMP_Z
else
	LOCAL_Z="$BASE_UL_DIR/$EXTERN_DIR/zlib/temp_zlib_install_linux"
	if [ -f "$LOCAL_Z/lib/libz.a" ]; then
		echo "libz.a found in $LOCAL_Z/lib"
	else
		#force building locally
		#does not exist systemwide
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -r -f zlib
		#get and compile latest zlib library
		$WGET_OR_CURL $WGET_OR_CURLOPT http://zlib.net/fossils/zlib-1.2.5.tar.gz; tar xzf zlib-1.2.5.tar.gz; mv zlib-1.2.5 zlib; rm -f zlib-1.2.5.tar.gz
		cd zlib
		#make and install in local folder
		./configure --prefix="$LOCAL_Z" --libdir="$LOCAL_Z"/lib
		$MAKE && $MAKE install || exit 1
	fi

	#clean all external libraries if CLEAN_EXTERNAL_BUILD=1
	if [ -n "$CLEAN_EXTERNAL_BUILD" ]; then
		#remove previuos versions of external libs builds
		rm -r -f $EXTERN_DIR
	fi
fi
LIBSDLIMAGE_FLAGS="$LIBSDLIMAGE_FLAGS -I$LOCAL_Z/include"
LIBSDLIMAGE_LDFLAGS="$LIBSDLIMAGE_LDFLAGS -L$LOCAL_Z/lib"

if [ ! -n "$MINGW" ]; then
	TEMP_BZ2="$BASE_UL_DIR/$EXTERN_DIR/bzip2/temp_bzip2_install_linux"
	if [ -n "$BUILD_BZ2" ]; then
		#we have to build it either because of user will or because does not exist system wide
		if [ -z "$LOCAL_BZ2" ]; then
			#does not exist systemwide
			if [ -n "$BUILD_ALL" -a ! -e "$TEMP_BZ2" ]; then
				#we erase and rebuild since the installation is not there
				cd "$BASE_UL_DIR/$EXTERN_DIR"
				rm -r -f bzip2
				#get and compile latest bzip2 library
				$WGET_OR_CURL $WGET_OR_CURLOPT http://www.bzip.org/1.0.5/bzip2-1.0.5.tar.gz; tar xzf bzip2-1.0.5.tar.gz; mv bzip2-1.0.5 bzip2; rm -f bzip2-1.0.5.tar.gz
				cd bzip2
				#make and install in local folder
				$MAKE && $MAKE install PREFIX="$TEMP_BZ2" || exit 1 
			fi
			#previous local install is existing, just set pointer to it
			#or override location if it was set as systemwide
			LOCAL_BZ2=$TEMP_BZ2
		fi
	fi
fi


if [ -n "$MINGW" ]; then
	# plibc
	LOCAL_PLIBC="$BASE_UL_DIR/$EXTERN_DIR/plibc"
	if [ -f "$BASE_UL_DIR/$EXTERN_DIR/plibc/lib/libplibc.dll.a" ]; then
		echo "You have file libplibc.dll.a in default system: $BASE_UL_DIR/$EXTERN_DIR/plibc/lib/libplibc.dll.a"
	else
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -fR plibc
		$WGET_OR_CURL $WGET_OR_CURLOPT http://sourceforge.net/projects/plibc/files/plibc/0.1.5/plibc-0.1.5.zip
		unzip plibc-0.1.5.zip -d plibc;
		rm -f plibc/lib/*.la
		rm -f plibc-0.1.5.zip
	fi
	LIBMICROHHTPD_FLAGS="-I$BASE_UL_DIR/$EXTERN_DIR/plibc/include"
	LIBMICROHHTPD_LDFLAGS="-L$BASE_UL_DIR/$EXTERN_DIR/plibc/lib"

	# libiconv
	LOCAL_LIBICONV="$BASE_UL_DIR/$EXTERN_DIR/libiconv/temp_libiconv_install_mingw"
	if [ -f "$LOCAL_LIBICONV/lib/libiconv.a" ]; then
		echo "libiconv.a found in $LOCAL_LIBICONV/lib"
	else
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -fR libiconv
		
		#download binaries
		#~ wget http://kent.dl.sourceforge.net/project/gnuwin32/libintl/0.14.4/libintl-0.14.4-dep.zip; unzip libintl-0.14.4-dep.zip -d libiconv
		#~ mkdir -p $LOCAL_LIBICONV/lib/
		#~ mv libiconv/*.dll $LOCAL_LIBICONV/lib/
		#~ rm -f libintl-0.14.4-dep.zip
		
		#build from sources
		$WGET_OR_CURL $WGET_OR_CURLOPT http://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.13.tar.gz
		tar zxvf libiconv-1.13.tar.gz; mv libiconv-1.13 libiconv; rm -f libiconv-1.13.tar.gz; cd libiconv
		./configure --enable-static ${HOSTARCH:+--host=$HOSTARCH} --prefix=$LOCAL_LIBICONV --libdir=$LOCAL_LIBICONV/lib
		make
		make install
	fi
	LIBSDLIMAGE_FLAGS="$LIBSDLIMAGE_FLAGS -I$LOCAL_LIBICONV/include"
	#~ LIBSDLIMAGE_LDFLAGS="$LIBSDLIMAGE_LDFLAGS -L$LOCAL_LIBICONV/lib"

	LOCAL_LIBINTL="$BASE_UL_DIR/$EXTERN_DIR/libintl/temp_libintl_install_mingw"
	if [ -f "$LOCAL_LIBINTL/lib/libintl.dll.a" ] || [ -f "$LOCAL_LIBINTL/lib/libintl.a" ]; then
		echo "libintl libraries found in $LOCAL_LIBINTL/lib"
	else
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		#download binaries
		rm -fR libintl
		$WGET_OR_CURL $WGET_OR_CURLOPT http://garr.dl.sourceforge.net/project/gnuwin32/libintl/0.14.4/libintl-0.14.4-lib.zip; unzip -o libintl-0.14.4-lib.zip -d libintl
		$WGET_OR_CURL $WGET_OR_CURLOPT http://switch.dl.sourceforge.net/project/gnuwin32/libintl/0.14.4/libintl-0.14.4-bin.zip; unzip -o libintl-0.14.4-bin.zip -d libintl
		rm -f libintl-0.14.4-bin.zip; rm -f libintl-0.14.4-lib.zip
		cd libintl
		mkdir -p $LOCAL_LIBINTL;
		mv bin $LOCAL_LIBINTL/; mv lib $LOCAL_LIBINTL/; mv include $LOCAL_LIBINTL/
		mv man $LOCAL_LIBINTL/; mv share $LOCAL_LIBINTL/
		
		# build from gettext sources
		#~ rm -fR gettext
		#~ rm -fR libintl
		#~ wget http://ftp.gnu.org/pub/gnu/gettext/gettext-0.18.1.1.tar.gz
		#~ tar zxvf gettext-0.18.1.1.tar.gz; mv gettext-0.18.1.1 gettext; cd gettext
		#~ ./configure ${HOSTARCH:+--host=$HOSTARCH} --with-libiconv-prefix=$LOCAL_LIBICONV
		#~ cd gettext-runtime/intl
		#~ make
		#~ mkdir -p $LOCAL_LIBINTL/bin; mkdir -p $LOCAL_LIBINTL/include; mkdir -p $LOCAL_LIBINTL/lib
		#~ cp ./.libs/*a $LOCAL_LIBINTL/lib
		#~ cp ./.libs/*.dll $LOCAL_LIBINTL/bin
		#~ cp *.h $LOCAL_LIBINTL/include
		#~ cd ../../../
		#~ rm -f gettext-0.18.1.1.tar.gz
		#~ rm -fR gettext
	fi
	LIBSDLIMAGE_FLAGS="$LIBSDLIMAGE_FLAGS -I$LOCAL_LIBINTL"
	LIBSDLIMAGE_LDFLAGS="$LIBSDLIMAGE_LDFLAGS -L$LOCAL_LIBINTL/lib"

	# check for mingw pthread libs
	LOCAL_PTHREAD="$BASE_UL_DIR/$EXTERN_DIR/pthreads"
	if [ -f "$BASE_UL_DIR/$EXTERN_DIR/pthreads/lib/libpthread.a" ]; then
		echo "libpthread.a in $LOCAL_PTHREAD/lib/"
	else
		cd "$BASE_UL_DIR/$EXTERN_DIR"
		rm -fR pthreads
		$WGET_OR_CURL $WGET_OR_CURLOPT http://www.mirrorservice.org/sites/sourceware.org/pub/pthreads-win32/pthreads-w32-2-8-0-release.tar.gz
		tar zxvf pthreads-w32-2-8-0-release.tar.gz; mv pthreads-w32-2-8-0-release pthreads; rm -f pthreads-w32-2-8-0-release.tar.gz;
		cd pthreads
		mkdir -p ./{include,lib,bin}
		make CROSS=${CROSSPREFIX} GC-inlined
		mv libpthreadGC2.a ./lib
		mv *.h ./include
		mv pthreadGC2.dll ./bin
		ln -s "$BASE_UL_DIR/$EXTERN_DIR/pthreads/lib/libpthreadGC2.a" "$BASE_UL_DIR/$EXTERN_DIR/pthreads/lib/libpthread.a"
	fi
	LIBMICROHHTPD_FLAGS="$LIBMICROHHTPD_FLAGS -I$BASE_UL_DIR/$EXTERN_DIR/pthreads/include"
	LIBMICROHHTPD_LDFLAGS="$LIBMICROHHTPD_LDFLAGS -L$BASE_UL_DIR/$EXTERN_DIR/pthreads/lib"
fi


echo "building libpng"
if [ -n "$MINGW" ]; then
	LOCAL_LIBPNG="$BASE_UL_DIR/$EXTERN_DIR/libpng/temp_libpng_install_mingw"
else
	LOCAL_LIBPNG="$BASE_UL_DIR/$EXTERN_DIR/libpng/temp_libpng_install_linux"
fi
if [ -f "$LOCAL_LIBPNG/lib/libpng.a" ]; then
	echo "libpng.a found in $LOCAL_LIBPNG/lib"
else
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	rm -fR libpng
	$WGET_OR_CURL $WGET_OR_CURLOPT http://downloads.sourceforge.net/project/libpng/libpng14/older-releases/1.4.4/libpng-1.4.4.tar.gz
	tar zxvf libpng-1.4.4.tar.gz; rm -f libpng-1.4.4.tar.gz
	mv libpng-1.4.4 libpng; cd libpng;
	CFLAGS=-I$LOCAL_Z/include CPPFLAGS=-I$LOCAL_Z/include LDFLAGS=-L$LOCAL_Z/lib sh configure  ${HOSTARCH:+--host=$HOSTARCH} --prefix=$LOCAL_LIBPNG --libdir=$LOCAL_LIBPNG/lib
	make
	make install
fi
LIBSDLIMAGE_FLAGS="$LIBSDLIMAGE_FLAGS -I$LOCAL_LIBPNG/include"
LIBSDLIMAGE_LDFLAGS="$LIBSDLIMAGE_LDFLAGS -L$LOCAL_LIBPNG/lib"


echo "building x264"
if [ -n "$LOCAL_X264" ]; then
	TEMP_X264=$LOCAL_X264
else
if [ -n "$MINGW" ]; then
	#TEMP_X264="$BASE_UL_DIR/$EXTERN_DIR/x264/temp_x264_install_mingw"	#would be needed only for the encoding by the chunker, so we disable it
	echo "mingw: libx264 not required for player"
else
	TEMP_X264="$BASE_UL_DIR/$EXTERN_DIR/x264/temp_x264_install_linux"
if [ -n "$BUILD_X264" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_X264" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	if [ -e "x264" ]; then
		cd x264
		make clean
	else
		#get and compile latest x264 library
		if [ -n "$MAC_OS" ]; then
		   $WGET_OR_CURL $WGET_OR_CURLOPT ftp://ftp.videolan.org/pub/videolan/x264/snapshots/x264-snapshot-20110115-2245.tar.bz2
		   tar yvfx x264-snapshot-20110115-2245.tar.bz2
		   rm -f x264-snapshot-20110115-2245.tar.bz2
		   mv x264-snapshot-20110115-2245/ x264
		else
		   git clone git://git.videolan.org/x264.git
		fi
		cd x264
	fi	

	#make and install in local folder
	./configure --prefix="$TEMP_X264" --libdir="$TEMP_X264"/lib ${HOSTARCH:+--host=$HOSTARCH}
	$MAKE && $MAKE install || exit 1
fi
fi
fi

echo "building mp3lame"
if [ -n "$LOCAL_MP3LAME" ]; then
	TEMP_MP3LAME=$LOCAL_MP3LAME
else
if [ -n "$MINGW" ]; then
	#TEMP_MP3LAME="$BASE_UL_DIR/$EXTERN_DIR/mp3lame/temp_mp3lame_install_mingw"	#would be needed only for the encoding by the chunker, so we disable it
	echo "mingw: libmp3lame not required for player"
else
	TEMP_MP3LAME="$BASE_UL_DIR/$EXTERN_DIR/mp3lame/temp_mp3lame_install_linux"
if [ -n "$BUILD_MP3LAME" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_MP3LAME" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	
	if [ -e "mp3lame" ]; then
		cd mp3lame
		make clean
	else
		#get and compile latest mp3lame library
		rm -f lame-3.98.4.tar.gz
		$WGET_OR_CURL $WGET_OR_CURLOPT http://sourceforge.net/projects/lame/files/lame/3.98.4/lame-3.98.4.tar.gz/download; tar xzf download; rm -f download; mv lame-3.98.4 mp3lame;
		cd mp3lame
	fi	

	#make and install in local folder
	./configure --disable-gtktest --disable-frontend --prefix="$TEMP_MP3LAME" --libdir="$TEMP_MP3LAME"/lib ${HOSTARCH:+--host=$HOSTARCH}
	$MAKE && $MAKE install || exit 1
fi
fi
fi

echo "building ffmpeg"
if [ -n "$LOCAL_FFMPEG" ]; then
	TEMP_FFMPEG=$LOCAL_FFMPEG
else
if [ -n "$MINGW" ]; then
	TEMP_FFMPEG="$BASE_UL_DIR/$EXTERN_DIR/ffmpeg/temp_ffmpeg_install_mingw"
else
	TEMP_FFMPEG="$BASE_UL_DIR/$EXTERN_DIR/ffmpeg/temp_ffmpeg_install_linux"
fi
if [ ! -e "$TEMP_X264/lib/libx264.a" ] || [ ! -e "$TEMP_MP3LAME/lib/libmp3lame.a" ] || [ ! -e "$LOCAL_Z/lib/libz.a" ] || [ ! -e "$LOCAL_BZ2/lib/libbz2.a" ]; then
	echo "Compilation of some ffmpeg dependancies failed. Check your internet connection and errors."
fi
if [ -e "$TEMP_X264/lib/libx264.a" ]; then
	FFMPEG_CONFIG+=" --enable-libx264"
	FFMPEG_EXTRA_CFLAGS+=" -I$TEMP_X264/include"
fi
if [ -e "$TEMP_MP3LAME/lib/libmp3lame.a" ]; then
	FFMPEG_CONFIG+=" --enable-libmp3lame"
	FFMPEG_EXTRA_CFLAGS+=" -I$TEMP_MP3LAME/include"
fi
if [ -n "$BUILD_FFMPEG" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_FFMPEG" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	
	if [ -e "ffmpeg" ]; then
		cd ffmpeg
		make clean
	else
		#get and compile ffmpeg with x264 support
		#get latest snapshot
		#rm -f ffmpeg-checkout-snapshot.tar.bz2
		#wget http://ffmpeg.org/releases/ffmpeg-checkout-snapshot.tar.bz2; tar xjf ffmpeg-checkout-snapshot.tar.bz2; mv ffmpeg-checkout-20* ffmpeg
		#do not get latest snapshot
		#get a release tarball instead
		rm -f ffmpeg-0.6.tar.bz2
		$WGET_OR_CURL $WGET_OR_CURLOPT http://ffmpeg.org/releases/ffmpeg-0.6.tar.bz2; tar xjf ffmpeg-0.6.tar.bz2; rm -f ffmpeg-0.6.tar.bz2; mv ffmpeg-0.6 ffmpeg
		#svn -r 21010 checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
		#do not get latest snapshot
		#get instead a specific one because allows output video rate resampling
		#svn -r 21010 checkout svn://svn.ffmpeg.org/ffmpeg/trunk ffmpeg
		cd ffmpeg
	fi	

	if [ -n "$MINGW" ]; then
		HOSTARCH_OLDSTYLE=${HOSTARCH:+--arch=i586 --enable-cross-compile --cross-prefix=i586-mingw32msvc- --target-os=$HOSTARCH --disable-sse}
	
		#configure bugfix
		sed -i -e 's/^SDL_CONFIG=/[ -z "$SDL_CONFIG" ] \&\& SDL_CONFIG=/g' ./configure;
		sed -i -e 's/check_cflags \+-Werror=missing-prototypes/#\0/g' configure;
	
		./configure $HOSTARCH_OLDSTYLE --enable-gpl --enable-nonfree --enable-version3 --disable-pthreads $FFMPEG_CONFIG --extra-cflags="$FFMPEG_EXTRA_CFLAGS -I$LOCAL_BZ2/include  -I$LOCAL_Z/include" --extra-ldflags="-L$TEMP_X264/lib -L$TEMP_MP3LAME/lib -L$LOCAL_BZ2/lib -L$LOCAL_Z/lib" --disable-doc --disable-ffplay --disable-ffprobe --disable-ffserver --prefix="$TEMP_FFMPEG" --libdir="$TEMP_FFMPEG"/lib
	else
		./configure --enable-gpl --enable-nonfree --enable-version3 --enable-pthreads  $FFMPEG_CONFIG --extra-cflags="$FFMPEG_EXTRA_CFLAGS -I$LOCAL_BZ2/include  -I$LOCAL_Z/include" --extra-ldflags="-L$TEMP_X264/lib -L$TEMP_MP3LAME/lib -L$LOCAL_BZ2/lib -L$LOCAL_Z/lib" --disable-doc --disable-ffplay --disable-ffprobe --disable-ffserver --prefix="$TEMP_FFMPEG" --libdir="$TEMP_FFMPEG"/lib
	fi
	$MAKE && $MAKE install || exit 1
fi
fi

echo "building libmicrohttpd"
if [ -n "$MINGW" ]; then
	TEMP_MHD="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd/temp_mhd_install_mingw"
else
	TEMP_MHD="$BASE_UL_DIR/$EXTERN_DIR/libmicrohttpd/temp_mhd_install_linux"
fi
if [ -n "$BUILD_MHD" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_MHD" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	
	if [ -e "libmicrohttpd" ]; then
		cd libmicrohttpd
		make clean
	else
		#get and compile libmicrohttpd lib
		if [ X`svn --version|head -1|cut -c14-16` = X1.6 ]; then
			svn --non-interactive --trust-server-cert checkout https://gnunet.org/svn/libmicrohttpd -r$VERSION_LIBMICROHTTPD
		else
			svn --non-interactive checkout https://gnunet.org/svn/libmicrohttpd -r$VERSION_LIBMICROHTTPD
		fi
		cd libmicrohttpd || { echo "CANNOT download libmicrohttpd"; exit 1; }
 
	fi
	
	autoreconf -fi
	if [ -n "$MINGW" ]; then
		CFLAGS="$CFLAGS $LIBMICROHHTPD_FLAGS" CPPFLAGS="$CPPFLAGS $LIBMICROHHTPD_FLAGS" LDFLAGS="$LDFLAGS $LIBMICROHHTPD_LDFLAGS" ./configure ${HOSTARCH:+--host=$HOSTARCH} --disable-curl --disable-https --enable-messages --disable-client-side --prefix="$TEMP_MHD" --libdir="$TEMP_MHD"/lib
	else
		./configure --disable-curl --disable-https --enable-messages --disable-client-side --prefix="$TEMP_MHD" --libdir="$TEMP_MHD"/lib
	fi
	$MAKE && $MAKE install || exit 1
fi


echo "building SDL"
if [ -n "$MINGW" ]; then
	TEMP_SDL="$BASE_UL_DIR/$EXTERN_DIR/sdl_mingw/temp_sdl_install"
else
	TEMP_SDL="$BASE_UL_DIR/$EXTERN_DIR/sdl/temp_sdl_install_linux"
fi
if [ -n "$BUILD_SDL" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"

	if [ -n "$MINGW" ]; then
#		if [ ! -e "sdl_mingw" ]; then
#			# use binaries
#			$WGET_OR_CURL $WGET_OR_CURLOPT http://www.libsdl.org/release/SDL-devel-1.2.14-mingw32.tar.gz; tar zxvf SDL-devel-1.2.14-mingw32.tar.gz; mv SDL-1.2.14 sdl_mingw;
#			rm -f SDL-devel-1.2.14-mingw32.tar.gz;
#			cd sdl_mingw
#			mkdir temp_sdl_install
#			mv bin $TEMP_SDL/; mv lib $TEMP_SDL/; mv include $TEMP_SDL/; mv share $TEMP_SDL/

			# build from sources
			$WGET_OR_CURL $WGET_OR_CURLOPT http://www.libsdl.org/release/SDL-1.2.15.tar.gz; tar xzf SDL-1.2.15.tar.gz; rm -f SDL-1.2.15.tar.gz; mv SDL-1.2.15 sdl_mingw
			cd sdl_mingw
			#make and install in local folder
			./configure ${HOSTARCH:+--host=$HOSTARCH} --disable-video-directfb --disable-shared --prefix="$TEMP_SDL" --libdir="$TEMP_SDL"/lib
			$MAKE && $MAKE install || exit 1
#		fi
	else
		if [ -e "sdl" ]; then
			cd sdl
			make clean
		else
			#get and compile SDL lib
			#$WGET_OR_CURL $WGET_OR_CURLOPT http://www.libsdl.org/release/SDL-1.2.15.tar.gz; tar xzf SDL-1.2.15.tar.gz; mv SDL-1.2.15 sdl; rm -f SDL-1.2.15.tar.gz
			$WGET_OR_CURL $WGET_OR_CURLOPT http://peerstreamer.org/files/release/SDL-1.2.15.tar.gz; tar xzf SDL-1.2.15.tar.gz; mv SDL-1.2.15 sdl; rm -f SDL-1.2.15.tar.gz
			cd sdl
		fi
		#make and install in local folder
		./autogen.sh
		./configure --disable-video-directfb --prefix="$TEMP_SDL" --libdir="$TEMP_SDL"/lib
		$MAKE && $MAKE install || exit 1
	fi
fi

echo "building SDLimage"
if [ -n "$BUILD_SDLIMAGE" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL/lib/libSDL_image.a" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	
	if [ -e "sdlimage" ]; then
		cd sdlimage
		make clean
	else
		#get and compile SDLIMAGE lib
		$WGET_OR_CURL $WGET_OR_CURLOPT http://www.libsdl.org/projects/SDL_image/release/SDL_image-1.2.10.tar.gz; tar xzf SDL_image-1.2.10.tar.gz; mv SDL_image-1.2.10 sdlimage
		rm -f SDL_image-1.2.10.tar.gz
		cd sdlimage
	fi
	
	#make and install in local SDL folder
        ./autogen.sh
	echo "./configure LIBPNG_CFLAGS=\"-I$LOCAL_LIBPNG/include\" LIBPNG_LIBS=\"-L$LOCAL_LIBPNG/lib\" CFLAGS=\"$CFLAGS $LIBSDLIMAGE_FLAGS -static\" CPPFLAGS=\"$CPPFLAGS $LIBSDLIMAGE_FLAGS -static\" LDFLAGS=\"$LDFLAGS $LIBSDLIMAGE_LDFLAGS -static\" ${HOSTARCH:+--host=$HOSTARCH} --prefix=\"$TEMP_SDL\" --libdir=\"$TEMP_SDL\"/lib --with-sdl-prefix=\"$TEMP_SDL\" --disable-png-shared"

	echo "./configure CFLAGS=\"$CFLAGS -I$LOCAL_LIBPNG/include\" CPPFLAGS=\"$CPPFLAGS -I$LOCAL_LIBPNG/include\" LDFLAGS=\"$LDFLAGS -L$LOCAL_LIBPNG/lib\" ${HOSTARCH:+--host=$HOSTARCH} --prefix=\"$TEMP_SDL\" --libdir=\"$TEMP_SDL\"/lib --with-sdl-prefix=\"$TEMP_SDL\" --disable-png-shared"

	./configure CFLAGS="$CFLAGS $LIBSDLIMAGE_FLAGS -I$LOCAL_LIBPNG/include" CPPFLAGS="$CPPFLAGS -I$LOCAL_LIBPNG/include" LDFLAGS="$LDFLAGS $LIBSDLIMAGE_LDFLAGS -L$LOCAL_LIBPNG/lib" ${HOSTARCH:+--host=$HOSTARCH} --prefix="$TEMP_SDL" --libdir="$TEMP_SDL"/lib --with-sdl-prefix="$TEMP_SDL" --disable-png-shared  --disable-shared --disable-sdltest

	$MAKE && $MAKE install || exit 1
fi

echo "building freetype"
# SDL_ttf depends on freetype
if [ -n "$MINGW" ]; then
	TEMP_FREETYPE="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install_mingw"
else
	TEMP_FREETYPE="$BASE_UL_DIR/$EXTERN_DIR/freetype/temp_freetype_install_linux"
fi
if [ -n "$BUILD_FREETYPE" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_FREETYPE" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	if [ -e "freetype" ]; then
		cd freetype
		make clean
	else
		#get and compile SDLTTF lib
		rm -f freetype-2.1.10.tar.gz
		$WGET_OR_CURL $WGET_OR_CURLOPT http://mirror.lihnidos.org/GNU/savannah/freetype/freetype-2.1.10.tar.gz; tar xzf freetype-2.1.10.tar.gz; rm -f freetype-2.1.10.tar.gz; mv freetype-2.1.10 freetype
		cd freetype
	fi

	#make and install in local folder
	./configure ${HOSTARCH:+--host=$HOSTARCH} --prefix="$TEMP_FREETYPE" --libdir="$TEMP_FREETYPE"/lib
	$MAKE && $MAKE install || exit 1
fi

echo "building libSDL-ttf"
if [ -n "$BUILD_SDLTTF" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_SDL/lib/libSDL_ttf.a" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	
	if [ -e "sdlttf" ]; then
		cd sdlttf
		make clean
	else
		#get and compile SDLTTF lib
		rm -f SDL_ttf-2.0.10.tar.gz
		$WGET_OR_CURL $WGET_OR_CURLOPT http://www.libsdl.org/projects/SDL_ttf/release/SDL_ttf-2.0.10.tar.gz; tar xzf SDL_ttf-2.0.10.tar.gz; rm -f SDL_ttf-2.0.10.tar.gz; mv SDL_ttf-2.0.10 sdlttf
		cd sdlttf
	fi

	#make and install in local SDL folder
	./configure ${HOSTARCH:+--host=$HOSTARCH} --with-freetype-prefix="$TEMP_FREETYPE" --with-sdl-prefix="$TEMP_SDL" --prefix="$TEMP_SDL" --libdir="$TEMP_SDL"/lib --disable-shared --disable-sdltest
	$MAKE && $MAKE install || exit 1
fi

echo "building curl"
if [ -n "$MINGW" ]; then
	TEMP_CURL="$BASE_UL_DIR/$EXTERN_DIR/curl/temp_curl_install_mingw"
else
	TEMP_CURL="$BASE_UL_DIR/$EXTERN_DIR/curl/temp_curl_install_linux"
fi
if [ -n "$BUILD_CURL" ] || [ -n "$BUILD_ALL" -a ! -e "$TEMP_CURL" ]; then
	cd "$BASE_UL_DIR/$EXTERN_DIR"
	if [ -e "curl" ]; then
		cd curl
		make clean
	else
		#get and compile CURL lib
		rm -f curl-7.21.0.tar.bz2
		$WGET_OR_CURL $WGET_OR_CURLOPT http://curl.haxx.se/download/curl-7.21.0.tar.bz2; tar xjf curl-7.21.0.tar.bz2; rm -f curl-7.21.0.tar.bz2; mv curl-7.21.0 curl
		cd curl
	fi

	#make and install in local folder
	./configure ${HOSTARCH:+--host=$HOSTARCH} --without-librtmp --disable-ftp --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --without-libssh2 --without-ssl --without-krb4 --enable-static --disable-shared --without-zlib --without-libidn --prefix="$TEMP_CURL" --libdir="$TEMP_CURL"/lib
	$MAKE && $MAKE install || exit 1
fi

echo "looking for libevent"
if [ -d "$BASE_UL_DIR/../../3RDPARTY-LIBS/libevent" ]; then
	LOCAL_EVENT="$BASE_UL_DIR/../../3RDPARTY-LIBS/libevent"
	echo "found LIBEVENT in $LOCAL_EVENT"
else 
  if [ -d "$BASE_UL_DIR/../THIRDPARTY-LIBS/NAPA-BASELIBS/3RDPARTY-LIBS/libevent" ]; then
	LOCAL_EVENT="$BASE_UL_DIR/../THIRDPARTY-LIBS/NAPA-BASELIBS/3RDPARTY-LIBS/libevent"
	echo "found LIBEVENT in $LOCAL_EVENT"
  else 
    if [ -d "$BASE_UL_DIR/external_libs/libevent" ]; then
	LOCAL_EVENT="$BASE_UL_DIR/external_libs/libevent"
	echo "found LIBEVENT in $LOCAL_EVENT"
    else
	if [ "$LOCAL_EVENT_A" = "" ]; then
		if [ -f "/usr/lib/libevent.a" ]; then
			echo "You have file libevent.a in default system"
			echo "setting path for it to default /usr/lib/libevent.a"
			LOCAL_EVENT="/usr"
		else
			echo "you seem not to have file libevent.a."
			echo "you will not be able to build the libevent based version"
			echo "(don't worry, the default version will still compile)."
		fi
	else
		LOCAL_EVENT=`dirname $LOCAL_EVENT_A`/..
	fi
    fi
  fi
fi

echo "looking for libconfuse"
if [ -d "$BASE_UL_DIR/../../3RDPARTY-LIBS/libconfuse" ]; then
	LOCAL_CONFUSE="$BASE_UL_DIR/../../3RDPARTY-LIBS/libconfuse"
	echo "found LIBCONFUSE in $LOCAL_CONFUSE"
else 
  if [ -d "$BASE_UL_DIR/../THIRDPARTY-LIBS/NAPA-BASELIBS/3RDPARTY-LIBS/libconfuse" ]; then
	LOCAL_CONFUSE="$BASE_UL_DIR/../THIRDPARTY-LIBS/NAPA-BASELIBS/3RDPARTY-LIBS/libconfuse"
	echo "found LIBCONFUSE in $LOCAL_CONFUSE"
  else 
    if [ -d "$BASE_UL_DIR/external_libs/libconfuse" ]; then
	LOCAL_CONFUSE="$BASE_UL_DIR/external_libs/libconfuse"
	echo "found LIBCONFUSE in $LOCAL_CONFUSE"
    else
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
  fi
fi

#set needed paths to external libraries
echo "-----"
NAPA="$BASE_UL_DIR/../THIRDPARTY-LIBS/NAPA-BASELIBS"
echo "path for NAPA base folder set to $NAPA"
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

echo "path for LIBEVENT dependancy is set to $LOCAL_EVENT"
echo "path for LIBCONFUSE dependancy is set to $LOCAL_CONFUSE"
echo "-----"

#compile the UL external applications
#chunker_streamer and chunker_player

#CHUNKER_STREAMER

# streamer is not supported yet on windows
echo "----------------COMPILING CHUNKER STREAMER"
cd "$BASE_UL_DIR"
cd chunker_streamer
[ -n "$REBUILD" ] && $MAKE clean
if [ -z "$MAC_OS" ]; then
	USE_AVFILTER=1
fi
LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_Z=$LOCAL_Z LOCAL_CONFUSE=$LOCAL_CONFUSE LOCAL_CURL=$LOCAL_CURL IO=$IO MAC_OS=$MAC_OS USE_AVFILTER=$USE_AVFILTER $MAKE || { echo "Failed to build CHUNKER STREAMER"; exit 1; }
echo "----------------FINISHED COMPILING CHUNKER STREAMER"

#CHUNKER_PLAYER
echo "----------------COMPILING CHUNKER PLAYER"
cd "$BASE_UL_DIR"
cd chunker_player
[ -n "$REBUILD" ] && $MAKE clean
NAPA=$NAPA LIBEVENT_DIR=$LOCAL_EVENT LOCAL_CURL=$LOCAL_CURL LOCAL_PTHREAD=$LOCAL_PTHREAD LOCAL_LIBPNG=$LOCAL_LIBPNG LOCAL_LIBICONV=$LOCAL_LIBICONV LOCAL_LIBINTL=$LOCAL_LIBINTL LOCAL_PLIBC=$LOCAL_PLIBC LOCAL_X264=$LOCAL_X264 LOCAL_MP3LAME=$LOCAL_MP3LAME LOCAL_FFMPEG=$LOCAL_FFMPEG LOCAL_BZ2=$LOCAL_BZ2 LOCAL_Z=$LOCAL_Z LOCAL_CONFUSE=$LOCAL_CONFUSE LOCAL_MHD=$LOCAL_MHD LOCAL_ABS_SDL=$LOCAL_ABS_SDL LOCAL_SDLIMAGE=$LOCAL_SDLIMAGE LOCAL_FREETYPE=$LOCAL_FREETYPE LOCAL_SDLTTF=$LOCAL_SDLTTF IO=$IO MAC_OS=$MAC_OS  $MAKE || { echo "Failed to build CHUNKER PLAYER" ; exit 1; }
echo "----------------FINISHED COMPILING CHUNKER PLAYER"
exit

#compile a version of winestreamer with UL enabled
#static needs fix??
cd "$BASE_UL_DIR/../Streamer"

$MAKE IO=$IO clean
#LOCAL_MHD=$LOCAL_MHD LOCAL_CURL=$LOCAL_CURL ULPLAYER=$BASE_UL_DIR ULPLAYER_EXTERNAL_LIBS=$EXTERN_DIR LIBEVENT_DIR=$LOCAL_EVENT ML=$ML STATIC=$STATIC MONL=$MONL IO=$IO DEBUG=$DEBUG THREADS=$THREADS MAC_OS=$MAC_OS SVNVERSION=$SVNVERSION $MAKE || { echo "Failed to build WineStreamer" ; exit 1; }
LOCAL_CURL=$LOCAL_CURL LIBEVENT_DIR=$LOCAL_EVENT ML=$ML STATIC=$STATIC MONL=$MONL ALTO=$ALTO IO=$IO DEBUG=$DEBUG THREADS=$THREADS MAC_OS=$MAC_OS SVNVERSION=$SVNVERSION $MAKE || { echo "Failed to build WineStreamer" ; exit 1; }

#check if all is ok
echo "============== RESULTS ==================="

if [ ! -n "$MINGW" ]; then
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
fi

cd "$BASE_UL_DIR/chunker_player"
if [ -n "$MINGW" ]; then
	SUFFIX=".exe"
	SCRIPT_SUFFIX=".bat"
else
	SUFFIX=""
	SCRIPT_SUFFIX=".sh"
fi
if [ -f "chunker_player$SUFFIX" ]; then
	echo "chunker_player OK"
	C_PLAYER_EXE=`ls -la chunker_player$SUFFIX`
	echo "$C_PLAYER_EXE"
	#now we want the bare name
	C_PLAYER_EXE=`ls chunker_player$SUFFIX`
else
	echo "chunker_player FAIL"
fi

cd "$BASE_UL_DIR/../Streamer"
echo "Check if the http binary is among these winestreamers:"
ls -la winestreamer* | grep -v ".o$"
echo "Your UL-enabled winestreamer has these flags:"
if [ -n "$ALTO" ]; then
	GREP_ALTO_EXPR="-alto"
	GREP_ALTO_FLAGS=""
	echo "ALTO: enabled"
else
	GREP_ALTO_EXPR="-alto"
	GREP_ALTO_FLAGS="-v"
	echo "ALTO: disabled"
fi
if [ "$STATIC" = "1" ]; then
	GREP_STATIC_EXPR="-halfstatic"
	GREP_STATIC_FLAGS=""
	echo "STATIC: halfstatic"
elif [ "$STATIC" = "2" ]; then
	GREP_STATIC_EXPR="-static"
	GREP_STATIC_FLAGS=""
	echo "STATIC: static"
else
	GREP_STATIC_EXPR="static"
	GREP_STATIC_FLAGS="-v"
	echo "STATIC: dynamic"
fi
echo "IO: $IO"
IO="-"$IO
echo "Hence your UL-enabled winestreamer should be..."
O_TARGET_EXE=`ls winestreamer* | grep -v ".o$" | grep $GREP_STATIC_FLAGS -e "$GREP_STATIC_EXPR" | grep $GREP_ALTO_FLAGS -e "$GREP_ALTO_EXPR" | grep -e "$IO"`
#O_TARGET_EXE=`ls winestreamer* | grep -v ".o$" | grep -e "$IO"`
echo "$O_TARGET_EXE"
if [ -f "$O_TARGET_EXE" ]; then
	echo "...and i found it."
else
	echo "...but sadly it appears build FAILED!."
fi

#creating conf file with executable name inside
cd "$BASE_UL_DIR/chunker_player"
echo "$O_TARGET_EXE" > peer_exec_name.conf

#packaging for distribution
cd "$BASE_UL_DIR/chunker_player"
if [ -f "$BASE_UL_DIR/../Streamer/$O_TARGET_EXE" -a -f "$C_PLAYER_EXE" ]; then
	echo "============== PACKAGING NAPAPLAYER ==================="
	rm -f -r napaplayer
	mkdir napaplayer
	mkdir napaplayer/icons
	cp icons/* napaplayer/icons/
	cp channels.conf napaplayer/
	cp peer_exec_name.conf napaplayer/
	cp runme$SCRIPT_SUFFIX napaplayer/
	cp README napaplayer/
	cp napalogo*.bmp napaplayer/
	cp *.ttf napaplayer/
	cp "$C_PLAYER_EXE" napaplayer/
	if [ -n "$MINGW" ]; then
		cp "$BASE_UL_DIR/../Streamer/$O_TARGET_EXE" napaplayer/
	else
		cp "$BASE_UL_DIR/../Streamer/$O_TARGET_EXE" napaplayer/
	fi
	if [ -n "$MINGW" ]; then
		cp "$LOCAL_LIBICONV/bin/libiconv-2.dll" napaplayer/libiconv2.dll
		if [ -f "$LOCAL_LIBINTL/bin/libintl-8.dll" ]; then
			cp "$LOCAL_LIBINTL/bin/libintl-8.dll" napaplayer/
		else
			cp "$LOCAL_LIBINTL/bin/libintl3.dll" napaplayer/libintl-8.dll
		fi
		cp "$LOCAL_PLIBC/bin/libplibc-1.dll" napaplayer/
		cp "$LOCAL_PTHREAD/bin/pthreadGC2.dll" napaplayer/
		cp "$LOCAL_ABS_SDL/bin/SDL.dll" napaplayer/
		rm -f -r napaplayer.zip
		zip -r napaplayer napaplayer
		if [ -s "napaplayer.zip" ]; then
			#it has size>0. OK!
			echo "Here it is your napaplayer package. Enjoy!"
			ls -la napaplayer.zip
		else
			echo "Sorry something went wrong during packaging napaplayer."
		fi
	else
		rm -f -r napaplayer.tar.gz
		tar cvfz napaplayer.tar.gz napaplayer
		if [ -s "napaplayer.tar.gz" ]; then
			#it has size>0. OK!
			echo "Here it is your napaplayer tarball. Enjoy!"
			ls -la napaplayer.tar.gz
		else
			echo "Sorry something went wrong during packaging napaplayer."
		fi
	fi
else
	echo ""
	echo "Not packaging napaplayer since build failed."
fi

echo "Finished build UL script"

