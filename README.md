Camera API add-on with FFmpeg support.

Support forum post:  
http://supportforums.blackberry.com/t5/Native-Development/Camera-API-NV12-frame-to-AVFrame-FFmpeg/td-p/1842089

Building FFmpeg:

	$ # build for arm
	$ ./configure --enable-cross-compile --cross-prefix=arm-unknown-nto-qnx8.0.0eabi- --arch=armv7 --disable-debug --enable-optimizations --enable-asm --disable-static --enable-shared --target-os=qnx --disable-ffplay --disable-ffserver --disable-ffprobe --prefix=`pwd`/target  
	$ make install 

	$ # build for x86 (this config won't work)
	$ #./configure --enable-cross-compile --cross-prefix=i486-pc-nto-qnx8.0.0- --arch=x86 --disable-debug --enable-optimizations --enable-asm --disable-static --enable-shared --target-os=qnx --disable-ffplay --disable-ffserver --disable-ffprobe --disable-yasm --prefix=`pwd`/target  
	$ #make install  

Compiling against this library:

	LIBS += -lcamapi -lavcodec -lavformat -lavutil