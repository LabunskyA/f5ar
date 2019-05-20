#include <jpeg/jpeglib.h>
#include "md5.h"

#define MD5_SIZE 16
enum SOURCE { FILE_SRC, MEM_SRC };

#define get_row(dct_arrays, dstruct, row_id)\
dstruct.mem->access_virt_barray(\
    (j_common_ptr) &dstruct, dct_arrays[0],\
    row_id, (JDIMENSION) 1, FALSE\
)[0]

typedef struct {
    struct {
        JDIMENSION row_id;
        JDIMENSION block_id;
        JDIMENSION coeff_id;

        JBLOCKROW row;

        size_t pos;
    } dct;

    struct {
        jvirt_barray_ptr *dct_arrays;
        struct jpeg_decompress_struct dstruct;
    } jpeg;

    struct {
        int type;
        union {
            struct {
                FILE *stream;
                char *path;
            } fs;
            struct {
                void *ptr;
                size_t *size;
            } mem;
        };
    } src;

    size_t size;
    bool is_active;

    char hash[MD5_SIZE];
} container_t;

#define dct_get(iterator) iterator.row[iterator.block_id][iterator.coeff_id]

int c_next(container_t* container) {
    container->dct.pos++;
    container->dct.coeff_id++;

    if (container->dct.pos == container->size)
        return F5AR_FAILURE;

    if (container->dct.coeff_id == DCTSIZE2) {
        container->dct.coeff_id = 0;
        container->dct.block_id++;

        if (container->dct.block_id == container->jpeg.dstruct.comp_info[0].width_in_blocks) {
            container->dct.block_id = 0;
            container->dct.row_id++;

            container->dct.row = get_row(
                    container->jpeg.dct_arrays,
                    container->jpeg.dstruct,
                    container->dct.row_id
            );
        }
    }

    return F5AR_OK;
}

int container_open(container_t *container, struct jpeg_error_mgr* jerr) {
    if (container->is_active)
        return F5AR_OK;

    container->jpeg.dstruct.err = jpeg_std_error(jerr);
    jpeg_create_decompress(&container->jpeg.dstruct);
    switch (container->src.type) {
        case FILE_SRC:
            jpeg_stdio_src(&container->jpeg.dstruct, container->src.fs.stream);
            break;
        case MEM_SRC:
            jpeg_mem_src(&container->jpeg.dstruct, container->src.mem.ptr, *container->src.mem.size);
            break;
    }

    jpeg_read_header(&container->jpeg.dstruct, TRUE);

    const size_t height_in_blocks = container->jpeg.dstruct.comp_info[0].height_in_blocks;
    const size_t width_in_blocks = container->jpeg.dstruct.comp_info[0].width_in_blocks;

    container->size = width_in_blocks * DCTSIZE2 * height_in_blocks;
    container->jpeg.dct_arrays = jpeg_read_coefficients(&container->jpeg.dstruct);

    /* Reset the iterator */
    memset(&container->dct, 0, sizeof(container->dct));
    container->dct.row = get_row(container->jpeg.dct_arrays, container->jpeg.dstruct, 0);

    container->is_active = true;
    return F5AR_OK;
}

void container_close_discard(container_t *container) {
    container->is_active = false;

    jpeg_finish_decompress(&container->jpeg.dstruct);
    jpeg_destroy_decompress(&container->jpeg.dstruct);

    if (container->src.type == FILE_SRC)
        fseek(container->src.fs.stream, 0, SEEK_SET);
}

int container_close_keep(container_t *container, struct jpeg_error_mgr* jerr) {
    struct jpeg_compress_struct cstruct;
    cstruct.err = jpeg_std_error(jerr);

    jpeg_create_compress(&cstruct);
    jpeg_copy_critical_parameters(&container->jpeg.dstruct, &cstruct);

    switch (container->src.type) {
        case FILE_SRC:
            container->src.fs.stream = freopen(container->src.fs.path, "wb", container->src.fs.stream);
            if (!container->src.fs.stream)
                return F5AR_IO_ERR;

            jpeg_stdio_dest(&cstruct, container->src.fs.stream);
            break;

        case MEM_SRC:
            jpeg_mem_dest(&cstruct, container->src.mem.ptr, container->src.mem.size);
            break;
    }

    jpeg_write_coefficients(&cstruct, container->jpeg.dct_arrays);
    jpeg_finish_compress(&cstruct), jpeg_destroy_compress(&cstruct);

    switch (container->src.type) {
        case FILE_SRC:
            container->src.fs.stream = freopen(container->src.fs.path, "rb", container->src.fs.stream);
            md5_file(container->src.fs.stream, container->hash);
            break;

        case MEM_SRC:
            md5_buffer(container->src.mem.ptr, *container->src.mem.size, container->hash);
            break;
    }

    jpeg_finish_decompress(&container->jpeg.dstruct),
            jpeg_destroy_decompress(&container->jpeg.dstruct);

    container->is_active = false;
    return F5AR_OK;
}