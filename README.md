# License
MIT

# libarchivepreload
Demo of abusing https://github.com/libarchive/libarchive to make `LD_PRELOAD`-based overrides of file-related functions. The `LD_PRELOAD`-based approach is useful when one doesn't have FUSE kernel module installed or does not have root permissions do use https://github.com/google/fuse-archive/ and it's wasteful to decompress a given archive.

**Limitations:** This demo does not optimize for iterative entry reads or iterative seeks, https://github.com/google/fuse-archive/ makes an attempt in that direction; also see https://github.com/libarchive/libarchive/issues/2306 for future support of fast seeks in ZIP / TAR / CPIO in libarchive

**Note:** for now only single-threaded support

# Syntax of PACKFS
`PACKFS` is a colon-separated list of parts:
- paths to directories, ending with a trailing `/`
- paths to files: archives or `.json`-listings

Each part can have format:
- `path`
- `path@mountpoint`
- `path@mountpoint@listingresolutiondir` - this is useful if listings are built-in, and the resolution-dir is remote, this allows to not scan the `listingresolutiondir`'s archives when not needed

If `listingresolutiondir` is not empty, `mountpoint` can be empty

## Examples:
- `/myarchivesdir@/packfsarchives`
- `/mylistingsdir@/packfslistings@/myarchivesdir`
- `PACKFS_CONFIG=texlive.iso:/packfs/archive/@/packfs/texlive-archive/` (mount ISO file to /packfs/, and mount all package archives as TDS without installation) ?
- zip static-linked in the binary (two cases: compressed, uncompressed and maybe even just appended?), e.g. `PACKFS_CONFIG=/packfs/my.zip`


# Overridden libc / posix functions
- [`open`](https://man7.org/linux/man-pages/man2/open.2.html)
- [`openat`](https://man7.org/linux/man-pages/man2/openat.2.html)
- [`close`](https://man7.org/linux/man-pages/man2/close.2.html)
- [`read`](https://man7.org/linux/man-pages/man2/read.2.html)
- [`access`](https://man7.org/linux/man-pages/man2/access.2.html)
- [`lseek`](https://man7.org/linux/man-pages/man2/lseek.2.html)
- [`stat`](https://man7.org/linux/man-pages/man2/stat.2.html)
- [`fstat`](https://man7.org/linux/man-pages/man2/fstat.2.html)
- [`fstatat`](https://man7.org/linux/man-pages/man2/fstatat.2.html)
- [`statx`](https://man7.org/linux/man-pages/man2/statx.2.html)
- [`fopen`](https://en.cppreference.com/w/c/io/fopen)
- [`fclose`](https://en.cppreference.com/w/c/io/fclose)
- [`fileno`](https://man7.org/linux/man-pages/man3/fileno.3.html)
- [`fcntl`](https://man7.org/linux/man-pages/man2/fcntl.2.html) - only `F_DUPFD` and `F_DUPFD_CLOEXEC` is emulated
- [`opendir`](https://man7.org/linux/man-pages/man3/opendir.3.html)
- [`fdopendir`](https://man7.org/linux/man-pages/man3/fdopendir.3p.html)
- [`readdir`](https://man7.org/linux/man-pages/man3/readdir.3.html)
- [`closedir`](https://man7.org/linux/man-pages/man3/closedir.3.html)

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
- https://github.com/facebook/sapling/blob/5cc682e8ff24ef182be2dbe07e484396539e80f4/eden/fs/inodes/TreeInode.cpp#L1798-L1833
- https://github.com/facebook/sapling/blob/main/eden/fs/docs/InodeLifetime.md
- https://lwn.net/Articles/544520/
- https://lwn.net/Articles/544298/
- https://github.com/TheAssassin/AppImageLauncher/issues/361
- https://github.com/sholtrop/ldpfuse/
- https://github.com/google/fuse-archive/blob/main/src/main.cc
- https://superuser.com/questions/1601311/fuse-fs-without-root-privileges-e-g-a-ld-preload-gateway-or-a-proot-plugin
- https://theses.liacs.nl/pdf/2021-2022-HoltropS.pdf
- https://github.com/fritzw/ld-preload-open
- https://github.com/strace/strace/blob/8399328f628182cf236a1c39a074d601babdeaa4/src/fcntl.c#L102
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/fcntl.c;h=7cd3a0f976d9f9c9fbb1b5cab5b57780a72cec58;hb=HEAD
- https://savannah.gnu.org/bugs/?48169
- https://github.com/coreutils/gnulib/blob/master/lib/fcntl.c
- https://github.com/coreutils/gnulib/blob/master/lib/cloexec.c
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/fcntl.c
- https://git.savannah.gnu.org/gitweb/?p=gnulib.git;a=blob;f=lib/cloexec.c
- https://man7.org/linux/man-pages/man2/fcntl.2.html
- https://github.com/emscripten-core/emscripten/blob/main/tools/file_packager.py
- https://github.com/6over3/zeroperl/blob/main/tools/sfs.js
