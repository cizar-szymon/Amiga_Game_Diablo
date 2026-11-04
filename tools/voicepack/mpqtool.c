/* Voice pack converter helper (mpqfs from the build tree).
     mpqtool extract ARCHIVE LIST OUTDIR   files named in LIST (one per line) that the
                                           archive has -> OUTDIR/<name with / for \>
     mpqtool pack ARCHIVE LIST INDIR       files of LIST from INDIR -> a new ARCHIVE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "mpqfs/mpqfs.h"

static void mkdirs(char *path)
{
    for (char *p = path + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(path, 0755); *p = '/'; }
}

static char *fslash(const char *name, const char *dir)
{
    static char out[1024];
    snprintf(out, sizeof out, "%s/%s", dir, name);
    for (char *p = out; *p; p++) if (*p == 92) *p = '/'; /* backslash */
    return out;
}

int main(int argc, char **argv)
{
    char line[512];
    int n = 0;
    if (argc < 5) return 5;
    FILE *l = fopen(argv[3], "r");
    if (!l) return 10;
    if (!strcmp(argv[1], "extract")) {
        mpqfs_archive_t *a;
        if (mpqfs_open(argv[2], &a) != 0) return 10;
        while (fgets(line, sizeof line, l)) {
            line[strcspn(line, "\r\n")] = 0;
            void *d = NULL; size_t sz = 0;
            if (!line[0] || mpqfs_read_file(a, line, &d, &sz) != 0) continue;
            char *path = fslash(line, argv[4]);
            mkdirs(path);
            FILE *f = fopen(path, "wb");
            fwrite(d, 1, sz, f);
            fclose(f);
            free(d);
            n++;
        }
        printf("extracted %d files\n", n);
    } else if (!strcmp(argv[1], "pack")) {
        mpqfs_writer_t *w;
        if (mpqfs_writer_create(argv[2], 4096, &w) != 0) return 10;
        while (fgets(line, sizeof line, l)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            FILE *f = fopen(fslash(line, argv[4]), "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *d = malloc(sz);
            fread(d, 1, sz, f);
            fclose(f);
            if (mpqfs_writer_add_file(w, line, d, sz) != 0) { printf("add failed: %s\n", line); return 10; }
            free(d);
            n++;
        }
        if (mpqfs_writer_close(w) != 0) { printf("close failed\n"); return 10; }
        printf("packed %d files\n", n);
    }
    return 0;
}
