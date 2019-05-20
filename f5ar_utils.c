#include <stdio.h>
#include <string.h>
#include <stdlib.h>

char* file_read(const char *path, size_t *size) {
    FILE *infile = fopen(path, "rb");
    if (!infile)
        return NULL;

    fseek(infile, 0L, SEEK_END),
            *size += ftell(infile),
            fseek(infile, 0L, SEEK_SET);

    char *buffer = malloc(*size);
    if (buffer == NULL)
        return NULL;

    const size_t expected = *size;
    if (fread(buffer, 1, expected, infile) != expected)
        free(buffer), buffer = NULL;

    fclose(infile);
    return buffer;
}

int file_write(const char *path, const char *data, size_t size) {
    FILE *infile = fopen(path, "wb");
    if (!infile)
        return -1;

    const size_t wrote = fwrite(data, 1, size, infile);
    fclose(infile);

    if (wrote != size)
        return -2;
    return 0;
}

void inline extract_dir_path(char* dest, const char* src) {
    unsigned up_to = 0;
    for (unsigned j = 0; j < FILENAME_MAX && src[j]; ++j)
        if (src[j] == '/')
            up_to = j + 1;

    if (up_to > 0)
        strncpy(dest, src, up_to);
    else
        dest[0] = '.';
}