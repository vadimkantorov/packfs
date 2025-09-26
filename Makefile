STATICLDFLAGS = -fPIC -Wl,--wrap=open,--wrap=openat,--wrap=close,--wrap=read,--wrap=access,--wrap=lseek,--wrap=stat,--wrap=fstat,--wrap=fstatat,--wrap=statx,--wrap=fopen,--wrap=fclose,--wrap=fileno,--wrap=fcntl,--wrap=opendir,--wrap=fdopendir,--wrap=closedir,--wrap=readdir

DYNAMICLDFLAGS = -shared -fPIC -ldl -DPACKFS_DYNAMIC_LINKING

ARCHIVECFLAGS = -DPACKFS_ARCHIVE -Ilibarchive -Ilibarchive/libarchive

LDD = ldd

libpackfs.so: packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -o $@ $^ $(DYNAMICLDFLAGS) $(ARCHIVECFLAGS) && $(LDD) $@

libpackfs.a : packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -c -o $(basename $@).o $< $(STATICLDFLAGS) $(ARCHIVECFLAGS) && $(AR) r $@ $(basename $@).o

packfs: packfs.c
	$(CC) -o $@ $^ $(STATICLDFLAGS) -DPACKFS_STATIC_PACKER

cat: cat.c libpackfs.a libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -o $@ $^ $(STATICLDFLAGS) 

all: libpackfs.so libpackfs.a

libarchive/.libs/libarchive.a: zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	cd libarchive && sh ./build/autogen.sh && LDFLAGS="-L../zlib -L../xz/src/liblzma/.libs" CFLAGS="-I../zlib -I../xz/src/liblzma/ -I../xz/src/liblzma/api" sh configure --without-bz2lib --without-libb2 --without-iconv --without-lz4 --without-zstd --without-cng --without-xml2 --without-expat --without-openssl && $(MAKE)

zlib/libz.a:
	cd zlib && CFLAGS=-fPIC sh ./configure --static && $(MAKE)

xz/src/liblzma/.libs/liblzma.a:
	cd xz && sh ./autogen.sh && CFLAGS=-fPIC sh ./configure --disable-shared && $(MAKE)

clean:
	-rm libpackfs.so libpackfs.a libpackfs.o
