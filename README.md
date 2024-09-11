# libarchivepreload
Demo of abusing https://github.com/libarchive/libarchive to make `LD_PRELOAD`-based overrides of file-related functions. The `LD_PRELOAD`-based approach is useful when one doesn't have FUSE kernel module installed or does not have root permissions do use https://github.com/google/fuse-archive/ and it's wasteful to decompress a given archive.

```shell
cc -shared -fPIC libarchivepreload.c -o libarchivepreload.so -ldl larchive -Llibarchive/.libs -Ilibarchive -Ilibarchive/libarchive   

zip libarchivepreload.zip libarchivepreload.c

LD_PRELOAD=$PWD/libarchivepreload.so LIBARCHIVEPRELOAD=libarchivepreload.zip /usr/bin/ls
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/ls libarchivepreload.zip

LD_PRELOAD=$PWD/libarchivepreload.so LIBARCHIVEPRELOAD=libarchivepreload.zip /usr/bin/cat libarchivepreload.c
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/cat libarchivepreload.zip/libarchivepreload.c
```

# Limitations
- this demo does not optimize for iterative entry reads or iterative seeks, https://github.com/google/fuse-archive/ makes an attempt in that direction; also see https://github.com/libarchive/libarchive/issues/2306 for future support of fast seeks in ZIP / TAR / CPIO in libarchive

# References
- https://github.com/google/fuse-archive/
- https://gist.github.com/vadimkantorov/2a4e092889b7132acd3b7ddfc2f2f907
- https://github.com/libarchive/libarchive/issues/2306
- https://github.com/coreutils/coreutils/blob/master/src/ls.c
- https://github.com/coreutils/coreutils/blob/master/src/cat.c
- https://git.busybox.net/busybox/tree/coreutils/ls.c
- https://git.busybox.net/busybox/tree/coreutils/cat.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/opendir.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/readdir.c
