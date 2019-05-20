/*
* Written by Labunsky Artem <me@labunsky.info>
*
* This file is distributed under the Beerware licence.
*/

#include "f5ar_cmd.h"
#include "f5ar.h"

#include <tinydir.h>
#include <regex.h>
#include <time.h>

#include "f5ar_utils.c"

#define check_throw(action, err) err = action; if (err) return err

static int fill_w_regex(f5archive *archive, const char *path, const regex_t *reg) {
    const size_t path_len = strlen(path);
    if (path_len >= FILENAME_MAX - 1)
        return F5AR_OK;

    tinydir_dir dir = {};
    tinydir_open(&dir, path);

    int err = F5AR_OK;
    while (dir.has_next && !err) {
        tinydir_file file;
        tinydir_readfile(&dir, &file);

        if (file.name[0] == '.')
            goto NEXT;

        if (!file.is_dir) {
            if (regexec(reg, file.name, 0, 0, 0))
                goto NEXT;

            if (f5ar_add_file(archive, file.path))
                err = -3;
        } else
            err = fill_w_regex(archive, file.path, reg);

        NEXT: tinydir_next(&dir);
    }

    tinydir_close(&dir);
    return err;
}

static int fill_w_hashes(f5archive *archive, const char *path) {
    const size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= FILENAME_MAX - 1)
        return F5AR_OK;

    tinydir_dir dir = {};
    tinydir_open(&dir, path);

    int err = F5AR_OK;
    while (dir.has_next && !err) {
        tinydir_file file;
        tinydir_readfile(&dir, &file);

        if (file.name[0] == '.')
            goto NEXT;

        if (!file.is_dir) {
            err = f5ar_fill_file(archive, file.path);
            switch (err) {
                case F5AR_OK_COMPLETE:
                    return F5AR_OK;
                case F5AR_NOT_FOUND:
                    err = 0;
            }
        } else
            err = fill_w_hashes(archive, file.path);

        NEXT: tinydir_next(&dir);
    }

    tinydir_close(&dir);
    return F5AR_FAILURE;
}

#define fread_err(dest, size, file) fread(dest, 1, size, file) != size
static int archive_read(f5archive *archive, const char *path) {
    int err = 0;

    FILE* in = fopen(path, "rb");
    if (!in) {
        err = F5AR_FILEIO_ERR;
        goto EXIT;
    }

    uint64_t order_size;
    if (fread_err(&archive->meta.k, sizeof(uint8_t), in) ||
        fread_err(&archive->meta.msg_size, sizeof(uint64_t), in) ||
        fread_err(&order_size, sizeof(uint64_t), in)) {
        err = F5AR_FILEIO_ERR;
        goto CLOSE;
    }

    f5ar_blob* order = malloc(sizeof(f5ar_blob) + order_size);
    if (!order) {
        err = F5AR_MALLOC_ERR;
        goto CLOSE;
    }

    order->size = order_size;
    if (fread_err(order->body, order_size, in)) {
        err = F5AR_FILEIO_ERR;
        goto CLOSE;
    }

    err = f5ar_import_order(archive, order);

    CLOSE: fclose(in);
    EXIT: return err;
}

#define fwrite_err(dest, size, file) fwrite(dest, 1, size, file) != size
static int archive_write(f5archive* archive, const char* path) {
    int err = F5AR_OK;
    const f5ar_blob* order = f5ar_export_order_used(archive);

    if (!order) {
        err = F5AR_MALLOC_ERR;
        goto EXIT;
    }

    FILE* out = fopen(path, "wb");
    if (!out) {
        err = F5AR_FILEIO_ERR;
        goto EXIT;
    }

    const uint64_t order_size64 = order->size;
    if (fwrite_err(&archive->meta.k, sizeof(uint8_t), out) ||
        fwrite_err(&archive->meta.msg_size, sizeof(uint64_t), out) ||
        fwrite_err(&order_size64, sizeof(uint64_t), out) ||
        fwrite_err(order->body, order->size, out))
        err = F5AR_FILEIO_ERR;

    fclose(out);
    EXIT: return err;
}

static void usage(char* argv[], int verbose) {
    if (!verbose)
        return;

    printf("Usage: %s [FLAG] [[ARGS]]\n\n", argv[0]);

    printf("Usable flags are:\n");
    printf("-p [folder] [regex] [file] [name]    \nCompress [file] in ([folder], [regex]) library to [archive name]\n\n");
    printf("-u [archive] [file]                  \nDecompress [archive] and write result to the [file]\n\n");
    printf("-a [folder] [regex]                  \nAnalyse ([folder], [regex]) library capacity\n\n");

    printf("Examples:\n\n");
    printf("Compress in.txt into *.jpg files in dogs folder and save as doge.arch:\n");
    printf("%s -p dogs/ .*\\.jpg in.txt doge.arch\n\n", argv[0]);

    printf("Decompress doge.arch in dogs/ folder to out.txt:\n");
    printf("%s -u dogs/doge.arch out.txt\n", argv[0]);
}

static void check_capacity(f5archive archive, size_t msg_size, int verbose) {
    if (!verbose)
        return;

    printf("Detected somewhat guaranteed capacity of %zu bytes\nDetected possible capacity of upto %zu bytes\n",
           archive.capacity.full / 8,
           (archive.capacity.full + archive.capacity.shrinkable - 1) / 8
    );

    if (archive.capacity.full / 8 < msg_size) {
        printf("Warning, file size is larger than guaranteed library capacity by %zu bytes.\n",
               msg_size - archive.capacity.full / 8);
        printf("Compression could not be successful. Continue?\n");

        char yn;
        do {
            printf("[Y/N]: ");
        } while (scanf("%c", &yn) && yn != 'Y' && yn != 'N');

        if (yn == 'N') {
            printf("Exiting...\n");
            exit(0);
        }
    }
}

#define do_timed_action(descr, action, verbose) {\
    clock_t begin = clock();\
    if (verbose) printf(#descr "...");\
    fflush(stdout);\
    {action;}\
    const double time_spent = (double) (clock() - begin) / CLOCKS_PER_SEC;\
    if (verbose) {if (time_spent > 0.1) printf(" done in %.1fs\n", time_spent);\
    else printf(" ok\n");}\
}

int f5ar_cmd_exec(int argc, char* argv[], int verbose) {
    int err;

    if (argc < 2) {
        usage(argv, verbose);
        return F5AR_WRONG_ARGS;
    }

    f5archive archive;
    memset(&archive, 0, sizeof(f5archive));

    switch (argv[1][1]) {
        case 'p': {
            if (argc < 6) {
                if (verbose) usage(argv, verbose);
                return F5AR_WRONG_ARGS;
            }

            size_t msg_size = 0;
            char *msg;
            do_timed_action(Reading compressing file, ({
                msg = file_read(argv[4], &msg_size);
                if (!msg) {
                    if (verbose) printf("\nError reading file %s\n", argv[4]);
                    return F5AR_FILEIO_ERR;
                }
            }), verbose);

            do_timed_action(Initializing the archive, ({
                regex_t regex;
                if (regcomp(&regex, argv[3], REG_EXTENDED | REG_NOSUB)) {
                    if (verbose) printf("Error compiling given regular expression");
                    return F5AR_WRONG_ARGS;
                }

                check_throw(f5ar_init(&archive), err);
                fill_w_regex(&archive, argv[2], &regex);
                regfree(&regex);
            }), verbose);

            do_timed_action(Analysing library capacity, check_throw(f5ar_analyze(&archive), err), verbose);
            check_capacity(archive, msg_size, verbose);

            do_timed_action(Compressing, ({
                err = f5ar_pack(&archive, msg, msg_size);
                if (err == F5AR_FAILURE && verbose)
                    if (verbose) printf("Not enough capacity\n");
                if (err) return err;
            }), verbose);

            do_timed_action(Saving the archive, ({
                char archive_path[FILENAME_MAX];
                memset(archive_path, 0, FILENAME_MAX);
                strncpy(archive_path, argv[2], strlen(argv[2]));

                const size_t path_len = strlen(argv[2]);
                archive_path[path_len] = '/';
                strncpy(archive_path + path_len + 1, argv[5], strlen(argv[5]));

                check_throw(archive_write(&archive, archive_path), err);
            }), verbose);
        } break;

        case 'u': {
            if (argc < 4) {
                usage(argv, verbose);
                return F5AR_WRONG_ARGS;
            }

            do_timed_action(Initializing the archive, check_throw(f5ar_init(&archive), err), verbose);
            do_timed_action(Reading the archive file, archive_read(&archive, argv[2]), verbose);

            do_timed_action(Filling the archive with files, ({
                char dir_path[FILENAME_MAX];
                memset(dir_path, 0, FILENAME_MAX);

                extract_dir_path(dir_path, argv[2]);

                if (fill_w_hashes(&archive, dir_path))
                    return F5AR_NOT_COMPLETE;
            }), verbose);

            size_t msg_size = 0;
            char *msg_ptr;
            do_timed_action(Decompressing, ({
                err = f5ar_unpack(&archive, &msg_ptr, &msg_size);
                if (err) {
                    if (verbose) printf("Read %zu bytes, %zu expected\n", msg_size, archive.meta.msg_size);
                    return err;
                }
            }), verbose);

            do_timed_action(Writing extracted data, ({
                if (file_write(argv[3], msg_ptr, msg_size))
                    return F5AR_FILEIO_ERR;
            }), verbose);
        } break;

        case 'a': {
            if (argc < 4) {
                usage(argv, verbose);
                return F5AR_WRONG_ARGS;
            }

            do_timed_action(Initializing the archive, ({
                regex_t regex;
                if (regcomp(&regex, argv[3], REG_EXTENDED | REG_NOSUB)) {
                    if (verbose) printf("Error compiling given regular expression");
                    return F5AR_WRONG_ARGS;
                }

                check_throw(f5ar_init(&archive), err);
                fill_w_regex(&archive, argv[2], &regex);
                regfree(&regex);
            }), verbose);

            do_timed_action(Analysing library capacity, f5ar_analyze(&archive), verbose);

            printf("Detected somewhatguaranteed capacity of %zu bytes\nDetected possible capacity of upto %zu bytes\n",
                   archive.capacity.full / 8,
                   (archive.capacity.full + archive.capacity.shrinkable) / 8
            );
        } break;

        default:
            usage(argv, verbose);
            return F5AR_WRONG_ARGS;
    }

    f5ar_destroy(&archive);
    return F5AR_OK;
}

int main(int argc, char* argv[]) {
    return f5ar_cmd_exec(argc, argv, 1);
}