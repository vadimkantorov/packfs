#include <string.h>

enum
{
    packfs_sep = '/',
    packfs_pathsep = ':'
};

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

const char* packfs_basename(const char* path)
{
    const char* trailing_slash = strrchr(path, packfs_sep);
    const char* basename = trailing_slash ? (trailing_slash + 1) : path;
    return basename;
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

int packfs_indir(const char* dir_path, const char* path)
{
    size_t dir_path_len = strlen(dir_path);
    const char* trailing_slash = strrchr(path, packfs_sep);
    if(dir_path_len == 0 && trailing_slash == NULL)
        return 1;
    if(0 == strncmp(dir_path, path, dir_path_len) && trailing_slash == (path + dir_path_len))
        return 1;
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

int strcmp_without_trailing_slash(const char* a, const char* b)
{
    size_t a_len = a != NULL ? strlen(a) : 0;
    size_t b_len = b != NULL ? strlen(b) : 0;
    int a_trailing_slash = a_len > 0 && a[a_len - 1] == packfs_sep;
    int b_trailing_slash = b_len > 0 && b[b_len - 1] == packfs_sep;

    size_t a_len_m1 = a_len - a_trailing_slash;
    size_t b_len_m1 = b_len - b_trailing_slash;
    
    if(a_len_m1 == 0 && b_len_m1 == 0)
        return 0;
    if((a_len_m1 == 0) ^ (b_len_m1 == 0))
        return 1;
    return strncmp(a, b, a_len_m1 > b_len_m1 ? a_len_m1 : b_len_m1);
}

int packfs_match(const char* path, const char* prefix, const char* entrypath)
{
    if(path == NULL || prefix == NULL || entrypath == NULL)
        return 0;

    size_t path_len = strlen(path);
    size_t prefix_len = strlen(prefix);
    size_t entrypath_len = strlen(entrypath);
    
    int path_trailing_slash = path_len > 0 && path[path_len - 1] == packfs_sep;
    int prefix_trailing_slash = prefix_len > 0 && prefix[prefix_len - 1] == packfs_sep;
    int entrypath_trailing_slash = entrypath_len > 0 && entrypath[entrypath_len - 1] == packfs_sep;

    if(prefix_len > 0)
    {
        if(0 != strncmp(path, prefix, prefix_len - prefix_trailing_slash))
            return 0;
        if(path_len - path_trailing_slash == prefix_len - prefix_trailing_slash)
            return 1;
        if(path[prefix_len - prefix_trailing_slash] != packfs_sep)
            return 0;
        if(0 != strcmp_without_trailing_slash(path + prefix_len + (!prefix_trailing_slash), entrypath))
            return 0;
        return 1;
    }
    else
    {
        return 0 == strcmp_without_trailing_slash(path, entrypath);
    }

    return 0;
}
