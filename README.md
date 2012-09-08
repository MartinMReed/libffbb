Camera API add-on with FFmpeg support.

Support forum post:  
http://supportforums.blackberry.com/t5/Native-Development/Camera-API-NV12-frame-to-AVFrame-FFmpeg/td-p/1842089

# FFmpeg Requirement

FFmpeg is a required library for libffbb. FFmpeg carries the LGPL-v2.1 license, unless H.264 is enabled, in which case it uses GPL.

Included with libffbb is the FFmpeg source and prebuilt library files. Listed below are some quick instructions for rebuilding FFmpeg for both armle-v7 and x86.

## Download FFmpeg

	$ # checkout using git
	$ git clone git://source.ffmpeg.org/ffmpeg.git ffmpeg

OR

Visit the website at: [http://ffmpeg.org](http://ffmpeg.org)

## Building FFmpeg

	$ # build (shared) libs for QNX armle-v7
	$ # to enable h.264 support add `--enable-gpl --enable-libx264 --extra-cflags=-I/workspace/libffbb/libx264/include --extra-ldflags=-L/workspace/libffbb/libx264/lib/armle-v7`
	$ ./configure --enable-cross-compile --cross-prefix=arm-unknown-nto-qnx8.0.0eabi- --arch=armv7 --disable-debug --enable-optimizations --enable-asm --disable-static --enable-shared --target-os=qnx --disable-ffplay --disable-ffserver --disable-ffprobe --prefix=`pwd`/target  
	$ make install

	$ # build (static) libs for QNX x86
	$ # to enable h.264 support add `--enable-gpl --enable-libx264 --extra-cflags=-I/workspace/libffbb/libx264/include --extra-ldflags=-L/workspace/libffbb/libx264/lib/x86`
	$ ./configure --enable-cross-compile --cross-prefix=i486-pc-nto-qnx8.0.0- --arch=x86 --disable-debug --enable-static --disable-shared --target-os=qnx --disable-ffplay --disable-ffserver --disable-ffprobe --disable-yasm --prefix=`pwd`/target  
	$ make install
	
	$ # build (static) libs for x86_86
	$ # to enable h.264 support add `--enable-gpl --enable-libx264 --extra-cflags=-I/workspace/libffbb/libx264/include --extra-ldflags=-L/workspace/libffbb/libx264/lib/x86`
	$ ./configure --arch=x86_64 --disable-debug --enable-static --disable-shared --disable-ffplay --disable-ffserver --disable-ffprobe --disable-yasm --prefix=`pwd`/target
	$ make install

## Compiling against FFmpeg

	device {
		ARCH = armle-v7
	}
	
	simulator {
		ARCH = x86
		LIBS += -lsocket -lz -lbz2
	}
	
	INCLUDEPATH += ../ffmpeg/include
	LIBS += -lcamapi -L../ffmpeg/lib/{gpl|lgpl}/qnx/$${ARCH} -lavformat -lavcodec -lavutil

## Including FFmpeg in the BAR

	<!-- include libs for armle-v7 -->
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/armle-v7/libavcodec.so.54">lib/libavcodec.so.54</asset>
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/armle-v7/libavformat.so.54">lib/libavformat.so.54</asset>
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/armle-v7/libavutil.so.51">lib/libavutil.so.51</asset>
	
	<!-- include libs for x86 -->
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/x86/libavcodec.a">lib/libavcodec.a</asset>
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/x86/libavformat.a">lib/libavformat.a</asset>
	<asset path="ffmpeg/lib/{gpl|lgpl}/qnx/x86/libavutil.a">lib/libavutil.a</asset>

# H.264 Optional Support

FFmpeg supports h.264 using the GPL library libx264. Included with libffbb is the libx264 source and prebuilt library files.

## Download libx264

Visit the website at: [http://www.videolan.org](http://www.videolan.org/developers/x264.html)

## Building libx264

	$ # build (shared) libs for armle-v7
	$ ./configure --cross-prefix=arm-unknown-nto-qnx8.0.0eabi- --enable-shared --host=arm-linux --disable-cli --prefix=`pwd`/target
	$ make install

	$ # build (static) libs for x86
	$ ./configure --cross-prefix=i486-pc-nto-qnx8.0.0- --enable-static --host=x86-linux --disable-asm --disable-cli --prefix=`pwd`/target
	$ make install

## Compiling against libx264

	INCLUDEPATH += ../libx264/include
	LIBS += -L../libx264/lib/$${ARCH} -lx264

## Including libx264 in the BAR

	<!-- include libs for armle-v7 -->
	<asset path="libx264/lib/armle-v7/libx264.so.125">lib/libx264.so.125</asset>
	
	<!-- include libs for x86 -->
	<asset path="libx264/lib/x86/libx264.a">lib/libx264.a</asset>

# License

While FFmpeg is either LGPL or GPL depending on how it is built, libffbb uses Apache License, Version 2.0.