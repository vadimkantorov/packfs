# libarchivepreload
Demo of abusing https://github.com/libarchive/libarchive to make `LD_PRELOAD`-based overrides of file-related functions. The `LD_PRELOAD`-based approach is useful when one doesn't have FUSE kernel module installed or does not have root permissions do use https://github.com/google/fuse-archive/ and it's wasteful to decompress a given archive.

```shell
cc -shared -fPIC libarchivepreload.c -o libarchivepreload.so -ldl libarchive/.libs/libarchive.a zlib/libz.a -Ilibarchive -Ilibarchive/libarchive #-DPACKFS_LOG 

zip -r libarchivepreload.zip libarchivepreload.c .git

LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/ls -lah libarchivepreload.zip/
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/ls -lah libarchivepreload.zip
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/ls -lah libarchivepreload.zip/libarchivepreload.c
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/cat libarchivepreload.zip/libarchivepreload.c
LD_PRELOAD=$PWD/libarchivepreload.so /usr/bin/find libarchivepreload.zip
```

# Limitations
- this demo does not optimize for iterative entry reads or iterative seeks, https://github.com/google/fuse-archive/ makes an attempt in that direction; also see https://github.com/libarchive/libarchive/issues/2306 for future support of fast seeks in ZIP / TAR / CPIO in libarchive

# References
- https://mropert.github.io/2018/02/02/pic_pie_sanitizers/
- https://github.com/google/fuse-archive/
- https://gist.github.com/vadimkantorov/2a4e092889b7132acd3b7ddfc2f2f907
- https://github.com/libarchive/libarchive/issues/2306
- https://github.com/coreutils/coreutils/blob/master/src/ls.c
- https://github.com/coreutils/coreutils/blob/master/src/cat.c
- https://git.busybox.net/busybox/tree/coreutils/ls.c
- https://git.busybox.net/busybox/tree/coreutils/cat.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/opendir.c
- https://git.musl-libc.org/cgit/musl/tree/src/dirent/readdir.c

- https://stackoverflow.com/questions/77100299/implementing-a-simple-version-of-readdir-with-system-call
- https://stackoverflow.com/questions/74890409/what-happens-in-c-function-readdir-from-dirent-h
- https://stackoverflow.com/questions/24103009/is-it-possible-to-implement-readdir-in-ubuntu-12-10-kernel-3-5
- https://unix.stackexchange.com/questions/625899/correctly-implementing-seeking-in-fuse-readdir-operation
- https://github.com/facebook/sapling/blob/5cc682e8ff24ef182be2dbe07e484396539e80f4/eden/fs/inodes/TreeInode.cpp#L1798-L1833- https://github.com/facebook/sapling/blob/main/eden/fs/docs/InodeLifetime.md
- https://lwn.net/Articles/544520/
- https://lwn.net/Articles/544298/
- https://github.com/TheAssassin/AppImageLauncher/issues/361
- https://github.com/sholtrop/ldpfuse/
- https://github.com/google/fuse-archive/blob/main/src/main.cc
- https://superuser.com/questions/1601311/fuse-fs-without-root-privileges-e-g-a-ld-preload-gateway-or-a-proot-plugin
- https://theses.liacs.nl/pdf/2021-2022-HoltropS.pdf
- https://github.com/fritzw/ld-preload-open
