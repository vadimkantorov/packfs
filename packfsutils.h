#include <string.h>

enum
{
    packfs_filefd_min = 1000000000, 
    packfs_filefd_max = 1000001000, 
    packfs_entries_name_maxlen = 128, 
    packfs_archive_entries_nummax = 1024,
    packfs_sep = '/',
    packfs_pathsep = ':'
};

void packfs_sanitize_path(char* path_sanitized, const char* path)
{
    size_t len = path != NULL ? strlen(path) : 0;
    if(len == 0)
        path_sanitized[0] = '\0';

    // lstrips ./ in the beginning; collapses double consecutive slashes; and rstrips abc/asd/..
    for(int i = (path != NULL && len > 2 && path[0] == '.' && path[1] == packfs_sep) ? 2 : 0, k = 0; len > 0 && i < len; i++)
    {
        if(!(i > 1 && path[i] == packfs_sep && path[i - 1] == packfs_sep))
        {
            path_sanitized[k++] = path[i];
            path_sanitized[k] = '\0';
        }
    }
    
    size_t path_sanitized_len = strlen(path_sanitized);
    if(path_sanitized_len >= 3 && path_sanitized[path_sanitized_len - 1] == '.' && path_sanitized[path_sanitized_len - 2] == '.'  && path_sanitized[path_sanitized_len - 3] == packfs_sep)
    {
        path_sanitized[path_sanitized_len - 3] = '\0';
        char* last_slash = strrchr(path_sanitized, packfs_sep);
        if(last_slash != NULL)
            *last_slash = '\0';
    }
}

const char* packfs_basename(const char* path)
{
    const char* last_slash = strrchr(path, packfs_sep);
    const char* basename = last_slash ? (last_slash + 1) : path;
    return basename;
}

int packfs_path_in_range(const char* prefix, const char* path)
{
    if(prefix == NULL || prefix[0] == '\0' || path == NULL || path[0] == '\0')
        return 0;
    size_t prefix_len = strlen(prefix);
    size_t path_len = strlen(path);
    int prefix_endswith_slash = prefix[prefix_len - 1] == packfs_sep;
    int prefix_ok = 0 == strncmp(prefix, path, prefix_len - (prefix_endswith_slash ? 1 : 0));
    size_t prefix_len_m1 = prefix_endswith_slash ? (prefix_len - 1) : prefix_len;
    return prefix_ok && ((path_len == prefix_len_m1) || (path_len >= prefix_len && path[prefix_len_m1] == packfs_sep));
}

int packfs_fd_in_range(int fd)
{
    return fd >= 0 && fd >= packfs_filefd_min && fd < packfs_filefd_max;
}

int packfs_indir(const char* dir_path, const char* path)
{
    size_t dir_path_len = strlen(dir_path);
    const char* last_slash = strrchr(path, packfs_sep);
    if(dir_path_len == 0 && last_slash == NULL)
        return 1;
    if(0 == strncmp(dir_path, path, dir_path_len) && last_slash == (path + dir_path_len))
        return 1;
    return 0;
}

const char* packfs_lstrip_prefix(const char* path, const char* prefix)
{
    if(path == NULL || prefix == NULL)
        return NULL;
    
    size_t prefix_len = strlen(prefix);

    if(prefix_len == 0)
        return path;

    if(0 == strncmp(prefix, path, prefix_len))
    {
        const char* path_without_prefix = path + prefix_len;
        if(path_without_prefix[0] == packfs_sep)
            path_without_prefix++;
        return path_without_prefix;
    }
    
    //return NULL;
    return path;
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
