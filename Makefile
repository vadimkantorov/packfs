LDD = ldd

STATICLDFLAGS = -fPIC -Wl,--wrap=open,--wrap=openat,--wrap=close,--wrap=read,--wrap=access,--wrap=lseek,--wrap=stat,--wrap=fstat,--wrap=fstatat,--wrap=statx,--wrap=fopen,--wrap=fclose,--wrap=fileno,--wrap=fcntl,--wrap=opendir,--wrap=fdopendir,--wrap=closedir,--wrap=readdir

DYNAMICLDFLAGS = -shared -fPIC -ldl -DPACKFS_DYNAMIC_LINKING

ARCHIVECFLAGS  = -Ilibarchive -Ilibarchive/libarchive -DPACKFS_ARCHIVE 

ARCHIVECFLAGSEXT = -D'PACKFS_ARCHIVEREADSUPPORTEXT=.iso:.zip:.tar:.tar.gz:.tar.xz' -D'PACKFS_ARCHIVEREADSUPPORTFORMAT(a)={archive_read_support_format_iso9660(a);archive_read_support_format_zip(a);archive_read_support_format_tar(a);archive_read_support_filter_gzip(a);archive_read_support_filter_xz(a);}'

libpackfs.so: packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -o $@ $^ $(DYNAMICLDFLAGS) $(ARCHIVECFLAGS) $(ARCHIVECFLAGSEXT) && $(LDD) $@

libpackfs.a : packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -c -o $(basename $@).o $< $(ARCHIVECFLAGS) $(ARCHIVECFLAGSEXT) $(STATICLDFLAGS) && $(AR) r $@ $(basename $@).o

packfs: packfs.c libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -o $@ $^ $(ARCHIVECFLAGS) -D'PACKFS_ARCHIVEREADSUPPORTEXT=.tar:.iso:.zip' -DPACKFS_STATIC_PACKER -D'PACKFS_ARCHIVEREADSUPPORTFORMAT(a)={archive_read_support_format_tar(a);archive_read_support_format_iso9660(a);archive_read_support_format_zip(a);}'

cat: cat.c libpackfs.a libarchive/.libs/libarchive.a zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	$(CC) -o $@ $^ $(STATICLDFLAGS) 

all: libpackfs.so libpackfs.a packfs

libarchive/.libs/libarchive.a: zlib/libz.a xz/src/liblzma/.libs/liblzma.a
	cd libarchive && sh ./build/autogen.sh && LDFLAGS="-L../zlib -L../xz/src/liblzma/.libs" CFLAGS="-I../zlib -I../xz/src/liblzma/ -I../xz/src/liblzma/api" sh configure --without-bz2lib --without-libb2 --without-iconv --without-lz4 --without-zstd --without-cng --without-xml2 --without-expat --without-openssl && $(MAKE)

zlib/libz.a:
	cd zlib && CFLAGS=-fPIC sh ./configure --static && $(MAKE)

xz/src/liblzma/.libs/liblzma.a:
	cd xz && sh ./autogen.sh && CFLAGS=-fPIC sh ./configure --disable-shared && $(MAKE)

clean:
	-rm libpackfs.so libpackfs.a libpackfs.o packfs
