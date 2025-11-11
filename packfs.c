// TODO: where do path normalization? add comment for every function on path string constraints
// TODO: use safe string functions everywhere
// TODO: prevent re-entrant packfs_init: happens when reading archive mentioned in PACKFS_CONFIG
// TODO: support reading from decompressed entries by offsets and support mmap-reads 
// TODO: check and handle various limits. if dir or prefix is considered without trailing slash - specify in varname, check path lens fit in packfs limit, check d_name length: https://unix.stackexchange.com/questions/619625/to-what-extent-does-linux-support-file-names-longer-than-255-bytes
// TODO: maybe use #include <stdbool.h> / bool / true / false - write wrapper in ctypes
// TODO: report error via a global errno / geterrno string
// TODO: compute offset in SPLIT

#ifdef PACKFS_ARCHIVE
#ifndef PACKFS_ARCHIVEREADSUPPORTEXT
#define PACKFS_ARCHIVEREADSUPPORTEXT .iso:.zip:.tar:.tar.gz:.tar.xz
#endif
#ifndef PACKFS_ARCHIVEREADSUPPORTFORMAT
#define PACKFS_ARCHIVEREADSUPPORTFORMAT(a) {archive_read_support_format_iso9660(a);archive_read_support_format_zip(a);archive_read_support_format_tar(a);archive_read_support_filter_gzip(a);archive_read_support_filter_xz(a);}
#endif
#endif

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ftw.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#ifdef PACKFS_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif


#define PACKFS_STRING_VALUE_(x) #x
#define PACKFS_STRING_VALUE(x) PACKFS_STRING_VALUE_(x)
#define PACKFS_CONCAT_(X, Y) X ## Y
#define PACKFS_CONCAT(X, Y) PACKFS_CONCAT_(X, Y)

#ifdef  PACKFS_DYNAMIC_LINKING
#define PACKFS_EXTERN_MOD
#define PACKFS_EXTERN_PTR(x)       (*x)
#define PACKFS_WRAP(x)             ( x)
#else
#define PACKFS_EXTERN_MOD extern
#define PACKFS_EXTERN_PTR(x)        (x)
#define PACKFS_WRAP(x) PACKFS_CONCAT(__wrap_, x)
#endif

#define PACKFS_EMPTY(s) ((s) == NULL || (s)[0] == '\0')

#define PACKFS_SPLIT(path, sep, entryabspath, entryabspath_len, prefix_len, i, islast) for(size_t packfs_split_k = 0, (entryabspath_len) = 0, (prefix_len) = 0, (islast) = 0, (i) = 0; packfs_split_k < 1; packfs_split_k++) for(const char* (entryabspath) = (path), *end = strchr((path), (sep)), *prevend = (path), *safeend = (end != NULL ? end : ((entryabspath) + strlen((entryabspath)))); ((entryabspath_len) = safeend - (entryabspath) ), ((prefix_len) = safeend - (path) + 1), ((islast) = end == NULL), (prevend != NULL); prevend = end, (entryabspath) = ((end != NULL && *end != '\0') ? (end + 1) : NULL), end = ((end != NULL && *end != '\0') ? strchr(end + 1, (sep)) : NULL), safeend = (end != NULL ? end : ((entryabspath) != NULL ? ((entryabspath) + strlen((entryabspath))) : NULL))) if(safeend != (entryabspath))

#define PACKFS_APPEND(paths, paths_len, path, path_len, sep)  { if((paths_len) > 0) {(paths)[(paths_len)++] = (sep); } strncpy((paths) + (paths_len), (path), (path_len)); (paths_len) += (path_len); }

char packfs_default_prefix[] = 
#ifdef PACKFS_PREFIX
PACKFS_STRING_VALUE(PACKFS_PREFIX)
#else
"/packfs/"
#endif
;

char packfs_archives_ext[] =
#ifdef PACKFS_ARCHIVE
PACKFS_STRING_VALUE(PACKFS_ARCHIVEREADSUPPORTEXT)
#else
""
#endif
;

char packfs_listing_ext[] = ".json";
char packfs_sep = '/';
char packfs_pathsep = ':';
char packfs_extsep = '.';
char packfs_atsep = '@';

enum 
{
    PACKFS_ERROR_NOTINRANGE = -2,
    PACKFS_ERROR_BAD = -1,
    PACKFS_ERROR = 1,
    PACKFS_OK = 0,
};

enum
{
    packfs_path_max = 255,
    
    packfs_dynamic_files_num_max = 65536,
    packfs_descr_fd_min = 1000000000,
    packfs_descr_fd_max = packfs_descr_fd_min + packfs_dynamic_files_num_max,
    packfs_descr_fd_cnt = packfs_descr_fd_max - packfs_descr_fd_min,

    packfs_static_ino_offset = 1000000000,
    packfs_dynamic_ino_offset = packfs_static_ino_offset + packfs_static_ino_offset,
    packfs_dirs_ino_offset = 1000000,
};

const char packfs_static[] = "packfs_static";
#ifdef PACKFS_STATIC
#include "packfs_static.h"
#else
const char   packfs_static_prefix[] = "";
const char   packfs_static_files_paths[] = ""; 
const char   packfs_static_dirs_paths[] = ""; 
const char*  _binary_packfs_static_start;
const char*  _binary_packfs_static_end;
const size_t packfs_static_files_num;
const size_t packfs_static_dirs_num;
const size_t packfs_static_files_offsets[0]; 
const size_t packfs_static_files_sizes[0];
#endif


#ifndef PACKFS_STATIC_PACKER

PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_open)         (const char *path, int flags, ...);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_openat)       (int dirfd, const char *path, int flags, ...);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_close)        (int fd);
PACKFS_EXTERN_MOD ssize_t               PACKFS_EXTERN_PTR(__real_read)         (int fd, void* buf, size_t count);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_access)       (const char *path, int flags);
PACKFS_EXTERN_MOD off_t                 PACKFS_EXTERN_PTR(__real_lseek)        (int fd, off_t offset, int whence);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_stat)         (const char *restrict path, struct stat *restrict statbuf);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fstat)        (int fd, struct stat * statbuf);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fstatat)      (int dirfd, const char* path, struct stat * statbuf, int flags);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_statx)        (int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
PACKFS_EXTERN_MOD FILE*                 PACKFS_EXTERN_PTR(__real_fopen)        (const char *path, const char *mode);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fclose)       (FILE* stream);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fileno)       (FILE* stream);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_fcntl)        (int fd, int action, ...);
PACKFS_EXTERN_MOD DIR*                  PACKFS_EXTERN_PTR(__real_opendir)      (const char *path);
PACKFS_EXTERN_MOD DIR*                  PACKFS_EXTERN_PTR(__real_fdopendir)    (int dirfd);
PACKFS_EXTERN_MOD int                   PACKFS_EXTERN_PTR(__real_closedir)     (DIR *dirp);
PACKFS_EXTERN_MOD struct dirent*        PACKFS_EXTERN_PTR(__real_readdir)      (DIR *dirp);

#else

#define __real_open                     open
#define __real_openat                   openat 
#define __real_close                    close
#define __real_read                     read
#define __real_access                   access
#define __real_lseek                    lseek
#define __real_stat                     stat
#define __real_fstat                    fstat 
#define __real_fstatat                  fstatat
#define __real_statx                    statx
#define __real_fopen                    fopen
#define __real_fclose                   fclose
#define __real_fileno                   fileno
#define __real_fcntl                    fcntl
#define __real_opendir                  opendir
#define __real_fdopendir                fdopendir
#define __real_closedir                 closedir
#define __real_readdir                  readdir

#endif

int packfs_init__real()
{
#ifdef PACKFS_DYNAMIC_LINKING
    #include <dlfcn.h>
    __real_open      = dlsym(RTLD_NEXT, "open"      );
    __real_openat    = dlsym(RTLD_NEXT, "openat"    );
    __real_close     = dlsym(RTLD_NEXT, "close"     );
    __real_read      = dlsym(RTLD_NEXT, "read"      );
    __real_access    = dlsym(RTLD_NEXT, "access"    );
    __real_lseek     = dlsym(RTLD_NEXT, "lseek"     );
    __real_stat      = dlsym(RTLD_NEXT, "stat"      );
    __real_fstat     = dlsym(RTLD_NEXT, "fstat"     );
    __real_fstatat   = dlsym(RTLD_NEXT, "fstatat"   );
    __real_statx     = dlsym(RTLD_NEXT, "statx"     );
    __real_fopen     = dlsym(RTLD_NEXT, "fopen"     );
    __real_fclose    = dlsym(RTLD_NEXT, "fclose"    );
    __real_fileno    = dlsym(RTLD_NEXT, "fileno"    );
    __real_fcntl     = dlsym(RTLD_NEXT, "fcntl"     );
    __real_opendir   = dlsym(RTLD_NEXT, "opendir"   );
    __real_fdopendir = dlsym(RTLD_NEXT, "fdopendir" );
    __real_closedir  = dlsym(RTLD_NEXT, "closedir"  );
    __real_readdir   = dlsym(RTLD_NEXT, "readdir"   );
    if(__real_open     == NULL || 
       __real_openat   == NULL || 
       __real_close    == NULL || 
       __real_read     == NULL || 
       __real_access   == NULL || 
       __real_lseek    == NULL || 
       __real_stat     == NULL || 
       __real_fstat    == NULL || 
       __real_fstatat  == NULL || 
       __real_statx    == NULL || 
       __real_fopen    == NULL || 
       __real_fclose   == NULL || 
       __real_fileno   == NULL || 
       __real_fcntl    == NULL || 
       __real_opendir  == NULL || 
       __real_fdopendir== NULL || 
       __real_closedir == NULL || 
       __real_readdir  == NULL )
        return PACKFS_ERROR;
#endif
    return PACKFS_OK;
}


bool packfs_initialized;
bool packfs_enabled;
int             packfs_descr_fd                [packfs_descr_fd_cnt];
int             packfs_descr_refs              [packfs_descr_fd_cnt];
bool            packfs_descr_isdir             [packfs_descr_fd_cnt]; // TODO: replace with checks of fd's LSB
void*           packfs_descr_fileptr           [packfs_descr_fd_cnt];
size_t          packfs_descr_size              [packfs_descr_fd_cnt];
size_t          packfs_descr_ino               [packfs_descr_fd_cnt];
struct dirent   packfs_descr_dirent            [packfs_descr_fd_cnt];
char   packfs_dynamic_prefix                [packfs_dynamic_files_num_max * packfs_path_max];
char   packfs_dynamic_archive_paths         [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_archive_paths_len;
size_t packfs_dynamic_archive_paths_offset  [packfs_dynamic_files_num_max];
char   packfs_dynamic_files_paths           [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_files_paths_len;
size_t packfs_dynamic_files_sizes           [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_offsets         [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_paths_prefixlen [packfs_dynamic_files_num_max];
size_t packfs_dynamic_files_num;

char   packfs_dynamic_dirs_paths            [packfs_dynamic_files_num_max * packfs_path_max];
size_t packfs_dynamic_dirs_paths_len;
size_t packfs_dynamic_dirs_num;

size_t packfs_path_len(const char* path)
{
    if(PACKFS_EMPTY(path) || path[0] == packfs_pathsep)
        return 0;
    const char* sep = strchr(path, packfs_pathsep);
    if(sep == NULL)
        return strlen(path);
    return sep - path;
}

int packfs_normpath(const char* path, char* path_normalized, size_t path_normalized_sizeof)
{
    if(path == NULL || path_normalized == NULL) return PACKFS_ERROR;
    if(path_normalized_sizeof == 0) return PACKFS_ERROR;
    path_normalized[0] = '\0';
    const size_t len = path != NULL ? strlen(path) : 0;
    if(len == 0) return PACKFS_OK;
    if(path_normalized_sizeof < len) return PACKFS_ERROR;

    // lstrips ./ in the beginning
    size_t path_normalized_len = 0;
    for(int i = (path != NULL && len > 2 && path[0] == packfs_extsep && path[1] == packfs_sep) ? 2 : 0; len > 0 && i < len; i++)
    {
        if(!(i > 1 && path[i] == packfs_sep && path[i - 1] == packfs_sep))
        {
            //collapses double consecutive slashes
            path_normalized[path_normalized_len++] = path[i];
            path_normalized[path_normalized_len] = '\0';
        }
    }
    
    // and rstrips abc/.. at the end of /path/to/abc/asd/.. 
    if(path_normalized_len >= 3 && path_normalized[path_normalized_len - 1] == packfs_extsep && path_normalized[path_normalized_len - 2] == packfs_extsep  && path_normalized[path_normalized_len - 3] == packfs_sep)
    {
        path_normalized[path_normalized_len - 3] = '\0';
        char* trailing_slash = strrchr(path_normalized, packfs_sep);
        if(trailing_slash != NULL)
            *trailing_slash = '\0';
    }
    return PACKFS_OK;
}

int packfs_dump_listing(const char* removeprefix, const char* output_path)
{
    int res = PACKFS_OK;
    if(PACKFS_EMPTY(output_path)) { res = PACKFS_ERROR; return res; }

    const size_t removeprefix_len = packfs_path_len(removeprefix);

    FILE* f = fopen(output_path, "w");
    if(!f) { res = PACKFS_ERROR; return res; }
    fprintf(f, "[\n");
    size_t first = true;
    PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        const char* relative_path = entryabspath + removeprefix_len;
        const size_t relative_len = entryabspath_len - removeprefix_len;
        fprintf(f, "%c {\"path\": \"%.*s\"}\n", first ? ' ' : ',', (int)relative_len, relative_path);
        first = false;
    }
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        const char* relative_path = entryabspath + removeprefix_len;
        const size_t relative_len = entryabspath_len - removeprefix_len;
        fprintf(f, "%c {\"path\": \"%.*s\", \"size\": %zu, \"offset\": %zu}\n", first ? ' ' : ',', (int)relative_len, relative_path, packfs_dynamic_files_sizes[i], packfs_dynamic_files_offsets[i]);
        first = false;
    }
    fprintf(f, "]\n\n");
    if(0 != fclose(f)) { res = PACKFS_ERROR; return res; }
    return res;
}

int packfs_dump_static_package(const char* prefix, const char* removeprefix, const char* output_path, const char* ld, const char* input_path)
{
    int res = PACKFS_OK;
    if(PACKFS_EMPTY(output_path)) { res = PACKFS_ERROR; return res; }

#ifdef PACKFS_STATIC_PACKER
    if(!PACKFS_EMPTY(input_path) && !PACKFS_EMPTY(ld))
    {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "\"%s\" -r -b binary -o \"%s.o\" \"%s\"", ld, output_path, input_path);
        res = system(tmp);
        if(res != 0) { fprintf(stderr, "#could not open invoke ld: %s.o\n", output_path); return res; }
    }

    const size_t prefix_len = packfs_path_len(prefix);
    const size_t prefix_len_m1 = (prefix_len >= 1 && prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;
    const size_t removeprefix_len_ = packfs_path_len(removeprefix);
    const size_t removeprefix_len_m1 = (removeprefix_len_ >= 1 && removeprefix[removeprefix_len_ - 1] == packfs_sep) ? (removeprefix_len_ - 1) : removeprefix_len_;
    const size_t removeprefix_len = removeprefix_len_ > 0 ? (removeprefix_len_m1 + 1) : 0;
        
    FILE* f = fopen(output_path, "w");
    if(!f) { res = PACKFS_ERROR; return res; }

    fprintf(f, "#include <stddef.h>\n\n");
    fprintf(f, "const char packfs_static_prefix[] = \"%.*s%c\";\n\n", (int)prefix_len_m1, prefix, packfs_sep);
    fprintf(f, "const size_t packfs_static_dirs_num = %zu, packfs_static_files_num = %zu;\n\n", packfs_dynamic_dirs_num, packfs_dynamic_files_num);
    fprintf(f, "extern char _binary_%s_start[], _binary_%s_end[];\n\n", packfs_static, packfs_static);
    fprintf(f, "const char packfs_static_dirs_paths[] =\n");
    PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        const char* relative_path = entryabspath + removeprefix_len;
        const size_t relative_len = entryabspath_len - removeprefix_len;
        fprintf(f, "\"%.*s%c%.*s\" \"%s\"\n", (int)prefix_len_m1, prefix, packfs_sep, (int)relative_len, relative_path, islast ? "" : ":");
    }
    if(packfs_dynamic_dirs_num == 0)
        fprintf(f, "\"\"\n");
    fprintf(f, ";\n\n");
    
    fprintf(f, "const char packfs_static_files_paths[] =\n");
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        const char* relative_path = entryabspath + removeprefix_len;
        const size_t relative_len = entryabspath_len - removeprefix_len;
        fprintf(f, "\"%.*s%c%.*s\" \"%s\"\n", (int)prefix_len_m1, prefix, packfs_sep, (int)relative_len, relative_path, islast ? "" : ":");
    }
    if(packfs_dynamic_files_num == 0)
        fprintf(f, "\"\"\n");
    fprintf(f, ";\n\n");
    
    fprintf(f, "\n\nconst size_t packfs_static_files_offsets[] = {\n");
    for(size_t i = 0; i < packfs_dynamic_files_num; i++)
        fprintf(f, "%zu,\n", packfs_dynamic_files_offsets[i]); 
    fprintf(f, "};\n\n");
    
    fprintf(f, "\n\nconst size_t packfs_static_files_sizes[] = {\n");
    for(size_t i = 0; i < packfs_dynamic_files_num; i++)
        fprintf(f, "%zu,\n", packfs_dynamic_files_sizes[i]); 
    fprintf(f, "};\n\n");

    if(0 != fclose(f)) { res = PACKFS_ERROR; return res; }
#endif
    return res;
}

bool packfs_match_ext(const char* path, size_t path_len, const char* exts)
{
    if(path_len == 0 || PACKFS_EMPTY(path) || PACKFS_EMPTY(exts))
        return false;

    const char* path_ext = strrchr(path, packfs_extsep);
    if(path_ext == NULL)
        return false;
    
    PACKFS_SPLIT(exts, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        if(entryabspath_len > 0 && path_len >= entryabspath_len && 0 == strncmp(entryabspath, path + path_len - entryabspath_len, entryabspath_len))
            return true;
    }
    return false;
}

size_t packfs_calc_archive_prefixlen(const char* path, const char* exts)
{
    if(PACKFS_EMPTY(path) || PACKFS_EMPTY(exts))
        return 0;

    PACKFS_SPLIT(path, packfs_sep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        if(packfs_match_ext(path, prefix_len - 1, exts))
            return prefix_len;
    }
    return 0;
}

enum packfs_match_path_mode
{
    PACKFS_DIR_EXISTS = 0, 
    PACKFS_FILE_MATCHES = 1, 
    PACKFS_ENTRY_IN_DIR = 2, 
    PACKFS_ENTRY_IN_DIR_RECURSIVELY = 3
};

bool packfs_match_path(const char* haystack, size_t haystack_len, const char* needle, size_t needle_len, const enum packfs_match_path_mode mode)
{
    switch(mode)
    {
        case PACKFS_DIR_EXISTS:
        {
            if(needle[needle_len - 1] == packfs_sep)
                needle_len--;

            PACKFS_SPLIT(haystack, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
            {
                if(0 != strncmp(entryabspath, needle, needle_len))
                    continue;

                if(((entryabspath_len == needle_len) || (entryabspath_len == needle_len + 1 && entryabspath[needle_len] == packfs_sep && (entryabspath[needle_len + 1] == '\0' || entryabspath[needle_len + 1] == packfs_pathsep))))
                    return true;
            }
            return false;
        }
        case PACKFS_FILE_MATCHES:
        {
            if(0 != strncmp(haystack, needle, needle_len))
                return false;

            return (haystack[needle_len] == '\0' || haystack[needle_len] == packfs_pathsep);
        }
        case PACKFS_ENTRY_IN_DIR_RECURSIVELY:
        {
            if(PACKFS_EMPTY(haystack) || PACKFS_EMPTY(needle))
                return false;
            const size_t needle_len = strlen(needle);
            
            PACKFS_SPLIT(haystack, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
            {
                const int prefix_trailing_slash = entryabspath[entryabspath_len - 1] == packfs_sep;
                const int prefix_ok = 0 == strncmp(entryabspath, needle, entryabspath_len - prefix_trailing_slash);
                const size_t len_m1 = entryabspath_len - prefix_trailing_slash;
                if(prefix_ok && ((needle_len == len_m1) || (needle_len >= entryabspath_len && needle[len_m1] == packfs_sep)))
                    return entryabspath_len > 0;//entryabspath_len;
            }
            return false;
        }
        case PACKFS_ENTRY_IN_DIR:
        {
            if(haystack_len == 0 || (haystack_len > 0 && haystack[haystack_len - 1] != packfs_sep))
                return false;
            const int prefix_matches = 0 == strncmp(haystack, needle, haystack_len - 1);
            if(!prefix_matches)
                return false;
            const char* suffix_slash = strchr(needle + haystack_len, packfs_sep);
            const int no_suffix_slash = (NULL == suffix_slash) || (suffix_slash >= needle + needle_len);
            const int suffix_without_dirs = no_suffix_slash || (needle + needle_len - 1 == suffix_slash);
            const int suffix_not_empty = needle_len - haystack_len > 0;
            return suffix_without_dirs && suffix_not_empty;
        }
        default:
            return false;
    }
}

int packfs_dynamic_add_file(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, size_t size, size_t offset, size_t archivepaths_offset)
{
    if(entrypath_len == 0 || PACKFS_EMPTY(entrypath)) return PACKFS_ERROR;
    const size_t prefix_len_m1 = (prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;
    prefix_len = prefix_len_m1 > 0 ? (prefix_len_m1 + 1) : prefix_len_m1;

    if(prefix_len_m1 > 0)
    {
        PACKFS_APPEND(packfs_dynamic_files_paths, packfs_dynamic_files_paths_len, prefix, prefix_len_m1, packfs_pathsep);
        packfs_dynamic_files_paths[packfs_dynamic_files_paths_len++] = packfs_sep;
        strncpy(packfs_dynamic_files_paths + packfs_dynamic_files_paths_len, entrypath, entrypath_len);
        if(entrypath[entrypath_len - 1] == packfs_sep) entrypath_len--; // FIXME: needed?
        packfs_dynamic_files_paths_len += entrypath_len;
    }
    else
    {
        PACKFS_APPEND(packfs_dynamic_files_paths, packfs_dynamic_files_paths_len, entrypath, entrypath_len, packfs_pathsep);
    }
                
    packfs_dynamic_files_paths_prefixlen[packfs_dynamic_files_num] = prefix_len;
    packfs_dynamic_files_sizes[packfs_dynamic_files_num] = size;
    packfs_dynamic_files_offsets[packfs_dynamic_files_num] = offset;
    packfs_dynamic_archive_paths_offset[packfs_dynamic_files_num] = archivepaths_offset;
    packfs_dynamic_files_num++;
    return PACKFS_OK;
}

int packfs_dynamic_add_dirname(const char* prefix, size_t prefix_len, const char* entrypath, size_t entrypath_len, bool isdir)
{
    if((prefix_len == 0 || PACKFS_EMPTY(prefix)) && (entrypath_len == 0 || PACKFS_EMPTY(entrypath))) return 1;
    const size_t prefix_len_m1 = (prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;
    prefix_len = prefix_len_m1 > 0 ? (prefix_len_m1 + 1) : prefix_len_m1;

    char path[packfs_path_max] = {0};
    if(prefix_len_m1 > 0)
    {
        strncpy(path, prefix, prefix_len_m1);
        path[prefix_len_m1] = packfs_sep;
    }
    
    strncpy(path + prefix_len, entrypath, entrypath_len);
    if(isdir && path[prefix_len + entrypath_len - 1] != packfs_sep)
        path[prefix_len + entrypath_len] = packfs_sep;

    PACKFS_SPLIT(path + prefix_len_m1 - 1, packfs_sep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        if(!islast)
        {
            const bool match = packfs_match_path(packfs_dynamic_dirs_paths, packfs_dynamic_dirs_paths_len, path, prefix_len, PACKFS_DIR_EXISTS);
            if(match)
                continue;

            PACKFS_APPEND(packfs_dynamic_dirs_paths, packfs_dynamic_dirs_paths_len, path, prefix_len, packfs_pathsep);
            packfs_dynamic_dirs_num++;
        }
    }
    return PACKFS_OK;
}

int packfs_dynamic_add_prefix(const char* prefix, size_t prefix_len)
{
    if(prefix_len == 0 || PACKFS_EMPTY(prefix)) return PACKFS_ERROR;
    const size_t prefix_len_m1 = (prefix[prefix_len - 1] == packfs_sep) ? (prefix_len - 1) : prefix_len;
    
    size_t packfs_dynamic_prefix_len = strlen(packfs_dynamic_prefix);
    const bool match = packfs_match_path(packfs_dynamic_prefix, packfs_dynamic_prefix_len, prefix, prefix_len_m1, PACKFS_DIR_EXISTS);
    if(match)
        return PACKFS_OK;

    PACKFS_APPEND(packfs_dynamic_prefix, packfs_dynamic_prefix_len, prefix, prefix_len_m1, packfs_pathsep);
    packfs_dynamic_prefix[packfs_dynamic_files_paths_len] = packfs_sep;
    packfs_dynamic_prefix[packfs_dynamic_files_paths_len + 1] = '\0';
    return PACKFS_OK;
}


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

// https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_file.c
// https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_fd.c

struct packfs_archive_data
{
    int fd;
    size_t last_file_offset;
    uint8_t buffer[1024 * 4];
};

int64_t packfs_archive_seek_callback(struct archive *a, void *client_data, int64_t request, int whence)
{
    const struct packfs_archive_data *mine = (struct packfs_archive_data *)client_data;
    const int64_t seek = request;
    int64_t res = 0;
    const int seek_bits = sizeof(seek) * 8 - 1;  /* off_t is a signed type. */

    /* We use off_t here because lseek() is declared that way. */

    /* Do not perform a seek which cannot be fulfilled. */
    if (sizeof(request) > sizeof(seek))
    {
        const int64_t max_seek = (((int64_t)1 << (seek_bits - 1)) - 1) * 2 + 1;
        const int64_t min_seek = ~max_seek;
        if (request < min_seek || request > max_seek)
        {
            errno = EOVERFLOW;
            goto err;
        }
    }

    res = lseek(mine->fd, seek, whence);
    if (res >= 0)
        return res;

err:
    if (errno == ESPIPE)
    {
        archive_set_error(a, errno, "A file descriptor(%d) is not seekable(PIPE)", mine->fd);
        return (ARCHIVE_FAILED);
    }
    else
    {
        /* If the input is corrupted or truncated, fail. */
        archive_set_error(a, errno, "Error seeking in a file descriptor(%d)", mine->fd);
        return (ARCHIVE_FATAL);
    }
}


ssize_t packfs_archive_read_callback(struct archive *a, void *client_data, const void **buff)
{
    // https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_fd.c
    struct packfs_archive_data *mine = (struct packfs_archive_data *)client_data;
    
    mine->last_file_offset = packfs_archive_seek_callback(a, client_data, 0, SEEK_CUR);

    *buff = mine->buffer;

    for (;;)
    {
        const ssize_t bytes_read = read(mine->fd, mine->buffer, sizeof(mine->buffer));
        if (bytes_read < 0)
        {
            if (errno == EINTR)
                continue;
            archive_set_error(a, errno, "Error reading fd %d", mine->fd);
        }
        return (bytes_read);
    }
    return 0;
}

int packfs_scan_archive(FILE* f, const char* packfs_archive_filename, const char* prefix)
{
    int res = PACKFS_OK;
#ifdef PACKFS_ARCHIVE
    struct archive *a = archive_read_new();
    PACKFS_ARCHIVEREADSUPPORTFORMAT(a);
    
    size_t prefix_len_m1 = prefix != NULL ? strlen(prefix) : 0;
    if(prefix_len_m1 > 0 && prefix[prefix_len_m1 - 1] == packfs_sep) prefix_len_m1--;
    const size_t packfs_archive_filename_len = strlen(packfs_archive_filename);

    packfs_dynamic_add_dirname(prefix, prefix_len_m1, "", 0, true); 
    packfs_dynamic_add_prefix(prefix, prefix_len_m1);
    
    const size_t archivepaths_offset = packfs_dynamic_archive_paths_len;
    strncpy(packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_len, packfs_archive_filename, packfs_archive_filename_len);
    packfs_dynamic_archive_paths_len += packfs_archive_filename_len + 1;

    struct archive_entry *entry;
    do
    {
        struct packfs_archive_data client_data;
        // TODO: switch to using FILE
        client_data.fd = open(packfs_archive_filename, O_RDONLY);
        archive_read_set_seek_callback(a, packfs_archive_seek_callback);
        archive_read_set_read_callback(a, packfs_archive_read_callback);
        archive_read_set_callback_data(a, &client_data);
        res = archive_read_open1(a);
        
        // res = archive_read_open_FILE(a, f);
        if(res != ARCHIVE_OK) { fprintf(stderr, "#%s\n", archive_error_string(a)); break; }
        
        for(;;)
        {
            res = archive_read_next_header(a, &entry);
            if (res == ARCHIVE_EOF)
                break;
            if (res != ARCHIVE_OK)
                break; // { fprintf(stderr, "#%s\n", archive_error_string(a)); return res; }
                
            const int entrytype = archive_entry_filetype(entry);
            const size_t size = (size_t)archive_entry_size(entry);
            const char* entrypath = archive_entry_pathname(entry);
            const size_t entrypath_len = strlen(entrypath);
            const bool isdir = entrytype == AE_IFDIR, isfile = entrytype == AE_IFREG;
            const char* entrytypestr = entrytype == AE_IFMT ? "AE_IFMT" : entrytype == AE_IFREG ? "AE_IFREG" : entrytype == AE_IFLNK ? "AE_IFLNK" : entrytype == AE_IFSOCK ? "AE_IFSOCK" : entrytype == AE_IFCHR ? "AE_IFCHR" : entrytype == AE_IFBLK ? "AE_IFBLK" : entrytype == AE_IFDIR ? "AE_IFDIR" : entrytype == AE_IFIFO ? "AE_IFIFO" : "archive_entry_pathname(entry) value is unknown";
            const void* firstblock_buff;
            size_t firstblock_len;
            int64_t firstblock_offset;
            res = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
            const char* firstblock_buff_charptr = (const char*)firstblock_buff;
            const char* curblock_buff_charptr = (const char*)client_data.buffer;
            const size_t offset = (isfile && archive_entry_size_is_set(entry) && curblock_buff_charptr <= firstblock_buff_charptr && firstblock_buff_charptr < curblock_buff_charptr + sizeof(client_data.buffer)) ? (client_data.last_file_offset + (size_t)(firstblock_buff_charptr - curblock_buff_charptr)) : 0;

            packfs_dynamic_add_dirname(prefix, prefix_len_m1, entrypath, entrypath_len, isdir); 
            if(isfile)
                packfs_dynamic_add_file(prefix, prefix_len_m1, entrypath, entrypath_len, size, offset, archivepaths_offset);
                
            res = archive_read_data_skip(a);
            if (res == ARCHIVE_EOF)
                break;
            if (res != ARCHIVE_OK)
                break; // { fprintf(stderr, "%s\n", archive_error_string(a)); return res; }
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
#endif
    return res;
}

int packfs_extract_archive_entry_from_FILE_to_FILE(FILE* f, const char* entrypath, size_t entrypath_len, FILE* h)
{
#ifdef PACKFS_ARCHIVE
    struct archive *a = archive_read_new();
    PACKFS_ARCHIVEREADSUPPORTFORMAT(a);

    struct archive_entry *entry;
    do
    {
        //if(archive_read_open_memory(a, buf, cnt) != ARCHIVE_OK)
        if(archive_read_open_FILE(a, f) != ARCHIVE_OK)
            break;
        
        for (;;)
        {
            int res = archive_read_next_header(a, &entry);
            if (res == ARCHIVE_EOF)
                break;
            if (res != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));

            const char* path = archive_entry_pathname(entry); size_t path_len = strlen(path);
            const bool match = packfs_match_path(path, path_len, entrypath, entrypath_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                const void *buff;
                size_t size;
                off_t offset;

                while ((res = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK)
                {
                    // assert(offset <= output_offset), do not support sparse files just yet, https://github.com/libarchive/libarchive/issues/2299
                    const char* p = buff;
                    while (size > 0)
                    {
                        const ssize_t bytes_written = fwrite(p, 1, size, h);
                        p += bytes_written;
                        size -= bytes_written;
                    }
                }
                break;
            }
            else
            {
                res = archive_read_data_skip(a);
                if (res == ARCHIVE_EOF)
                    break;
                if (res != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
        }
    }
    while(0);
    archive_read_close(a);
    archive_read_free(a);
#endif
    return PACKFS_OK;
}

int packfs_scan_archive_dir(DIR* dirptr, const char* path_normalized, size_t path_normalized_len, const char* prefix)
{
    for(struct dirent* entry = __real_readdir(dirptr); entry != NULL; entry = __real_readdir(dirptr))
    {
        const size_t archive_prefixlen = packfs_calc_archive_prefixlen(entry->d_name, packfs_archives_ext);
        if(archive_prefixlen > 0)
        {
            char _path_normalized[packfs_path_max];
            strcpy(_path_normalized, path_normalized);
            _path_normalized[path_normalized_len] = packfs_sep;
            strcpy(_path_normalized + path_normalized_len + 1, entry->d_name);
        
            FILE* fileptr = __real_fopen(_path_normalized, "rb");
            if(fileptr != NULL)
            {
                packfs_enabled = true;
                // something below seems to trigger again packfs_init();
                packfs_scan_archive(fileptr, _path_normalized, prefix);
                __real_fclose(fileptr);
            }

        }
    }
    return PACKFS_OK;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

int packfs_list_files_dirs(const char *path, const struct stat *statptr, int fileflags, struct FTW *pftw)
{
    const size_t path_len = strlen(path);
    if(fileflags == FTW_F)
    {
        struct stat path_stat;
        const size_t offset = 0, archivepaths_offset = 0, size = ((0 == stat(path, &path_stat)) ? path_stat.st_size : 0);
        packfs_dynamic_add_file("", 0, path, path_len, size, offset, archivepaths_offset);
    }
    else if(fileflags == FTW_D)
        packfs_dynamic_add_dirname("", 0, path, path_len, true);
    return 0;
}

int packfs_scan_path(const char* input_path, bool isdir)
{
    if(PACKFS_EMPTY(input_path)) return PACKFS_ERROR;

    int res = PACKFS_OK;
    if(isdir)
    {
        const int fd_limit = 20;
        const int flags = 0;
        res = nftw(input_path, packfs_list_files_dirs, fd_limit, flags);
    }
    else
    {
        const char* last_slash = strrchr(input_path, packfs_sep);
        const size_t basename_offset = last_slash != NULL ? (last_slash - input_path + 1) : 0;
        const size_t basename_len = strlen(input_path) - basename_offset;
        struct stat path_stat;
        const size_t offset = 0, archivepaths_offset = 0, size = ((0 == stat(input_path, &path_stat)) ? path_stat.st_size : 0);
        packfs_dynamic_add_file("", 0, input_path + basename_offset, basename_len, size, offset, archivepaths_offset);
    }
    return res;
}

int packfs_scan_listing(FILE* fileptr, const char* packfs_listing_filename, const char* prefix, const char* prefix_archive)
{
    size_t prefix_len_m1 = prefix != NULL ? packfs_path_len(prefix) : 0;
    if(prefix_len_m1 > 0 && prefix[prefix_len_m1 - 1] == packfs_sep) prefix_len_m1--;

    const char* packfs_archive_filename = packfs_listing_filename;
    const size_t packfs_archive_filename_len = strlen(packfs_listing_filename) - strlen(packfs_listing_ext);
    
    packfs_dynamic_add_dirname(prefix, prefix_len_m1, "", 0, true); 
    packfs_dynamic_add_prefix(prefix, prefix_len_m1);
    
    const size_t archivepaths_offset = packfs_dynamic_archive_paths_len;

    strncpy(packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_len, packfs_archive_filename, packfs_archive_filename_len);
    packfs_dynamic_archive_paths[packfs_dynamic_archive_paths_len + packfs_archive_filename_len] = packfs_pathsep;
    packfs_dynamic_archive_paths_len += packfs_archive_filename_len + 1;
        
    {
        char entrypath[packfs_path_max];
        size_t size, offset, entrypath_len;
        fscanf(fileptr, "[\n");
        for(;;)
        {
            size = offset = 0;

            fscanf(fileptr, "{\n");
            const int ret = fscanf(fileptr, "\"path\"\n:\n\"%[^\"]\"", entrypath);
            if(ret != 1) break;
            entrypath_len = strlen(entrypath);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"size\"\n:\n%zu", &size);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"offset\"\n:\n%zu", &offset);
            fscanf(fileptr, "}\n");
            fscanf(fileptr, ",\n");

            const bool isdir = entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep;
            
            packfs_dynamic_add_dirname(prefix, prefix_len_m1, entrypath, entrypath_len, isdir); 
            if(!isdir)
                packfs_dynamic_add_file(prefix, prefix_len_m1, entrypath, entrypath_len, size, offset, archivepaths_offset);
        }
        fscanf(fileptr, "]\n");
    }
    return PACKFS_OK;
}

int packfs_init(const char* path, const char* packfs_config)
{ 
    if(packfs_initialized != 1)
    {
        packfs_init__real();
        packfs_initialized = true;
        packfs_enabled = false;
    }
    
    if(packfs_initialized == true && packfs_enabled == false)
    {
        const char* packfs_disabled = getenv("PACKFS_DISABLED");
        if(packfs_disabled != NULL && packfs_disabled[0] == '1' && packfs_disabled[1] == '\0')
            packfs_enabled = true;
    }

    if(packfs_initialized == true && packfs_enabled == false)
    {
        if(packfs_config == NULL)
            packfs_config = getenv("PACKFS_CONFIG");
        
        if(packfs_config != NULL && packfs_config[0] != '\0')
        {
            PACKFS_SPLIT(packfs_config, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
            {
                char path_normalized[packfs_path_max] = {0}; strncpy(path_normalized, entryabspath, entryabspath_len);

                char* at_prefix = strchr(path_normalized, packfs_atsep);
                const char* prefix = at_prefix != NULL ? (at_prefix + 1) : packfs_default_prefix;
                const size_t path_normalized_len = at_prefix == NULL ? entryabspath_len : (at_prefix - path_normalized);
                path_normalized[path_normalized_len] = '\0';
                char* at_prefixarchive = at_prefix != NULL ? strchr(prefix, packfs_atsep) : NULL;
                const char* prefix_archive = at_prefixarchive != NULL ? (at_prefixarchive + 1) : ""; 
                if(at_prefixarchive != NULL) at_prefixarchive[0] = '\0';

                const bool isdir = path_normalized_len >= 1 ? path_normalized[path_normalized_len - 1] == packfs_sep : false;
                const bool match = packfs_match_ext(path_normalized, path_normalized_len, packfs_listing_ext);
                if(match)
                {
                    FILE* fileptr = __real_fopen(path_normalized, "r");
                    if(fileptr != NULL)
                    {
                        packfs_enabled = true;
                        packfs_scan_listing(fileptr, path_normalized, prefix, prefix_archive);
                        __real_fclose(fileptr);
                    }
                }
                else if(isdir)
                {
                    DIR* dirptr = __real_opendir(path_normalized);
                    if(dirptr != NULL)
                    {
                        packfs_enabled = true;
                        packfs_scan_archive_dir(dirptr, path_normalized, path_normalized_len, prefix);
                        __real_closedir(dirptr);
                    }
                }
                else
                {
                    FILE* fileptr = __real_fopen(path_normalized, "rb");
                    if(fileptr != NULL)
                    {
                        packfs_enabled = true;
                        // something below seems to trigger again packfs_init();
                        packfs_scan_archive(fileptr, path_normalized, prefix);
                        __real_fclose(fileptr);
                    }
                }
            }
        }
        
        if(path != NULL && path[0] != '\0')
        {
            char path_normalized[packfs_path_max] = {0}; packfs_normpath(path, path_normalized, sizeof(path_normalized));
            const size_t archive_prefixlen = packfs_calc_archive_prefixlen(path_normalized, packfs_archives_ext);
            if(archive_prefixlen > 0)
            {
                path_normalized[archive_prefixlen - 1] = '\0';
                const char* prefix = path_normalized;
               
                FILE* fileptr = __real_fopen(path_normalized, "rb");
                if(fileptr != NULL)
                {
                    packfs_enabled = true;
                    // something below seems to trigger again packfs_init();
                    packfs_scan_archive(fileptr, path_normalized, prefix);
                    __real_fclose(fileptr);
                }
            }
        }
    }
    return PACKFS_OK;
}

bool packfs_path_in_range(const char* prefix, const char* path)
{
    return packfs_match_path(prefix, 0, path, 0, PACKFS_ENTRY_IN_DIR_RECURSIVELY);
}

bool packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_descr_fd_min && fd < packfs_descr_fd_max;
}

void* packfs_find(int fd, void* ptr)
{
    if(ptr != NULL)
    {
        for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
        {
            if(packfs_descr_fileptr[k] == ptr)
                return &packfs_descr_fd[k];
        }
        return NULL;
    }
    else
    {
        if(!packfs_fd_in_range(fd))
            return NULL;
        
        for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
        {
            if(packfs_descr_fd[k] == fd)
                return packfs_descr_fileptr[k];
        }
    }
    return NULL;
}

int packfs_resolve_relative_path(char* dest, int dirfd, const char* path)
{
    #define PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path) \
    { \
        path = (strlen(path) > 1 && path[0] == packfs_extsep && path[1] == packfs_sep) ? (path + 2) : path; \
        const size_t path_len = strlen(path); \
        strncpy(dest, entryabspath, entryabspath_len); \
        strncpy(dest + entryabspath_len, path, path_len); \
        dest[entryabspath_len + path_len] = '\0'; \
    }

    size_t d_ino = 0;
    bool found = false;
    for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
    {
        if(packfs_descr_fd[k] == dirfd)
        {
            d_ino = packfs_descr_ino[k];
            found = true;
            break;
        }
    }
    
    PACKFS_SPLIT(packfs_static_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        if(i == d_ino - packfs_static_ino_offset)
        {
            PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path)
            return PACKFS_OK;
        }
    }
    
    PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        if(i == d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset)
        {
            PACKFS_CONCAT_PATH(dest, entryabspath, entryabspath_len, path)
            return PACKFS_OK;
        }
    }

    strcpy(dest, path);
    return PACKFS_OK;
    #undef PACKFS_CAT_PATH
}

void* packfs_readdir(void* stream)
{
    #define PACKFS_FILL_DIRENT(dir_entry, d_type_val, d_ino_val, entryabspath, entryabspath_len) \
    { \
        char path_normalized[packfs_path_max] = {0}; strncpy(path_normalized, entryabspath, entryabspath_len); \
        const int has_trailing_slash = path_normalized[(entryabspath_len) - 1] == packfs_sep; \
        path_normalized[has_trailing_slash ? (entryabspath_len - 1) : (entryabspath_len)] = '\0'; \
        const char* last_slash = strrchr(path_normalized, packfs_sep); \
        const size_t basename_offset = last_slash != NULL ? (last_slash - path_normalized + 1) : 0; \
        const size_t basename_len = (entryabspath_len - has_trailing_slash) - basename_offset; \
        strncpy(dir_entry->d_name, entryabspath + basename_offset, basename_len); \
        dir_entry->d_name[basename_len] = '\0'; \
        dir_entry->d_type = d_type_val; \
        dir_entry->d_ino = ((ino_t)(d_ino_val)); \
    }
    
    // dir_entry->d_ino is abused to mean shifted index in dynamic/static, then in corresponding dir-list, then in file-list
    // dir_entry->d_off is abused to mean shifted offset in dynamic dirpath
    
    struct dirent* dir_entry = stream;
    size_t d_ino = (size_t)dir_entry->d_ino;

    if(d_ino >= packfs_static_ino_offset && d_ino < packfs_dynamic_ino_offset)
    {
        const int check_dirs = (d_ino >= packfs_static_ino_offset + packfs_dirs_ino_offset) && (d_ino < packfs_static_ino_offset + packfs_dirs_ino_offset + packfs_dirs_ino_offset);
        int check_files = (d_ino >= packfs_static_ino_offset) && (d_ino < packfs_static_ino_offset + packfs_dirs_ino_offset);
        const char* dirabspath = packfs_static_dirs_paths + (size_t)dir_entry->d_off;
        size_t dirabspath_len = packfs_path_len(dirabspath);
        
        PACKFS_SPLIT(packfs_static_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            const int i_match = i > (d_ino - packfs_static_ino_offset - packfs_dirs_ino_offset);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_DIR, packfs_static_ino_offset + packfs_dirs_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }

        if(check_dirs)
        {
            check_files = true;
            d_ino = packfs_static_ino_offset;
        }
        
        PACKFS_SPLIT(packfs_static_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            const int i_match = (i > d_ino - packfs_static_ino_offset) || (i == 0 && check_dirs);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_REG, packfs_static_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }
    }
    else if(d_ino >= packfs_dynamic_ino_offset)
    {
        const int check_dirs = (d_ino >= packfs_dynamic_ino_offset + packfs_dirs_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset + packfs_dirs_ino_offset);
        int check_files = (d_ino >= packfs_dynamic_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset);
        const char* dirabspath = packfs_dynamic_dirs_paths + (size_t)dir_entry->d_off;
        const size_t dirabspath_len = packfs_path_len(dirabspath);
        
        PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            const int i_match = (i > d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_DIR, packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }

        if(check_dirs)
        {
            check_files = true;
            d_ino = packfs_dynamic_ino_offset;
        }
        
        PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(dirabspath, dirabspath_len, entryabspath, entryabspath_len, PACKFS_ENTRY_IN_DIR);
            const int i_match = (i > d_ino - packfs_dynamic_ino_offset) || (i == 0 && check_dirs);
            if(i_match && match)
            {
                PACKFS_FILL_DIRENT(dir_entry, DT_REG, packfs_dynamic_ino_offset + i, entryabspath, entryabspath_len)
                return dir_entry;
            }
        }
    }
    return NULL;
    #undef PACKFS_FILL_DIRENT
}

int packfs_access(const char* path)
{
    char path_normalized[packfs_path_max] = {0}; packfs_normpath(path, path_normalized, sizeof(path_normalized)); size_t path_normalized_len = strlen(path_normalized);
    bool path_in_range = false;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        path_in_range = true;
        PACKFS_SPLIT(packfs_static_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
                return PACKFS_OK;
        }
    }
        
    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = true;
        PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
                return PACKFS_OK;
        }
    }

    return path_in_range ? PACKFS_ERROR_BAD : PACKFS_ERROR_NOTINRANGE;
}

int packfs_stat(const char* path, int fd, bool* isdir, size_t* size, size_t* d_ino)
{
    if(PACKFS_EMPTY(path)) return PACKFS_ERROR_NOTINRANGE;
    
    char path_normalized[packfs_path_max] = {0}; packfs_normpath(path, path_normalized, sizeof(path_normalized)); size_t path_normalized_len = strlen(path_normalized);
    bool path_in_range = false;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        path_in_range = true;
        PACKFS_SPLIT(packfs_static_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                *size = 0;
                *isdir = true;
                *d_ino = packfs_static_ino_offset + packfs_dirs_ino_offset + i;
                return PACKFS_OK;
            }
        }
        
        PACKFS_SPLIT(packfs_static_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                *size = packfs_static_files_sizes[i]; 
                *isdir = false;
                *d_ino = packfs_static_ino_offset + i;
                return PACKFS_OK;
            }
        }
    }

    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = true;
        PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                *size = 0;
                *isdir = true;
                *d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                return PACKFS_OK;
            }
        }
    
        PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
        {
            const bool match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                *size = packfs_dynamic_files_sizes[i];
                *isdir = false;
                *d_ino = packfs_dynamic_ino_offset + i;
                return PACKFS_OK;
            }
        }
    }

    if(path_in_range)
        return PACKFS_ERROR_BAD;
    
    if(packfs_fd_in_range(fd))
    {
        for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
        {
            const bool match = packfs_descr_fd[k] == fd; 
            if(match)
            {
                *size = packfs_descr_size[k];
                *isdir = packfs_descr_isdir[k];
                *d_ino = packfs_descr_ino[k];
                return PACKFS_OK;
            }
        }
        return PACKFS_ERROR_BAD;
    }

    return PACKFS_ERROR_NOTINRANGE;
}

void* packfs_open(const char* path, int flags)
{
    char path_normalized[packfs_path_max] = {0}; packfs_normpath(path, path_normalized, sizeof(path_normalized)); size_t path_normalized_len = strlen(path_normalized);
    bool path_in_range = false;
    bool isdir = false;

    void* fileptr = NULL; 
    size_t size = 0, d_ino = 0, d_off = 0;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized))
    {
        PACKFS_SPLIT(packfs_static_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast) 
        {
            const bool match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                path_in_range = true;
                isdir = true;
                d_ino = packfs_static_ino_offset + packfs_dirs_ino_offset + i;
                d_off = entryabspath - packfs_static_dirs_paths;
                size = 0;
            }
        }
        
        PACKFS_SPLIT(packfs_static_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast) 
        {
            const bool match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_FILE_MATCHES);
            if(match)
            {
                path_in_range = true;
                isdir = false;
                d_ino = packfs_static_ino_offset + i;
                d_off = 0;
                
                size = packfs_static_files_sizes[i];
                fileptr = fmemopen((void*)_binary_packfs_static_end, size, "r");
            }
        }
    }

    if(packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        path_in_range = true;
        PACKFS_SPLIT(packfs_dynamic_dirs_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast) 
        {
            const bool match = packfs_match_path(entryabspath, entryabspath_len, path_normalized, path_normalized_len, PACKFS_DIR_EXISTS);
            if(match)
            {
                path_in_range = true;
                isdir = true;
                d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                d_off = entryabspath - packfs_dynamic_dirs_paths;
                size = 0;
            }
        }

        PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast) 
        {
            const size_t prefix_len = packfs_dynamic_files_paths_prefixlen[i];
            
            const char* archivepath = packfs_dynamic_archive_paths + packfs_dynamic_archive_paths_offset[i];
            const char* entrypath = entryabspath + prefix_len;
            const size_t entrypath_len = packfs_path_len(entrypath);
            const bool match = packfs_match_path(path_normalized, path_normalized_len, entryabspath, entryabspath_len, PACKFS_FILE_MATCHES);

            if(match)
            {
                path_in_range = true;
                isdir = false;
                d_ino = packfs_dynamic_ino_offset + i;
                d_off = 0;
                
                size = packfs_dynamic_files_sizes[i];
                fileptr = fmemopen(NULL, size, "rb+");
                
                char archivepath_[packfs_path_max] = {0}; strncpy(archivepath_, archivepath, packfs_path_len(archivepath));

                FILE* packfs_archive_fileptr = __real_fopen(archivepath_, "rb");//packfs_archive_fileptr;
                if(packfs_archive_fileptr != NULL)
                {
                    packfs_extract_archive_entry_from_FILE_to_FILE(packfs_archive_fileptr, entrypath, entrypath_len, (FILE*)fileptr);
                    __real_fclose(packfs_archive_fileptr);
                }
                fseek((FILE*)fileptr, 0, SEEK_SET);
                break;
            }
        }
    }

    for(size_t k = 0; path_in_range && k < packfs_descr_fd_cnt; k++)
    {
        if(packfs_descr_fd[k] == 0)
        {
            const int fd = packfs_descr_fd_min + k;
            
            if(isdir)
            {
                packfs_descr_dirent [k] = (struct dirent){0};
                packfs_descr_dirent [k].d_ino = (ino_t)d_ino;
                packfs_descr_dirent [k].d_off = (off_t)d_off;
                packfs_descr_fileptr[k] = &packfs_descr_dirent[k];
            }
            else
            {
                packfs_descr_fileptr[k] = fileptr;
            }

            packfs_descr_isdir[k] = isdir;
            packfs_descr_refs [k] = 1;
            packfs_descr_fd   [k] = fd;
            packfs_descr_size [k] = size;
            packfs_descr_ino  [k] = d_ino;

            return packfs_descr_fileptr[k];
        }
    }

    return NULL;
}

int packfs_close(int fd)
{
    if(!packfs_fd_in_range(fd))
        return PACKFS_ERROR_NOTINRANGE;

    for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
    {
        if(packfs_descr_fd[k] == fd)
        {
            packfs_descr_refs[k]--;
            if(packfs_descr_refs[k] > 0)
                return PACKFS_OK;

            const int res = (!packfs_descr_isdir[k]) ? __real_fclose(packfs_descr_fileptr[k]) : 0;
            packfs_descr_dirent[k]  = (struct dirent){0};
            packfs_descr_isdir[k] = false;
            packfs_descr_fd[k] = 0;
            packfs_descr_size[k] = 0;
            packfs_descr_fileptr[k] = NULL;
            packfs_descr_ino[k] = 0;
            return res;
        }
    }
    return PACKFS_ERROR_BAD;
}

ssize_t packfs_read(int fd, void* buf, size_t count)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return PACKFS_ERROR_BAD;
    return fread(buf, 1, count, ptr);
}

int packfs_seek(int fd, long offset, int whence)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return PACKFS_ERROR_BAD;
    return fseek(ptr, offset, whence);
}

int packfs_dup(int oldfd, int newfd)
{
    size_t K = -1;
    if(oldfd >= 0 && packfs_descr_fd_min <= oldfd && oldfd < packfs_descr_fd_max)
    {
        for(size_t k = 0; k < packfs_descr_fd_cnt; k++)
        {
            if(packfs_descr_fd[k] == oldfd)
            {
                K = k;
                break;
            }
        }
    }
    for(size_t k = 0; K >= 0 && k < packfs_descr_fd_cnt; k++)
    {
        const int fd = packfs_descr_fd_min + k;
        if(packfs_descr_fd[k] == 0 && (newfd < packfs_descr_fd_min || newfd >= fd))
        {
            packfs_descr_refs[K]++;
            
            packfs_descr_isdir[k] = packfs_descr_isdir[K];
            packfs_descr_fd[k]        = fd;
            packfs_descr_refs[k]    = 1;
            packfs_descr_size[k]  = packfs_descr_size[K];
            packfs_descr_ino[k]   = packfs_descr_ino[K];
            packfs_descr_dirent[k]    = packfs_descr_dirent[K];
            packfs_descr_fileptr[k]   = packfs_descr_fileptr[K];
            return fd;
        }
    }
    return PACKFS_ERROR_BAD;
    
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////

FILE* PACKFS_WRAP(fopen)(const char *path, const char *mode)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        FILE* res = packfs_open(path, 0);
        if(res != NULL)
            return res;
    }
    return __real_fopen(path, mode);
}

int PACKFS_WRAP(fileno)(FILE *stream)
{
    packfs_init(NULL, NULL);
    int res = __real_fileno(stream);
    if(packfs_enabled && res < 0)
    {        
        int* ptr = packfs_find(-1, stream);
        res = ptr == NULL ? PACKFS_ERROR_BAD : (*ptr);
    }
    return res;
}

int PACKFS_WRAP(fclose)(FILE* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        const int* ptr = packfs_find(-1, stream);
        const int fd = ptr == NULL ? PACKFS_ERROR_BAD : *ptr;
        const int res = packfs_close(fd);
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    return __real_fclose(stream);
}

int PACKFS_WRAP(open)(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }
    
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        void* stream = packfs_open(path, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            const int* ptr = packfs_find(-1, stream);
            const int res = ptr == NULL ? PACKFS_ERROR_BAD : (*ptr);
            return res;
        }
    }

    return __real_open(path, flags, mode);
}

int PACKFS_WRAP(openat)(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    if((flags & O_CREAT) != 0 || (flags & O_TMPFILE) != 0)
    {
        va_list arg;
        va_start(arg, flags);
        mode = va_arg(arg, mode_t);
        va_end(arg);
    }

    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        void* stream = packfs_open(path_normalized, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            const int* ptr = packfs_find(-1, stream);
            const int res = ptr == NULL ? PACKFS_ERROR_BAD : (*ptr);
            return res;
        }
    }
    
    return __real_openat(dirfd, path, flags, mode);
}

int PACKFS_WRAP(close)(int fd)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        const int res = packfs_close(fd);
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    return __real_close(fd);
}

ssize_t PACKFS_WRAP(read)(int fd, void* buf, size_t count)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        const ssize_t res = packfs_read(fd, buf, count);
        if(res >= 0)
            return res;
    }
    return __real_read(fd, buf, count);
}

off_t PACKFS_WRAP(lseek)(int fd, off_t offset, int whence)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        const int res = packfs_seek(fd, (long)offset, whence);
        if(res >= 0)
            return res;
    }
    return __real_lseek(fd, offset, whence);
}

int PACKFS_WRAP(access)(const char *path, int flags)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        const int res = packfs_access(path);
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    return __real_access(path, flags); 
}

int PACKFS_WRAP(stat)(const char *restrict path, struct stat *restrict statbuf)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, d_ino = 0;
        bool isdir = false;
        const int res = packfs_stat(path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }

    return __real_stat(path, statbuf);
}

int PACKFS_WRAP(fstat)(int fd, struct stat * statbuf)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, d_ino = 0;
        bool isdir = false;
        const int res = packfs_stat(NULL, fd, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    
    return __real_fstat(fd, statbuf);
}

int PACKFS_WRAP(fstatat)(int dirfd, const char* path, struct stat * statbuf, int flags)
{
    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, d_ino = 0;
        bool isdir = false;
        const int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }

        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    
    return __real_fstatat(dirfd, path, statbuf, flags);
}

int PACKFS_WRAP(statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    packfs_init(path, NULL);
    char path_normalized[packfs_path_max]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        size_t size = 0, d_ino = 0;
        bool isdir = false;
        const int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            *statbuf = (struct statx){0};
            statbuf->stx_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->stx_size = size;
            statbuf->stx_ino = d_ino;
        }
        return res;
    }

    return __real_statx(dirfd, path, flags, mask, statbuf);
}

DIR* PACKFS_WRAP(opendir)(const char *path)
{
    packfs_init(path, NULL);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        DIR* stream = packfs_open(path, 1);
        if(stream != NULL)
            return stream;
    }
    return __real_opendir(path);
}

DIR* PACKFS_WRAP(fdopendir)(int dirfd)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_fd_in_range(dirfd))
    {
        DIR* stream = packfs_find(dirfd, NULL);
        if(stream != NULL)
            return stream;
    }
    return __real_fdopendir(dirfd);
}

struct dirent* PACKFS_WRAP(readdir)(DIR* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        const int* ptr = packfs_find(-1, stream);
        if(ptr != NULL)
            return (struct dirent*)packfs_readdir(stream);
    }
    return __real_readdir(stream);
}

int PACKFS_WRAP(closedir)(DIR* stream)
{
    packfs_init(NULL, NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {        
        const int* ptr = packfs_find(-1, stream);
        const int fd = ptr == NULL ? PACKFS_ERROR_BAD : *ptr;
        const int res = packfs_close(fd);
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    return __real_closedir(stream);
}

int PACKFS_WRAP(fcntl)(int fd, int action, ...)
{
    int intarg = -1;
    void* ptrarg = NULL;
    char argtype = ' ';
    va_list arg;
    va_start(arg, action);
    switch(action)
    {
        case F_GETFD:
        case F_GETFL:
        case F_GETLEASE:
        case F_GETOWN:
        case F_GETPIPE_SZ:
        case F_GETSIG:
        case F_GET_SEALS: // linux-specific
        {
            argtype = ' ';
            break;
        }
        case F_ADD_SEALS: // linux-specific
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_NOTIFY:
        case F_SETFD:
        case F_SETFL:
        case F_SETLEASE:
        case F_SETOWN:
        case F_SETPIPE_SZ:
        case F_SETSIG:
        {
            intarg = va_arg(arg, int);
            argtype = '0';
            break;
        }
        default:
        {
            ptrarg = va_arg(arg, void*);
            argtype = '*';
            break;
        }
    }
    va_end(arg);

    packfs_init(NULL, NULL);
    
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        const int res = (argtype == '0' && (action == F_DUPFD || action == F_DUPFD_CLOEXEC)) ? packfs_dup(fd, intarg) : PACKFS_ERROR_BAD;
        if(res == PACKFS_ERROR_BAD || res >= PACKFS_OK)
            return res;
    }
    
    return (argtype == '0' ? __real_fcntl(fd, action, intarg) : argtype == '*' ? __real_fcntl(fd, action, ptrarg) : __real_fcntl(fd, action));
}

int packfs_cat_files_offsets(const char* output_path)
{
    int res = PACKFS_OK; 
    FILE* fileptr = fopen(output_path, "w");
    if(!fileptr) { res = 1; fprintf(stderr, "#could not open output file: %s\n", packfs_static); return res; }
    size_t offset = 0, size = 0;
    PACKFS_SPLIT(packfs_dynamic_files_paths, packfs_pathsep, entryabspath, entryabspath_len, prefix_len, i, islast)
    {
        char tmp_path[packfs_path_max];
        snprintf(tmp_path, sizeof(tmp_path), "%.*s", (int)entryabspath_len, entryabspath);

        size = 0;
        FILE* h = fopen(tmp_path, "r");
        for(;;)
        {
            const size_t bytes_read = fread(tmp_path, 1, sizeof(tmp_path), h);
            if(bytes_read == 0)
                break;
            size += bytes_read;
            fwrite(tmp_path, 1, bytes_read, fileptr);
        }
        fclose(h);
        packfs_dynamic_files_offsets[i] = offset;
        offset += size;
    }
    fclose(fileptr);
    return res;
}

#ifdef PACKFS_STATIC_PACKER

int main(int argc, const char **argv)
{
    // https://github.com/libarchive/libarchive/issues/2283
    
    const char* input_path = NULL;
    const char* output_path = "packfs_static.h";
    const char* prefix = packfs_default_prefix;
    const char* ld = "ld";
    const char* include = NULL;
    const char* exclude = NULL;
    int listing = false, object = false;

    for(int i = 1; i < argc; i++)
    {
        if(0 == strcmp("--input-path", argv[i]))
            input_path = argv[++i];
        else if(0 == strcmp("--output-path", argv[i]))
            output_path = argv[++i];
        else if(0 == strcmp("--prefix", argv[i]))
            prefix = argv[++i];
        else if(0 == strcmp("--ld", argv[i]))
            ld = argv[++i];
        else if(0 == strcmp("--include", argv[i]))
            include = argv[++i];
        else if(0 == strcmp("--exclude", argv[i]))
            exclude = argv[++i];
        else if(0 == strcmp("--listing", argv[i]))
            listing = 1;
        else if(0 == strcmp("--object", argv[i]))
            object = 1;
    }
    
    int res = PACKFS_OK;
    struct stat path_stat;
    if(input_path == NULL || 0 != stat(input_path, &path_stat)) { res = 1; fprintf(stderr, "Input path not specified or does not exist\n"); return res; }
    bool isfile = S_ISREG(path_stat.st_mode), isdir = S_ISDIR(path_stat.st_mode);
    if(!isfile && !isdir) { res = 1; fprintf(stderr, "Input path not file or dir\n"); return res; }
    
    const char* removeprefix = "";
    const char* header_path = NULL;
    const char* package_path = NULL;
    int removepackage = false;
    
    char tmp_path[packfs_path_max];
    
    if(listing)
    {
        fprintf(stderr, "%s\n", input_path);

        FILE* fileptr = fopen(output_path, "w");
        if(!fileptr) { res = 1; fprintf(stderr, "#could not open output file: %s\n", output_path); return res; }
        res = packfs_scan_archive(fileptr, input_path, prefix); removeprefix = input_path;
        fclose(fileptr);

        packfs_dump_listing(removeprefix, output_path);

        if(object)
        {
            snprintf(tmp_path, sizeof(tmp_path), "%s.h", output_path);

            removepackage = false;
            package_path = input_path;
            header_path = tmp_path;
        }
    }
    else if(object)
    {
        fprintf(stderr, "%s\n", input_path);

        res = packfs_scan_path(input_path, isdir); removeprefix = isdir ? input_path : "";
        res = packfs_cat_files_offsets(packfs_static);

        removepackage = true;
        package_path = packfs_static;
        header_path = output_path;
    }

    if(object)
    {
        res = packfs_dump_static_package(prefix, removeprefix, header_path, ld, package_path);
        fprintf(stderr, "%s\n", header_path);
        fprintf(stderr, "%s.o\n", header_path);

        if(removepackage)
        {
            res = unlink(package_path);
            if(res != 0) { fprintf(stderr, "#could not delete: %s\n", package_path); return res; }
        }
    }
    return res;
}

#endif
