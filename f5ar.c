/*
* API implementation for data compression using modified F5 steganography algorithm
* Written by Labunsky Artem <me@labunsky.info>
*
* This software is based in part on the work of the Independent JPEG Group
* Distributed under the Simplified BSD License
*/

#include "f5ar.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "md5.h"
#include "container.c"

struct linked_container {
    struct linked_container* next;
    container_t container;
};

struct f5archive_ctx {
    struct linked_container* head;
    struct linked_container* tail;

    struct jpeg_error_mgr err;

    /* Even 64-bit servers should not be able to handle more than
    * 2^32 * |C| bytes for at least a decade */
    uint32_t size;
    uint32_t filled;

    uint32_t used;
};

int f5ar_init(f5archive* archive) {
    archive->ctx = (archive->ctx ? archive->ctx : calloc(sizeof(struct f5archive_ctx), 1));
    if (!archive->ctx)
        return F5AR_MALLOC_ERR;

    return F5AR_OK;
}

static struct linked_container* append_new(f5archive *archive) {
    if (!archive->ctx)
        return NULL;

    if (!archive->ctx->head)
        archive->ctx->head = calloc(sizeof(struct linked_container), 1),
        archive->ctx->tail = archive->ctx->head;
    else
        archive->ctx->tail->next = calloc(sizeof(struct linked_container), 1),
        archive->ctx->tail = archive->ctx->tail->next;

    archive->ctx->size += archive->ctx->tail ? 1 : 0;
    return archive->ctx->tail;
}

static void free_tail(f5archive *archive) {
    struct linked_container *el = archive->ctx->head;
    while (el->next != archive->ctx->tail)
        el = el->next;
    archive->ctx->tail = el;

    free (el->next);
    el->next = NULL;

    archive->ctx->size--;
}

static int copy_fs_path(f5archive *archive, struct linked_container *new, const char *path) {
    const size_t path_len = strlen(path);
    new->container.src.fs.path = malloc(path_len + 1);
    if (!new->container.src.fs.path) {
        fclose(new->container.src.fs.stream);
        free_tail(archive);

        return F5AR_MALLOC_ERR;
    }

    strcpy(new->container.src.fs.path, path);
    new->container.src.fs.path[path_len] = '\0';

    return F5AR_OK;
}

int f5ar_add_file(f5archive *archive, const char *path) {
    FILE* src = fopen(path, "rb");
    if (!src)
        return F5AR_FILEIO_ERR;

    struct linked_container* new = append_new(archive);
    if (!new) {
        fclose(src);
        return F5AR_MALLOC_ERR;
    }

    new->container.src.type = FILE_SRC;
    new->container.src.fs.stream = src;

    const int err = copy_fs_path(archive, new, path);
    if (!err)
        archive->ctx->filled++;
    return err;
}

int f5ar_add_mem(f5archive *archive, void *ptr, size_t* size) {
    struct linked_container* new = append_new(archive);
    if (!new)
        return F5AR_MALLOC_ERR;

    new->container.src.type = MEM_SRC;
    new->container.src.mem.ptr = ptr;
    new->container.src.mem.size = size;

    archive->ctx->filled++;
    return F5AR_OK;
}

static int f5archive_clear_ctx(struct f5archive_ctx* ctx) {
    if (!ctx)
        return F5AR_NOT_INITIALIZED;

    struct linked_container *el = ctx->head, *tmp;
    while (el) {
        if (el->container.src.type == FILE_SRC) {
            fclose(el->container.src.fs.stream);
            free((el->container.src.fs.path));
        }

        tmp = el, el = el->next, free(tmp);
    }

    return F5AR_OK;
}

void f5ar_destroy(f5archive *archive) {
    if (!archive->ctx)
        return;

    f5archive_clear_ctx(archive->ctx),
    free(archive->ctx);
}

static inline void export_to(f5archive* archive, char* dest, size_t size) {
    struct linked_container* el = archive->ctx->head;
    while (size)
        memcpy(dest, el->container.hash, MD5_SIZE),
        dest += MD5_SIZE, size--,
        el = el->next;
}

f5ar_blob* f5ar_export_order(f5archive* archive) {
    if (!archive->ctx)
        return NULL;

    const size_t mem_size = archive->ctx->size * MD5_SIZE;
    f5ar_blob* order = malloc(sizeof(f5ar_blob) + mem_size);

    if (!order)
        return NULL;

    order->size = mem_size;
    export_to(archive, order->body, archive->ctx->size);

    return order;
}

f5ar_blob* f5ar_export_order_used(f5archive* archive) {
    if (!archive->ctx)
        return NULL;

    const size_t mem_size = archive->ctx->used * MD5_SIZE;
    f5ar_blob* order = malloc(sizeof(f5ar_blob) + mem_size);

    if (!order)
        return NULL;

    order->size = mem_size;
    export_to(archive, order->body, archive->ctx->used);

    return order;
}

static inline int import_to(f5archive* archive, char* src, size_t size) {
    while (size) {
        append_new(archive);

        if (archive->ctx->tail)
            memcpy(archive->ctx->tail->container.hash, src, MD5_SIZE);
        else
            return F5AR_MALLOC_ERR;

        size -= MD5_SIZE, src += MD5_SIZE;
    }

    return F5AR_OK;
}

int f5ar_import_order(f5archive *archive, f5ar_blob *order) {
    if (!archive->ctx)
        return F5AR_NOT_INITIALIZED;

    f5archive_clear_ctx(archive->ctx);

    return import_to(archive, order->body, order->size);
}

#define abs(x) (x >= 0 ? x : -x)
static f5archive_capacity capacity(container_t *container, struct f5archive_ctx* ctx) {
    f5archive_capacity capacity = {};

    int err = container_open(container, &ctx->err);
    if (err)
        return capacity;

#define height(container) container->jpeg.dstruct.comp_info[0].height_in_blocks
    for (JDIMENSION row_id = 0; row_id < height(container); row_id++) {
#undef height
        JBLOCKROW row = container->jpeg.dstruct.mem->access_virt_barray(
                (j_common_ptr) &container->jpeg.dstruct, container->jpeg.dct_arrays[0],
                row_id, (JDIMENSION) 1, FALSE
        )[0];

#define width(container) container->jpeg.dstruct.comp_info[0].width_in_blocks
        for (size_t block_id = 0; block_id < width(container); block_id++) {
#undef width
            for (unsigned i = 0; i < DCTSIZE2; i++) {
                const JCOEF c = abs(row[block_id][i]);

                capacity.shrinkable += (c == 1) ? 1 : 0;
                capacity.full += (c > 1) ? 1 : 0;
            }
        }
    }

    container_close_discard(container);
    return capacity;
}

/* Will be called only once */
int f5ar_analyze(f5archive *archive) {
    if (!archive->ctx)
        return F5AR_NOT_INITIALIZED;

    archive->capacity.full = 0,
    archive->capacity.shrinkable = 0;

    struct linked_container* el = archive->ctx->head;
    while (el) {
        const f5archive_capacity local = capacity(&el->container, archive->ctx);

        archive->capacity.full += local.full,
        archive->capacity.shrinkable += local.shrinkable,

        el = el->next;
    }

    return F5AR_OK;
}

int f5ar_fill_file(f5archive *archive, const char *path) {
    if (!archive->ctx)
        return F5AR_NOT_INITIALIZED;

    FILE* src = fopen(path, "rb");
    if (!src)
        return F5AR_FILEIO_ERR;

    char hash[MD5_SIZE];
    if (md5_file(src, hash)) {
        fclose(src);
        return F5AR_FILEIO_ERR;
    }

    struct linked_container *el = archive->ctx->head;
    while (el) {
        if (!memcmp(el->container.hash, hash, MD5_SIZE)) {
            fseek(src, 0, SEEK_SET);

            el->container.src.type = FILE_SRC;
            el->container.src.fs.stream = src;

            const int err = copy_fs_path(archive, el, path);
            if (err) {
                fclose(src);
                return err;
            }

            archive->ctx->filled++;
            return (archive->ctx->filled == archive->ctx->size) ? F5AR_OK_COMPLETE : F5AR_OK;
        }

        el = el->next;
    }

    fclose(src);
    return F5AR_NOT_FOUND;
}

int f5ar_fill_mem(f5archive *archive, void *ptr, size_t* size) {
    if (!archive->ctx)
        return F5AR_NOT_INITIALIZED;

    char hash[MD5_SIZE];
    md5_buffer(ptr, *size, hash);

    struct linked_container *el = archive->ctx->head;
    while (el) {
        if (!memcmp(el->container.hash, hash, MD5_SIZE)) {
            el->container.src.type = MEM_SRC;
            el->container.src.mem.ptr = ptr;
            el->container.src.mem.size = size;

            archive->ctx->filled++;
            return (archive->ctx->filled == archive->ctx->size) ? F5AR_OK_COMPLETE : F5AR_OK;
        }

        el = el->next;
    }

    return F5AR_NOT_FOUND;
}

static unsigned f5em(JCOEF **a, size_t n) {
    unsigned hash = 0;
    for (size_t i = 0; i < n;)
        if (*(a[i++]) & 1)
            hash ^= i;
    return hash;
}

static int catch_up(f5archive* archive, struct linked_container** glob, struct linked_container* local) {
    struct linked_container* el = *glob;
    int err = F5AR_OK;

    while (el != local) {
        err = container_close_keep(&el->container, &archive->ctx->err);
        el = el->next, archive->ctx->used++;
    }

    *glob = el;
    return err;
}

static unsigned calc_k(f5archive_capacity arch_capacity, size_t size) {
    unsigned k = 1, capacity;

    while (k < 24) {
        capacity = arch_capacity.full + arch_capacity.shrinkable / k * 2;

        double kn_rate = ((double) k) / ((1 << k) - 1);
        double embed_rate = ((double) (size * 8)) / capacity;

        if (embed_rate >= kn_rate)
            return k-1;
        else
            k++;
    }

    return k;
}

int f5ar_pack(f5archive *archive, const char *data, size_t size) {
    if (!archive->ctx || archive->ctx->filled != archive->ctx->size)
        return F5AR_NOT_COMPLETE;

    archive->ctx->used = 0;
    if (size == 0)
        return F5AR_OK;

    if (archive->capacity.full + archive->capacity.shrinkable == 0)
        f5ar_analyze(archive);

    archive->meta.msg_size = size;
    if (archive->meta.k == 0)
        archive->meta.k = calc_k(archive->capacity, size);
    size_t n = (1 << archive->meta.k) - 1;

    JCOEF** a = malloc(n * sizeof(JCOEF*));
    if (!a) return F5AR_MALLOC_ERR;

    struct linked_container* el = archive->ctx->head;
    int err = container_open(&el->container, &archive->ctx->err);
    if (err) {
        free(a);
        return err;
    }

    unsigned msg_shift = 0;
    size_t msg_i = 0;

    while (msg_i < size && !err) {
        unsigned kword = 0;
        for (unsigned k_shift = 0; k_shift < archive->meta.k && msg_i < size; ++k_shift) {
            kword = (data[msg_i] & (1 << msg_shift)) ? (kword | (1 << k_shift)) : kword;

            msg_shift = (++msg_shift == 8) ? 0 : msg_shift;
            msg_i += (msg_shift == 0) ? 1 : 0;
        }

        struct linked_container* local_el = el;
        size_t ai = 0;

        while (true) {
            while (ai < n && !err) {
                JCOEF coeff = dct_get(local_el->container.dct);
                if (coeff != 0)
                    a[ai++] = &dct_get(local_el->container.dct);

                if (c_next(&local_el->container)) {
                    if (local_el->next) {
                        local_el = local_el->next;
                        err = container_open(&local_el->container, &archive->ctx->err);
                    } else
                        err = F5AR_FAILURE;
                }
            }

            if (err)
                break;

            unsigned s = f5em(a, n) ^ kword;
            if (s == 0)
                break;

            JCOEF val = *(a[s-1]);
            val += (val > 0) ? -1 : 1;

            if ((*a[s-1] = val) != 0)
                break;

            while (s < n)
                a[s-1] = a[s], s++;
            ai = n-1;
        }

        err = catch_up(archive, &el, local_el);
    }

    archive->ctx->used++;
    free(a);

    err = container_close_keep(&el->container, &archive->ctx->err);
    return err;
}

static unsigned f5ex(const JCOEF *a, size_t n) {
    unsigned hash = 0;
    for (size_t i = 0; i < n;)
        if (a[i++] & 1)
            hash ^= i;
    return hash;
}

int f5ar_unpack(f5archive *archive, char **res_ptr, size_t *size) {
    if (archive->ctx->size != archive->ctx->filled)
        return F5AR_NOT_COMPLETE;

    char* msg = calloc(1, archive->meta.msg_size);
    if (!msg) return F5AR_MALLOC_ERR;

    const unsigned k = archive->meta.k, n = (unsigned) ((1 << k) - 1);
    const unsigned k_mask_max = (unsigned) (1 << k);

    unsigned msg_mask = 1;
    size_t msg_i = 0;

    JCOEF* a = malloc(sizeof(JCOEF) * n);
    if (!a)
        return F5AR_MALLOC_ERR;

    struct linked_container *el = archive->ctx->head;
    int err = container_open(&el->container, &archive->ctx->err);

    while (msg_i < archive->meta.msg_size) {
        unsigned ai = 0;
        while (ai < n && !err) {
            const JCOEF coeff = dct_get(el->container.dct);
            if (coeff != 0)
                a[ai++] = coeff;

            if (c_next(&el->container)) {
                container_close_discard(&el->container);
                el = el->next;

                if (el == NULL) {
                    free(a), free(msg), *size = msg_i + 1;
                    return F5AR_FAILURE;
                }

                err = container_open(&el->container, &archive->ctx->err);
            }
        }

        if (err)
            break;

        unsigned k_word = f5ex(a, n), k_mask = 1;
        while (k_mask < k_mask_max && msg_i < archive->meta.msg_size) {
            msg[msg_i] |= (k_word & k_mask) ? msg_mask : 0,
            msg_mask <<= 1, k_mask <<= 1;

            if (msg_mask == 256)
                msg_mask = 1, msg_i++;
        }
    }

    free(a),
    container_close_discard(&el->container);

    *size = archive->meta.msg_size,
    *res_ptr = msg;

    return F5AR_OK;
}