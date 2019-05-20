/*
* An API for data compression in JPEG files
* using modified F5 steganography algorithm
* Written by Labunsky Artem <me@labunsky.info>
*
* This software is based in part on the work of the Independent JPEG Group
* Distributed under the Simplified BSD License
*/

#ifndef F5C_H
#define F5C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

/* Here you can find all the error codes */
enum F5AR_CODES {
    F5AR_OK = 0, F5AR_OK_COMPLETE = 1, F5AR_NOT_FOUND = 2,

    F5AR_MALLOC_ERR = -1, F5AR_FILEIO_ERR = -2,
    F5AR_NOT_INITIALIZED = -3, F5AR_NOT_COMPLETE = -5,
    F5AR_FAILURE = -6, F5AR_IO_ERR = -7,
    F5AR_WRONG_ARGS = -8
};

typedef struct {
    uint8_t k;
    uint64_t msg_size;
} f5archive_meta;

typedef struct {
    size_t shrinkable;
    size_t full;
} f5archive_capacity;

typedef struct {
    int state;

    f5archive_meta meta;
    f5archive_capacity capacity;

    struct f5archive_ctx *ctx;
} f5archive;

/* Call this before any operations */
int f5ar_init(f5archive *);

/* Compression API */

/* You can add containers sequentially calling these functions to form new order */
int f5ar_add_file(f5archive *, const char *path);

/* size will be updated via the pointer as the file size could change and should contain the original size */
int f5ar_add_mem(f5archive *archive, void *ptr, size_t* size);

/* Free all used by the archive memory and close all openned file streams */
void f5ar_destroy(f5archive *);

/* Will be called only once */
int f5ar_analyze(f5archive *);

/* Do compression and fetch the result */
int f5ar_pack(f5archive *, const char *data, size_t size);


typedef struct {
    size_t size;
    char body[];
} f5ar_blob;

/* Export order of every container in the archive */
f5ar_blob *f5ar_export_order(f5archive *);

/* Export only subset of containers used in the packing process */
f5ar_blob *f5ar_export_order_used(f5archive *);

/* Decompression API */

/* Use this functions to import previously exported order into the other array
* This call will destroy the existing archieve order and free all associated memory
* Archive will contain only hashes in the specefied order, you need to use **trycomplete**
* functions to make it suitable for the compression and decompression ones */
int f5ar_import_order(f5archive *, f5ar_blob *);

/* Try filling any empty slots in imported order with a file */
int f5ar_fill_file(f5archive *, const char *path);

/* Same as the *add_mem() one */
int f5ar_fill_mem(f5archive *archive, void *ptr, size_t* size);

int f5ar_unpack(f5archive *, char **res_ptr, size_t *size);

#ifdef __cplusplus
}
#endif
#endif