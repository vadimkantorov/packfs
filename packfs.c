// TODO: support working with linked zip
// TODO: use trailing slash as dir indicator
// TODO: where do path normalization?
// TODO: use safe string functions everywhere

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#define PACKFS_STRING_VALUE_(x) #x
#define PACKFS_STRING_VALUE(x) PACKFS_STRING_VALUE_(x)
#define PACKFS_CONCAT_(X, Y) X ## Y
#define PACKFS_CONCAT(X, Y) PACKFS_CONCAT_(X, Y)

#ifdef  PACKFS_DYNAMIC_LINKING
#define PACKFS_EXTERN(x)       (*x)
#define PACKFS_WRAP(x)         ( x)
#else
#define PACKFS_EXTERN(x) extern( x)
#define PACKFS_WRAP(x) PACKFS_CONCAT(__wrap_, x)
#endif

int                  PACKFS_EXTERN(__real_open)         (const char *path, int flags, ...);
int                  PACKFS_EXTERN(__real_openat)       (int dirfd, const char *path, int flags, ...);
int                  PACKFS_EXTERN(__real_close)        (int fd);
ssize_t              PACKFS_EXTERN(__real_read)         (int fd, void* buf, size_t count);
int                  PACKFS_EXTERN(__real_access)       (const char *path, int flags);
off_t                PACKFS_EXTERN(__real_lseek)        (int fd, off_t offset, int whence);
int                  PACKFS_EXTERN(__real_stat)         (const char *restrict path, struct stat *restrict statbuf);
int                  PACKFS_EXTERN(__real_fstat)        (int fd, struct stat * statbuf);
int                  PACKFS_EXTERN(__real_fstatat)      (int dirfd, const char* path, struct stat * statbuf, int flags);
int                  PACKFS_EXTERN(__real_statx)        (int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf);
FILE*                PACKFS_EXTERN(__real_fopen)        (const char *path, const char *mode);
int                  PACKFS_EXTERN(__real_fclose)       (FILE* stream);
int                  PACKFS_EXTERN(__real_fileno)       (FILE* stream);
int                  PACKFS_EXTERN(__real_fcntl)        (int fd, int action, ...);
DIR*                 PACKFS_EXTERN(__real_opendir)      (const char *path);
DIR*                 PACKFS_EXTERN(__real_fdopendir)    (int dirfd);
int                  PACKFS_EXTERN(__real_closedir)     (DIR *dirp);
struct dirent*       PACKFS_EXTERN(__real_readdir)      (DIR *dirp);

void packfs_init__real()
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
#endif
}

enum
{
    packfs_filefd_min = 1000000000, 
    packfs_filefd_max = 1000008192, 
    packfs_files_name_maxlen = 128, 
    packfs_dynamic_files_nummax = 8192,
    packfs_static_ino_offset = 1000000000,
    packfs_dynamic_ino_offset = 2000000000,
    packfs_dirs_ino_offset = 1000000,
};

enum
{
    packfs_sep = '/',
    packfs_pathsep = ':'
};

#ifdef PACKFS_STATIC
#include "packfs.h"
#else
char   packfs_static_prefix[1];
size_t packfs_static_files_num, packfs_static_dirs_num;
const char** packfs_static_paths; 
const char** packfs_static_dirpaths; 
const char** packfs_static_starts; 
const char** packfs_static_ends;
#endif

int packfs_initialized, packfs_enabled;
int             packfs_filefd           [packfs_filefd_max - packfs_filefd_min];
int             packfs_filefdrefs       [packfs_filefd_max - packfs_filefd_min];
char            packfs_fileisdir        [packfs_filefd_max - packfs_filefd_min];
void*           packfs_fileptr          [packfs_filefd_max - packfs_filefd_min];
size_t          packfs_filesize         [packfs_filefd_max - packfs_filefd_min];
size_t          packfs_fileino          [packfs_filefd_max - packfs_filefd_min];
struct dirent   packfs_dirent           [packfs_filefd_max - packfs_filefd_min];


char   packfs_default_prefix[] = "/packfs";
char   packfs_dynamic_prefix      [packfs_dynamic_files_nummax * packfs_files_name_maxlen];
char   packfs_dynamic_archivepaths[packfs_dynamic_files_nummax * packfs_files_name_maxlen]; size_t packfs_dynamic_archivepaths_total;
char   packfs_dynamic_paths       [packfs_dynamic_files_nummax * packfs_files_name_maxlen]; size_t packfs_dynamic_paths_total;
char   packfs_dynamic_dirpaths    [packfs_dynamic_files_nummax * packfs_files_name_maxlen]; size_t packfs_dynamic_dirpaths_total;

size_t packfs_dynamic_files_num, packfs_dynamic_dirs_num;
size_t packfs_dynamic_files_sizes[packfs_dynamic_files_nummax];
size_t packfs_dynamic_files_archiveoffset[packfs_dynamic_files_nummax];
size_t packfs_dynamic_paths_prefixlen[packfs_dynamic_files_nummax];

void packfs_normalize_path(char* path_normalized, const char* path)
{
    size_t len = path != NULL ? strlen(path) : 0;
    if(len == 0)
        path_normalized[0] = '\0';

    // lstrips ./ in the beginning; collapses double consecutive slashes; and rstrips abc/asd/..
    for(int i = (path != NULL && len > 2 && path[0] == '.' && path[1] == packfs_sep) ? 2 : 0, k = 0; len > 0 && i < len; i++)
    {
        if(!(i > 1 && path[i] == packfs_sep && path[i - 1] == packfs_sep))
        {
            path_normalized[k++] = path[i];
            path_normalized[k] = '\0';
        }
    }
    
    size_t path_normalized_len = strlen(path_normalized);
    if(path_normalized_len >= 3 && path_normalized[path_normalized_len - 1] == '.' && path_normalized[path_normalized_len - 2] == '.'  && path_normalized[path_normalized_len - 3] == packfs_sep)
    {
        path_normalized[path_normalized_len - 3] = '\0';
        char* trailing_slash = strrchr(path_normalized, packfs_sep);
        if(trailing_slash != NULL)
            *trailing_slash = '\0';
    }
}

int packfs_path_in_range(const char* prefixes, const char* path)
{
    if(prefixes == NULL || prefixes[0] == '\0' || path == NULL || path[0] == '\0')
        return 0;
    size_t path_len = strlen(path);
    
    for(const char* begin = prefixes, *end = strchr(prefixes, packfs_pathsep), *prevend  = prefixes; prevend != NULL; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
    {
        size_t prefix_len = end == NULL ? strlen(begin) : (end - begin);
        
        int prefix_trailing_slash = begin[prefix_len - 1] == packfs_sep;
        int prefix_ok = 0 == strncmp(begin, path, prefix_len - prefix_trailing_slash);
        size_t prefix_len_m1 = prefix_len - prefix_trailing_slash;
        if(prefix_ok && ((path_len == prefix_len_m1) || (path_len >= prefix_len && path[prefix_len_m1] == packfs_sep)))
            return 1;
    }
    return 0;
}

size_t packfs_archive_prefix_extract(const char* path, const char* suffixes)
{
    if(path == NULL || suffixes == NULL || suffixes[0] == '\0')
        return 0;
    for(const char* res = strchr(path, packfs_sep), *prevres = path; prevres != NULL; prevres = res, res = (res != NULL ? strchr(res + 1, packfs_sep) : NULL))
    {
        size_t prefix_len = res == NULL ? strlen(path) : (res - path);
        for(const char* begin = suffixes, *end = strchr(suffixes, packfs_pathsep), *prevend  = suffixes; prevend != NULL; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
        {
            size_t suffix_len = end == NULL ? strlen(begin) : (end - begin);
            if(suffix_len > 0 && prefix_len >= suffix_len && 0 == strncmp(begin, path + prefix_len - suffix_len, suffix_len))
                return prefix_len;
        }
    }
    return 0;
}


///////////

#include <archive.h>
#include <archive_entry.h>

const char* packfs_archive_read_new(void* ptr)
{
    static char packfs_archive_suffix[] = ".iso:.zip:.tar:.tar.gz:.tar.xz";
    if(ptr != NULL)
    {
        struct archive* a = ptr;
        archive_read_support_format_iso9660(a);
        archive_read_support_format_zip(a);
        archive_read_support_format_tar(a);
        archive_read_support_filter_gzip(a);
        archive_read_support_filter_xz(a);
    }
    return packfs_archive_suffix;
}

int packfs_dir_exists(const char* prefix, const char* path)
{
    /*
    for(size_t i = (packfs_path_in_range(packfs_static_prefix, prefix) ? 0 : packfs_static_files_num); i < packfs_static_files_num; i++)
    {
        const char* entrypath = packfs_static_paths[i];
        int entryisdir = entrypath[0] != '\0' && entrypath[strlen(entrypath) - 1] == packfs_sep;
        if(entryisdir && 0 == strcmp(path, entrypath))
            return 1;
    }
    */
    
    size_t prefix_len = strlen(prefix);
    
    for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, prefix) ? 0 : packfs_dynamic_dirs_num), offset = 0; i < packfs_dynamic_dirs_num; offset += (strlen(packfs_dynamic_dirpaths  + offset) + 1), i++)
    {
        const char* entryabspath = packfs_dynamic_dirpaths + offset;
        if(0 == strncmp(prefix, entryabspath, prefix_len) && entryabspath[prefix_len] == packfs_sep && 0 == strcmp(entryabspath + prefix_len + 1, path))
            return 1;
    }
    
    return 0;
}

void packfs_add_path(char* prefixes, const char* prefix)
{
    size_t prefixes_len = strlen(prefixes);
    if(prefixes[0] == '\0')
        strcpy(prefixes, prefix);
    else
    {
        prefixes[prefixes_len] = packfs_pathsep;
        prefixes[prefixes_len + 1] = '\0';
        strcat(prefixes, prefix);
    }
}

void packfs_scan_listing(FILE* fileptr, const char* packfs_listing_filename, const char* prefix)
{
    size_t prefix_len = prefix != NULL ? strlen(prefix) : 0;
    if(prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep) prefix_len--;
    const char* packfs_archive_filename = packfs_listing_filename;
    size_t packfs_archive_filename_len = strlen(packfs_listing_filename) - strlen(".json");
    
    //FIXME: adds prefix even if input archive cannot be opened | do not scan the same archive second time
    packfs_add_path(packfs_dynamic_prefix, prefix);
    
    size_t archive_offset = packfs_dynamic_archivepaths_total;
    strncpy(packfs_dynamic_archivepaths + packfs_dynamic_archivepaths_total, packfs_archive_filename, packfs_archive_filename_len);
    fprintf(stderr, "packfs_scan_listing1 '%s' '%s' '%s'\n", packfs_listing_filename, prefix, packfs_dynamic_archivepaths + packfs_dynamic_archivepaths_total);
    
    packfs_dynamic_archivepaths_total += packfs_archive_filename_len + 1;
        
    {
        if(!packfs_dir_exists(prefix, ""))
        {
            const char* full_path = packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total;
            strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, prefix, prefix_len);
            packfs_dynamic_dirpaths_total += prefix_len;
            packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total] = packfs_sep;
            packfs_dynamic_dirpaths_total += 2;
            packfs_dynamic_dirs_num++;
        }
    
        char entrypath[packfs_files_name_maxlen];
        size_t entrysize, entryoffset, entrypath_len;
        fscanf(fileptr, "[\n");
        while(1)
        {
            entrysize = entryoffset = 0;

            fscanf(fileptr, "{\n");
            int ret = fscanf(fileptr, "\"path\"\n:\n\"%[^\"]\"", entrypath);
            if(ret != 1) break;
            entrypath_len = strlen(entrypath);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"size\"\n:\n%zu", &entrysize);
            fscanf(fileptr, ",\n");
            fscanf(fileptr, "\"offset\"\n:\n%zu", &entryoffset);
            fscanf(fileptr, "}\n");
            fscanf(fileptr, ",\n");

            int entryisdir = entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep;
            int entryisfile = !entryisdir;
    
            fprintf(stderr, "packfs_scan_listing11 '%s' %zu\n", entrypath, entrysize);
            
            if(entryisdir && !packfs_dir_exists(prefix, entrypath))
            {
                const char* full_path = packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total;
                strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, prefix, prefix_len);
                packfs_dynamic_dirpaths_total += prefix_len;
                packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total] = packfs_sep;
                packfs_dynamic_dirpaths_total++;
                strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, entrypath, entrypath_len);
                if(entrypath_len == 0 || entrypath[entrypath_len - 1] != packfs_sep) entrypath_len++;
                packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total + (entrypath_len - 1)] = packfs_sep;
                packfs_dynamic_dirpaths_total += (entrypath_len) + 1;
                
                fprintf(stderr, "packfs_scan_listing1111 '%s'\n", full_path);
            
                packfs_dynamic_dirs_num++;
            }
            else if(entryisfile)
            {
                packfs_dynamic_paths_prefixlen[packfs_dynamic_files_num] = prefix_len + 1;
                
                const char* full_path = packfs_dynamic_paths + packfs_dynamic_paths_total;
                strncpy(packfs_dynamic_paths + packfs_dynamic_paths_total, prefix, prefix_len);
                packfs_dynamic_paths_total += prefix_len;
                packfs_dynamic_paths[packfs_dynamic_paths_total] = packfs_sep;
                packfs_dynamic_paths_total++;
                strncpy(packfs_dynamic_paths + packfs_dynamic_paths_total, entrypath, entrypath_len);
                if(entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep) entrypath_len--;
                packfs_dynamic_paths_total += (entrypath_len) + 1;
            
                packfs_dynamic_files_sizes[packfs_dynamic_files_num] = entrysize;
                packfs_dynamic_files_archiveoffset[packfs_dynamic_files_num] = archive_offset;
                
                fprintf(stderr, "packfs_scan_listing111 '%s' '%s'\n", full_path, archive_offset);
                packfs_dynamic_files_num++;
            }
        }
        fscanf(fileptr, "]\n");
    }
}

void packfs_scan_archive(FILE* f, struct archive* a, const char* packfs_archive_filename, const char* prefix)
{
    size_t prefix_len = prefix != NULL ? strlen(prefix) : 0;
    if(prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep) prefix_len--;
    size_t packfs_archive_filename_len = strlen(packfs_archive_filename);

    //FIXME: adds prefix even if input archive cannot be opened | do not scan the same archive second time
    packfs_add_path(packfs_dynamic_prefix, prefix);
                
    size_t archive_offset = packfs_dynamic_archivepaths_total;
    strncpy(packfs_dynamic_archivepaths + packfs_dynamic_archivepaths_total, packfs_archive_filename, packfs_archive_filename_len);
    packfs_dynamic_archivepaths_total += packfs_archive_filename_len + 1;

    struct archive_entry *entry;
    do
    {
        if(archive_read_open_FILE(a, f) != ARCHIVE_OK) //if(archive_read_open1(a) != ARCHIVE_OK)
            break;
        
        if(!packfs_dir_exists(prefix, ""))
        {
            const char* full_path = packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total;
            strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, prefix, prefix_len);
            packfs_dynamic_dirpaths_total += prefix_len;
            packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total] = packfs_sep;
            packfs_dynamic_dirpaths_total += 2;
            packfs_dynamic_dirs_num++;
        }
        
        while(1)
        {
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));
                
            int entrytype = archive_entry_filetype(entry);
            size_t entrysize = (size_t)archive_entry_size(entry);
            const char* entrypath = archive_entry_pathname(entry);
            size_t entrypath_len = strlen(entrypath);
            
            int entryisdir = entrytype == AE_IFDIR;
            int entryisfile = entrytype == AE_IFREG;

            if(entryisdir && !packfs_dir_exists(prefix, entrypath)) // TODO: execute after entrypath has trailing slash
            {
                const char* full_path = packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total;
                strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, prefix, prefix_len);
                packfs_dynamic_dirpaths_total += prefix_len;
                packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total] = packfs_sep;
                packfs_dynamic_dirpaths_total++;
                strncpy(packfs_dynamic_dirpaths + packfs_dynamic_dirpaths_total, entrypath, entrypath_len);
                if(entrypath_len == 0 || entrypath[entrypath_len - 1] != packfs_sep) entrypath_len++;
                packfs_dynamic_dirpaths[packfs_dynamic_dirpaths_total + (entrypath_len - 1)] = packfs_sep;
                packfs_dynamic_dirpaths_total += (entrypath_len) + 1;
            
                packfs_dynamic_dirs_num++;
            }
            else if(entryisfile) // TODO: execute after entrypath has trailing slash
            {
                packfs_dynamic_paths_prefixlen[packfs_dynamic_files_num] = prefix_len + 1;
                
                const char* full_path = packfs_dynamic_paths + packfs_dynamic_paths_total;
                strncpy(packfs_dynamic_paths + packfs_dynamic_paths_total, prefix, prefix_len);
                packfs_dynamic_paths_total += prefix_len;
                packfs_dynamic_paths[packfs_dynamic_paths_total] = packfs_sep;
                packfs_dynamic_paths_total++;
                strncpy(packfs_dynamic_paths + packfs_dynamic_paths_total, entrypath, entrypath_len);
                if(entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep) entrypath_len--;
                packfs_dynamic_paths_total += (entrypath_len) + 1;
            
                packfs_dynamic_files_sizes[packfs_dynamic_files_num] = entrysize;
                packfs_dynamic_files_archiveoffset[packfs_dynamic_files_num] = archive_offset;
                
                packfs_dynamic_files_num++;
            }
                
            r = archive_read_data_skip(a);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));
        }
    }
    while(0);
}

void packfs_extract_archive_entry_from_FILE_to_FILE(struct archive* a, FILE* f, const char* entrypath, FILE* h)
{
    struct archive_entry *entry;
    do
    {
        //if(archive_read_open_memory(a, buf, cnt) != ARCHIVE_OK)
        if(archive_read_open_FILE(a, f) != ARCHIVE_OK)
            break;
        
        while (1)
        {
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                break; //fprintf(stderr, "%s\n", archive_error_string(a));

            if(0 == strcmp(entrypath, archive_entry_pathname(entry)))
            {
                enum { MAX_WRITE = 1024 * 1024};
                const void *buff;
                size_t size;
                off_t offset;

                while ((r = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK)
                {
                    // assert(offset <= output_offset), do not support sparse files just yet, https://github.com/libarchive/libarchive/issues/2299
                    const char* p = buff;
                    while (size > 0)
                    {
                        ssize_t bytes_written = fwrite(p, 1, size, h);
                        p += bytes_written;
                        size -= bytes_written;
                    }
                }
                break;
            }
            else
            {
                r = archive_read_data_skip(a);
                if (r == ARCHIVE_EOF)
                    break;
                if (r != ARCHIVE_OK)
                    break; //fprintf(stderr, "%s\n", archive_error_string(a));
            }
        }
    }
    while(0);
}

///////////

void packfs_init(const char* path)
{ 
    if(packfs_initialized != 1)
    {
        packfs_init__real();
        packfs_initialized = 1;
        packfs_enabled = 0;
    }

    if(packfs_initialized == 1 && packfs_enabled == 0)
    {
        char path_normalized[packfs_files_name_maxlen]; 
        char path_normalized_entry[packfs_files_name_maxlen]; 
        
        const char* packfs_archives = getenv("PACKFS_ARCHIVES");
        const char* packfs_archivedirs = getenv("PACKFS_ARCHIVEDIRS");
        const char* packfs_listings = getenv("PACKFS_LISTINGS");
        const char* packfs_archives_suffixes = packfs_archive_read_new(NULL);
        
        if(packfs_listings != NULL && packfs_listings[0] != '\0')
        {
            for(const char* begin = packfs_listings, *end = strchr(packfs_listings, packfs_pathsep), *prevend  = packfs_listings; prevend != NULL && *begin != '\0'; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
            {
                size_t len = end == NULL ? strlen(begin) : (end - begin);
                strncpy(path_normalized, begin, len);
                path_normalized[len] = '\0';
                char* a = strchr(path_normalized, '@');
                const char* prefix = a != NULL ? (a + 1) : packfs_default_prefix;
                path_normalized[a != NULL ? (a - path_normalized) : len] = '\0';
                
                FILE* fileptr = __real_fopen(path_normalized, "r");
                if(fileptr != NULL)
                {
                    packfs_enabled = 1;
                    packfs_scan_listing(fileptr, path_normalized, prefix);
                    __real_fclose(fileptr);
                }
            }
        }
        else if(packfs_archivedirs != NULL && packfs_archivedirs[0] != '\0')
        {
            for(const char* begin = packfs_archivedirs, *end = strchr(packfs_archivedirs, packfs_pathsep), *prevend  = packfs_archivedirs; prevend != NULL && *begin != '\0'; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
            {
                size_t len = end == NULL ? strlen(begin) : (end - begin);
                strncpy(path_normalized, begin, len);
                path_normalized[len] = '\0';
                char* a = strchr(path_normalized, '@');
                const char* prefix = a != NULL ? (a + 1) : packfs_default_prefix;
                len = a != NULL ? (a - path_normalized) : len;
                path_normalized[len] = '\0';

                DIR* dirptr = __real_opendir(path_normalized);
                if(dirptr != NULL)
                {
                    for(struct dirent* entry = __real_readdir(dirptr); entry != NULL; entry = __real_readdir(dirptr))
                    {
                        size_t path_prefix_len = packfs_archive_prefix_extract(entry->d_name, packfs_archives_suffixes);
                        if(path_prefix_len > 0)
                        {
                            strcpy(path_normalized_entry, path_normalized);
                            path_normalized_entry[len] = packfs_sep;
                            path_normalized_entry[len + 1] = '\0';
                            strcat(path_normalized_entry, entry->d_name);
                        
                            packfs_enabled = 1;
                            FILE* fileptr = __real_fopen(path_normalized_entry, "rb");
                            {
                                packfs_enabled = 1;
                                struct archive *a = archive_read_new();
                                packfs_archive_read_new(a);
                                packfs_scan_archive(fileptr, a, path_normalized, prefix);
                                archive_read_close(a);
                                archive_read_free(a);
                                __real_fclose(fileptr);
                            }
                        }
                        
                            
                    }
                    __real_closedir(dirptr);
                }
                
            }
        }
        else if(packfs_archives != NULL && packfs_archives[0] != '\0')
        {
            for(const char* begin = packfs_archives, *end = strchr(packfs_archives, packfs_pathsep), *prevend  = packfs_archives; prevend != NULL && *begin != '\0'; prevend = end, begin = (end + 1), end = end != NULL ? strchr(end + 1, packfs_pathsep) : NULL)
            {
                size_t len = end == NULL ? strlen(begin) : (end - begin);
                strncpy(path_normalized, begin, len);
                path_normalized[len] = '\0';
                char* a = strchr(path_normalized, '@');
                const char* prefix = a != NULL ? (a + 1) : packfs_default_prefix;
                path_normalized[a != NULL ? (a - path_normalized) : len] = '\0';
                
                FILE* fileptr = __real_fopen(path_normalized, "rb");
                if(fileptr != NULL)
                {
                    packfs_enabled = 1;
                    struct archive *a = archive_read_new();
                    packfs_archive_read_new(a);
                    packfs_scan_archive(fileptr, a, path_normalized, prefix);
                    archive_read_close(a);
                    archive_read_free(a);
                    __real_fclose(fileptr);
                }
            }
        }
        else if(path != NULL)
        {
            packfs_normalize_path(path_normalized, path);
            size_t path_prefix_len = packfs_archive_prefix_extract(path_normalized, packfs_archives_suffixes);
            if(path_prefix_len > 0)
            {
                path_normalized[path_prefix_len] = '\0';
                const char* prefix = path_normalized;
                
                FILE* fileptr = __real_fopen(path_normalized, "rb");
                if(fileptr != NULL)
                {
                    packfs_enabled = 1;
                    struct archive *a = archive_read_new();
                    packfs_archive_read_new(a);
                    packfs_scan_archive(fileptr, a, path_normalized, prefix);
                    archive_read_close(a);
                    archive_read_free(a);
                    __real_fclose(fileptr);
                }
            }
        }
    }
}

int packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_filefd_min && fd < packfs_filefd_max;
}

void* packfs_find(int fd, void* ptr)
{
    if(ptr != NULL)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_fileptr[k] == ptr)
                return &packfs_filefd[k];
        }
        return NULL;
    }
    else
    {
        if(!packfs_fd_in_range(fd))
            return NULL;
        
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == fd)
                return packfs_fileptr[k];
        }
    }
    return NULL;
}

void packfs_resolve_relative_path(char* dest, int dirfd, const char* path)
{
    size_t d_ino = 0, found = 0;
    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == dirfd)
        {
            d_ino = packfs_fileino[k];
            found = 1;
            break;
        }
    }
    
    /*
    for(size_t i = 0; found && (d_ino >= packfs_static_ino_offset && d_ino < packfs_dynamic_ino_offset) && i < packfs_static_files_num; i++)
    {
        size_t entry_index = d_ino - packfs_static_ino_offset;
        if(i == entry_index)
        {
            const char* prefix = packfs_static_prefix;
            const char* entrypath = packfs_static_paths[i]; 
            
            entrypath = (strlen(entrypath) > 1 && entrypath[0] == '.' && entrypath[1] == packfs_sep) ? (entrypath + 2) : entrypath;
            path = (strlen(path) > 1 && path[0] == '.' && path[1] == packfs_sep) ? (path + 2) : path;
            if(strlen(entrypath) > 0)
                sprintf(dest, "%s%c%s%c%s", prefix, (char)packfs_sep, entrypath, (char)packfs_sep, path);
            else
                sprintf(dest, "%s%c%s", prefix, (char)packfs_sep, path);
            return;
        }
    }
    */
    
    for(size_t i = 0, offset = 0; i < packfs_dynamic_dirs_num; offset += (strlen(packfs_dynamic_dirpaths + offset) + 1), i++)
    {
        const char* entryabspath = packfs_dynamic_dirpaths + offset;
    
        if(i == d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset)
        {
            path = (strlen(path) > 1 && path[0] == '.' && path[1] == packfs_sep) ? (path + 2) : path;
            strcpy(dest, entryabspath);
            strcat(dest, path);
            return;
        }
    }

    strcpy(dest, path);
}

/*
int packfs_indir(const char* dirpath, const char* path)
{
    size_t dirpath_len = strlen(dirpath);
    size_t path_len = strlen(path);
    if(dirpath_len == 0 || (dirpath_len > 0 && dirpath[dirpath_len - 1] != packfs_sep))
        return 0;
    int dirpath_first_slash = dirpath[0] == packfs_sep ? 1 : 0;
    int prefix_matches = 0 == strncmp(dirpath + dirpath_first_slash, path, dirpath_len - dirpath_first_slash);
    if(!prefix_matches)
        return 0;
    const char* suffix_slash = strchr(path + dirpath_len - dirpath_first_slash, packfs_sep);
    int suffix_without_dirs = NULL == suffix_slash || (path + path_len - 1 == suffix_slash);
    int suffix_not_empty = strlen(path + dirpath_len) > 0;
    return suffix_without_dirs && suffix_not_empty;
}
*/

int packfs_indir(const char* dirpath, const char* path)
{
    size_t dirpath_len = strlen(dirpath);
    size_t path_len = strlen(path);
    if(dirpath_len == 0 || (dirpath_len > 0 && dirpath[dirpath_len - 1] != packfs_sep))
        return 0;
    int prefix_matches = 0 == strncmp(dirpath, path, dirpath_len - 1);
    if(!prefix_matches)
        return 0;
    const char* suffix_slash = strchr(path + dirpath_len, packfs_sep);
    int suffix_without_dirs = NULL == suffix_slash || (path + path_len - 1 == suffix_slash);
    int suffix_not_empty = strlen(path + dirpath_len) > 0;
    return suffix_without_dirs && suffix_not_empty;
}

void* packfs_readdir(void* stream)
{
    struct dirent* dir_entry = stream;
    
    size_t d_ino = (size_t)dir_entry->d_ino;
    /*
    if(d_ino >= packfs_static_ino_offset && d_ino < packfs_dynamic_ino_offset)
    {
        size_t ino_offset = packfs_static_ino_offset;
        size_t entry_index = d_ino - ino_offset;
        
        for(size_t i = 0; i < packfs_static_files_num; i++)
        {
            const char* dir_entry_name = packfs_static_paths[(size_t)dir_entry->d_off];
            
            const char* entrypath = packfs_static_paths[i];
            size_t entrypath_len = strlen(entrypath);
            int entryisdir = entrypath[0] != '\0' && entrypath[strlen(entrypath) - 1] == packfs_sep;
            
            if(i > entry_index && packfs_indir(dir_entry_name, entrypath))
            {
                if(entryisdir)
                {
                    strcpy(dir_entry->d_name, entrypath);
                    dir_entry->d_name[entrypath_len - 1] = '\0';
                    const char* last_slash = strrchr(dir_entry->d_name, packfs_sep); 
                    size_t ind = last_slash != NULL ? (last_slash - dir_entry->d_name + 1) : 0;
                    size_t cnt = entrypath_len - 1 - ind;
                    strncpy(dir_entry->d_name, entrypath + ind, cnt);
                    dir_entry->d_name[cnt] = '\0';
                }
                else
                {
                    const char* last_slash = strrchr(entrypath, packfs_sep);
                    strcpy(dir_entry->d_name, last_slash != NULL ? (last_slash + 1) : entrypath);
                }
                dir_entry->d_type = entryisdir ? DT_DIR : DT_REG;
                dir_entry->d_ino = (ino_t)(ino_offset + i);
                return dir_entry;
            }
        }
    }
    else
    */
    if(d_ino >= packfs_dynamic_ino_offset)
    {
        int check_dirs = (d_ino >= packfs_dynamic_ino_offset + packfs_dirs_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset + packfs_dirs_ino_offset);
        int check_files = (d_ino >= packfs_dynamic_ino_offset) && (d_ino < packfs_dynamic_ino_offset + packfs_dirs_ino_offset);
        const char* dirabspath = packfs_dynamic_dirpaths + (size_t)dir_entry->d_off;
        
        for(size_t i = 0, offset = 0; check_dirs && i < packfs_dynamic_dirs_num; offset += (strlen(packfs_dynamic_dirpaths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_dirpaths + offset;
            size_t entryabspath_len = strlen(entryabspath);
            
            fprintf(stderr, "packfs_readdir1 '%s' '%s' %d\n", dirabspath, entryabspath, packfs_indir(dirabspath, entryabspath));
            if(i > (d_ino - packfs_dynamic_ino_offset - packfs_dirs_ino_offset) && packfs_indir(dirabspath, entryabspath))
            {
                strcpy(dir_entry->d_name, entryabspath);
                dir_entry->d_name[entryabspath_len - 1] = '\0';
                const char* last_slash = strrchr(dir_entry->d_name, packfs_sep); 
                size_t ind = last_slash != NULL ? (last_slash - dir_entry->d_name + 1) : 0;
                size_t cnt = entryabspath_len - 1 - ind;
                strncpy(dir_entry->d_name, entryabspath + ind, cnt);
                dir_entry->d_name[cnt] = '\0';
                dir_entry->d_type = DT_DIR; 
                dir_entry->d_ino = (ino_t)(packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i);
                return dir_entry;
            }
        }

        if(check_dirs)
        {
            check_files = 1;
            d_ino = packfs_dynamic_ino_offset;
        }
        
        for(size_t i = 0, offset = 0; check_files && i < packfs_dynamic_files_num; offset += (strlen(packfs_dynamic_paths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_paths + offset;
            
            fprintf(stderr, "packfs_readdir2 '%s' '%s' %d\n", dirabspath, entryabspath, packfs_indir(dirabspath, entryabspath));
            
            if((i > (d_ino - packfs_dynamic_ino_offset) || (i == 0 && check_dirs)) && packfs_indir(dirabspath, entryabspath))
            {
                const char* last_slash = strrchr(entryabspath, packfs_sep);
                strcpy(dir_entry->d_name, last_slash != NULL ? (last_slash + 1) : entryabspath);
                dir_entry->d_type = DT_REG;
                dir_entry->d_ino = (ino_t)(packfs_dynamic_ino_offset + i);
                return dir_entry;
            }

        }
    }
    return NULL;
}

int packfs_access(const char* path)
{
    char path_normalized[packfs_files_name_maxlen]; packfs_normalize_path(path_normalized, path);
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        /*
        for(size_t i = (packfs_path_in_range(packfs_static_prefix, path_normalized) ? 0 : packfs_static_files_num); i < packfs_static_files_num; i++)
        {
            const char* prefix = packfs_static_prefix;
            const char* entrypath = packfs_static_paths[i];
            size_t prefix_len = strlen(prefix);
            if(packfs_match(path_normalized, prefix, entrypath, prefix_len))
                return 0;
        }
        */
        for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, path_normalized) ? 0 : packfs_dynamic_files_num), offset = 0; i < packfs_dynamic_files_num; offset += (strlen(packfs_dynamic_paths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_paths + offset;
            if(0 == strcmp(path_normalized, entryabspath))
                return 0;
        }
        return -1;
    }
    return -2;
}

int packfs_stat(const char* path, int fd, size_t* isdir, size_t* size, size_t* d_ino)
{
    char path_normalized[packfs_files_name_maxlen]; packfs_normalize_path(path_normalized, path);
    size_t path_normalized_len = strlen(path_normalized);
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        /*
        for(size_t i = (packfs_path_in_range(packfs_static_prefix, path_normalized) ? 0 : packfs_static_files_num); i < packfs_static_files_num; i++)
        {
            const char* prefix = packfs_static_prefix;
            const char* entrypath = packfs_static_paths[i];
            int entryisdir = entrypath[0] != '\0' && entrypath[strlen(entrypath) - 1] == packfs_sep;
            size_t prefix_len = strlen(prefix);
            if(packfs_match(path_normalized, prefix, entrypath, prefix_len))
            {
                *size = packfs_static_ends[i] - packfs_static_starts[i];
                *isdir = entryisdir;
                *d_ino = packfs_static_ino_offset + i;
                return 0;
            }
        }
        */
        
        for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, path_normalized) ? 0 : packfs_dynamic_dirs_num), offset = 0; i < packfs_dynamic_dirs_num; offset += (strlen(packfs_dynamic_dirpaths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_dirpaths + offset;
            if(0 == strncmp(path_normalized, entryabspath, path_normalized_len) && entryabspath[path_normalized_len] == packfs_sep && entryabspath[path_normalized_len + 1] == '\0')
            {
                *size = 0;
                *isdir = 1;
                *d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                return 0;
            }
        }
    
        for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, path_normalized) ? 0 : packfs_dynamic_files_num), offset = 0; i < packfs_dynamic_files_num; offset += (strlen(packfs_dynamic_paths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_paths + offset;
            if(0 == strcmp(path_normalized, entryabspath))
            {
                *size = packfs_dynamic_files_sizes[i];
                *isdir = 0;
                *d_ino = packfs_dynamic_ino_offset + i;
                return 0;
            }
        }
        return -1;
    }
    
    if(packfs_fd_in_range(fd))
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == fd)
            {
                *size = packfs_filesize[k];
                *isdir = packfs_fileisdir[k];
                *d_ino = packfs_fileino[k];
                return 0;
            }
        }
        return -1;
    }

    return -2;
}

void* packfs_open(const char* path, int flags)
{
    char path_normalized[packfs_files_name_maxlen]; packfs_normalize_path(path_normalized, path);
    size_t path_normalized_len = strlen(path_normalized);

    void* fileptr = NULL; size_t filesize = 0, d_ino = 0, d_off = 0, found = 0;
    
    if(packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized))
    {
        /*
        for(size_t i = (packfs_path_in_range(packfs_static_prefix, path_normalized) ? 0 : packfs_static_files_num); i < packfs_static_files_num; i++)
        {
            const char* prefix = packfs_static_prefix;
            const char* entrypath = packfs_static_paths[i];
            int entryisdir = entrypath[0] != '\0' && entrypath[strlen(entrypath) - 1] == packfs_sep;
            size_t prefix_len = strlen(prefix);
            if(packfs_match(path_normalized, prefix, entrypath, prefix_len))
            {
                if(entryisdir)
                {
                    found = 2;
                    d_ino = packfs_static_ino_offset + i;
                    d_off = 0; //packfs_dynamic_paths_offset;
                    filesize = 0;
                }
                else
                {
                    found = 1;
                    d_ino = packfs_static_ino_offset + i;
                    d_off = 0;
                    
                    filesize = (size_t)(packfs_static_ends[i] - packfs_static_starts[i]);
                    fileptr = fmemopen((void*)packfs_static_starts[i], filesize, "r");
                }
                break;
            }
        }
        */
        
        for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, path_normalized) ? 0 : packfs_dynamic_dirs_num), offset = 0; i < packfs_dynamic_dirs_num; offset += (strlen(packfs_dynamic_dirpaths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_dirpaths + offset;
            if(0 == strncmp(path_normalized, entryabspath, path_normalized_len) && entryabspath[path_normalized_len] == packfs_sep && entryabspath[path_normalized_len + 1] == '\0')
            {
                found = 2;
                d_ino = packfs_dynamic_ino_offset + packfs_dirs_ino_offset + i;
                d_off = offset;
                filesize = 0;
            }
        }
        
        for(size_t i = (packfs_path_in_range(packfs_dynamic_prefix, path_normalized) ? 0 : packfs_dynamic_files_num), offset = 0; i < packfs_dynamic_files_num; offset += (strlen(packfs_dynamic_paths + offset) + 1), i++)
        {
            const char* entryabspath = packfs_dynamic_paths + offset;
            size_t prefix_len = packfs_dynamic_paths_prefixlen[i];
            const char* entrypath = packfs_dynamic_paths + offset + prefix_len;
            const char* archivepath   = packfs_dynamic_archivepaths + packfs_dynamic_files_archiveoffset[i];
            
            if(0 == strcmp(path_normalized, entryabspath))
            {
                found = 1;
                d_ino = packfs_dynamic_ino_offset + i;
                d_off = 0;
                filesize = packfs_dynamic_files_sizes[i];

                fileptr = fmemopen(NULL, filesize, "rb+");
                FILE* packfs_archive_fileptr = __real_fopen(archivepath, "rb");//packfs_archive_fileptr;
                if(packfs_archive_fileptr != NULL)
                {
                    struct archive *a = archive_read_new();
                    packfs_archive_read_new(a);
                    packfs_extract_archive_entry_from_FILE_to_FILE(a, packfs_archive_fileptr, entrypath, (FILE*)fileptr);
                    archive_read_close(a);
                    archive_read_free(a);
                    __real_fclose(packfs_archive_fileptr);
                }
                fseek((FILE*)fileptr, 0, SEEK_SET);
                break;
            }
        }
    }
    
    for(size_t k = 0; found > 0 && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == 0)
        {
            int fd = packfs_filefd_min + k;
            
            if(found == 2)
            {
                packfs_dirent[k] = (struct dirent){0};
                packfs_dirent[k].d_ino = (ino_t)d_ino;
                packfs_dirent[k].d_off = (off_t)d_off;
                fileptr = &packfs_dirent[k];
            }

            packfs_fileisdir[k] = found == 2;
            packfs_filefdrefs[k] = 1;
            packfs_filefd[k] = fd;
            packfs_fileptr[k] = fileptr;
            packfs_filesize[k] = filesize;
            packfs_fileino[k] = d_ino;
            return fileptr;
        }
    }

    return NULL;
}

int packfs_close(int fd)
{
    if(!packfs_fd_in_range(fd))
        return -2;

    for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        if(packfs_filefd[k] == fd)
        {
            packfs_filefdrefs[k]--;
            if(packfs_filefdrefs[k] > 0)
                return 0;

            int res = (!packfs_fileisdir[k]) ? __real_fclose(packfs_fileptr[k]) : 0;
            packfs_dirent[k]  = (struct dirent){0};
            packfs_fileisdir[k] = 0;
            packfs_filefd[k] = 0;
            packfs_filesize[k] = 0;
            packfs_fileptr[k] = NULL;
            packfs_fileino[k] = 0;
            return res;
        }
    }
    return -1;
}

ssize_t packfs_read(int fd, void* buf, size_t count)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fread(buf, 1, count, ptr);
}

int packfs_seek(int fd, long offset, int whence)
{
    FILE* ptr = packfs_find(fd, NULL);
    if(!ptr)
        return -1;
    return fseek(ptr, offset, whence);
}

int packfs_dup(int oldfd, int newfd)
{
    int K = -1;
    if(oldfd >= 0 && packfs_filefd_min <= oldfd && oldfd < packfs_filefd_max)
    {
        for(size_t k = 0; k < packfs_filefd_max - packfs_filefd_min; k++)
        {
            if(packfs_filefd[k] == oldfd)
            {
                K = k;
                break;
            }
        }
    }
    for(size_t k = 0; K >= 0 && k < packfs_filefd_max - packfs_filefd_min; k++)
    {
        int fd = packfs_filefd_min + k;
        if(packfs_filefd[k] == 0 && (newfd < packfs_filefd_min || newfd >= fd))
        {
            packfs_filefdrefs[K]++;
            
            packfs_fileisdir[k] = packfs_fileisdir[K];
            packfs_filefd[k]    = fd;
            packfs_filefdrefs[k]= 1;
            packfs_filesize[k]  = packfs_filesize[K];
            packfs_fileino[k]   = packfs_fileino[K];
            packfs_dirent[k]    = packfs_dirent[K];
            packfs_fileptr[k]   = packfs_fileptr[K];
            return fd;
        }
    }
    return -1;
    
}

///////////

FILE* PACKFS_WRAP(fopen)(const char *path, const char *mode)
{
    packfs_init(path);
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
    packfs_init(NULL);
    int res = __real_fileno(stream);
    if(packfs_enabled && res < 0)
    {        
        int* ptr = packfs_find(-1, stream);
        res = ptr == NULL ? -1 : (*ptr);
    }
    return res;
}

int PACKFS_WRAP(fclose)(FILE* stream)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
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
    
    packfs_init(path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        void* stream = packfs_open(path, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
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

    packfs_init(path);
    char path_normalized[packfs_files_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        void* stream = packfs_open(path_normalized, (flags & O_DIRECTORY) != 0);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int res = ptr == NULL ? -1 : (*ptr);
            return res;
        }
    }
    
    return __real_openat(dirfd, path, flags, mode);
}

int PACKFS_WRAP(close)(int fd)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_close(fd);
        if(res >= -1)
            return res;
    }
    return __real_close(fd);
}

ssize_t PACKFS_WRAP(read)(int fd, void* buf, size_t count)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        ssize_t res = packfs_read(fd, buf, count);
        if(res >= 0)
            return res;
    }
    return __real_read(fd, buf, count);
}

off_t PACKFS_WRAP(lseek)(int fd, off_t offset, int whence)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = packfs_seek(fd, (long)offset, whence);
        if(res >= 0)
            return res;
    }
    return __real_lseek(fd, offset, whence);
}

int PACKFS_WRAP(access)(const char *path, int flags)
{
    packfs_init(path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        int res = packfs_access(path);
        if(res >= -1)
            return res;
    }
    return __real_access(path, flags); 
}

int PACKFS_WRAP(stat)(const char *restrict path, struct stat *restrict statbuf)
{
    packfs_init(path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }

    return __real_stat(path, statbuf);
}

int PACKFS_WRAP(fstat)(int fd, struct stat * statbuf)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(NULL, fd, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }
        if(res >= -1)
            return res;
    }
    
    return __real_fstat(fd, statbuf);
}

int PACKFS_WRAP(fstatat)(int dirfd, const char* path, struct stat * statbuf, int flags)
{
    packfs_init(path);
    char path_normalized[packfs_files_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        *statbuf = (struct stat){0};
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
        if(res == 0)
        {
            statbuf->st_mode = isdir ? S_IFDIR : S_IFREG;
            statbuf->st_size = size;
            statbuf->st_ino = (ino_t)d_ino;
        }

        if(res >= -1)
            return res;
    }
    
    return __real_fstatat(dirfd, path, statbuf, flags);
}

int PACKFS_WRAP(statx)(int dirfd, const char *restrict path, int flags, unsigned int mask, struct statx *restrict statbuf)
{
    packfs_init(path);
    char path_normalized[packfs_files_name_maxlen]; packfs_resolve_relative_path(path_normalized, dirfd, path);

    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path_normalized) || packfs_path_in_range(packfs_dynamic_prefix, path_normalized)))
    {
        size_t size = 0, isdir = 0, d_ino = 0;
        int res = packfs_stat(path_normalized, -1, &isdir, &size, &d_ino);
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
    packfs_init(path);
    if(packfs_enabled && (packfs_path_in_range(packfs_static_prefix, path) || packfs_path_in_range(packfs_dynamic_prefix, path)))
    {
        void* stream = packfs_open(path, 1);
        if(stream != NULL)
        {
            int* ptr = packfs_find(-1, stream);
            int fd = ptr == NULL ? -1 : *ptr;
            return (DIR*)stream;
        }
    }
    return __real_opendir(path);
}

DIR* PACKFS_WRAP(fdopendir)(int dirfd)
{
    packfs_init(NULL);
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
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {
        int* ptr = packfs_find(-1, stream);
        if(ptr != NULL)
            return (struct dirent*)packfs_readdir(stream);
    }
    return __real_readdir(stream);
}

int PACKFS_WRAP(closedir)(DIR* stream)
{
    packfs_init(NULL);
    if(packfs_enabled && packfs_find(-1, stream) != NULL)
    {        
        int* ptr = packfs_find(-1, stream);
        int fd = ptr == NULL ? -1 : *ptr;
        int res = packfs_close(fd);
        if(res >= -1)
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

    packfs_init(NULL);
    
    if(packfs_enabled && packfs_fd_in_range(fd))
    {
        int res = (argtype == '0' && (action == F_DUPFD || action == F_DUPFD_CLOEXEC)) ? packfs_dup(fd, intarg) : -1;
        if(res >= -1)
            return res;
    }
    
    return (argtype == '0' ? __real_fcntl(fd, action, intarg) : argtype == '*' ? __real_fcntl(fd, action, ptrarg) : __real_fcntl(fd, action));
}
