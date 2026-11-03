/* Native SDL2 subset: RWops on files and on memory.
   Read-only files go through dos.library with a 32 KB buffer, so a seek
   inside the buffer costs nothing (libnix's fseek() drops its buffer). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "msdl_internal.h"

SDL_RWops *SDL_AllocRW(void)
{
    SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof(SDL_RWops));
    if (rw == NULL) SDL_SetError("out of memory");
    return rw;
}

void SDL_FreeRW(SDL_RWops *area) { free(area); }

Sint64 SDL_RWsize(SDL_RWops *ctx) { return ctx->size(ctx); }
Sint64 SDL_RWseek(SDL_RWops *ctx, Sint64 offset, int whence) { return ctx->seek(ctx, offset, whence); }
Sint64 SDL_RWtell(SDL_RWops *ctx) { return ctx->seek(ctx, 0, RW_SEEK_CUR); }
size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum) { return ctx->read(ctx, ptr, size, maxnum); }
size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num) { return ctx->write(ctx, ptr, size, num); }
int SDL_RWclose(SDL_RWops *ctx) { return ctx->close(ctx); }

/* ---- files opened for writing: stdio ---- */
static Sint64 f_size(SDL_RWops *ctx)
{
    FILE *f = ctx->hidden.stdio.fp;
    long pos = ftell(f), end;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

static Sint64 f_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    FILE *f = ctx->hidden.stdio.fp;
    int w = whence == RW_SEEK_SET ? SEEK_SET : whence == RW_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (!(whence == RW_SEEK_CUR && offset == 0) && fseek(f, (long)offset, w) != 0) {
        return SDL_SetError("seek failed");
    }
    return ftell(f);
}

static size_t f_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    return fread(ptr, size, maxnum, ctx->hidden.stdio.fp);
}

static size_t f_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    return fwrite(ptr, size, num, ctx->hidden.stdio.fp);
}

static int f_close(SDL_RWops *ctx)
{
    int r = 0;
    if (ctx != NULL) {
        if (fclose(ctx->hidden.stdio.fp) != 0) r = SDL_SetError("close failed");
        SDL_FreeRW(ctx);
    }
    return r;
}

/* ---- read-only files ---- */
#define RBUF 32768

typedef struct {
    BPTR fh;
    long size;
    long pos;           /* logical position */
    long bufpos;        /* file offset of buf[0] */
    long buflen;
    Uint8 buf[RBUF];
} RFile;

static Sint64 r_size(SDL_RWops *ctx) { return ((RFile *)ctx->hidden.unknown.data1)->size; }

static Sint64 r_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    RFile *r = (RFile *)ctx->hidden.unknown.data1;
    long p = whence == RW_SEEK_SET ? (long)offset : whence == RW_SEEK_CUR ? r->pos + (long)offset : r->size + (long)offset;
    if (p < 0) return SDL_SetError("seek before the start");
    r->pos = p;
    return p;
}

static size_t r_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    RFile *r = (RFile *)ctx->hidden.unknown.data1;
    Uint8 *d = (Uint8 *)ptr;
    size_t want, got = 0;
    if (size == 0 || maxnum == 0) return 0;
    want = size * maxnum;
    if (r->pos >= r->size) return 0;
    if ((long)want > r->size - r->pos) want = (size_t)(r->size - r->pos);
    while (got < want) {
        if (r->pos >= r->bufpos && r->pos < r->bufpos + r->buflen) {
            size_t off = (size_t)(r->pos - r->bufpos);
            size_t n = (size_t)r->buflen - off;
            if (n > want - got) n = want - got;
            memcpy(d + got, r->buf + off, n);
            got += n;
            r->pos += (long)n;
        } else if (want - got >= RBUF) {
            LONG n;
            if (Seek(r->fh, r->pos, OFFSET_BEGINNING) < 0) break;
            n = Read(r->fh, d + got, (LONG)(want - got));
            if (n <= 0) break;
            got += (size_t)n;
            r->pos += n;
        } else {
            LONG n;
            if (Seek(r->fh, r->pos, OFFSET_BEGINNING) < 0) break;
            n = Read(r->fh, r->buf, RBUF);
            if (n <= 0) {
                r->buflen = 0;
                break;
            }
            r->bufpos = r->pos;
            r->buflen = n;
        }
    }
    return got / size;
}

static size_t r_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    (void)ctx; (void)ptr; (void)size; (void)num;
    SDL_SetError("file opened for reading");
    return 0;
}

static int r_close(SDL_RWops *ctx)
{
    if (ctx != NULL) {
        RFile *r = (RFile *)ctx->hidden.unknown.data1;
        Close(r->fh);
        free(r);
        SDL_FreeRW(ctx);
    }
    return 0;
}

static SDL_RWops *open_read(const char *file)
{
    RFile *r;
    SDL_RWops *rw;
    BPTR fh = Open((CONST_STRPTR)file, MODE_OLDFILE);
    if (fh == 0) {
        SDL_SetError("Couldn't open %s", file);
        return NULL;
    }
    r = (RFile *)malloc(sizeof(RFile));
    rw = SDL_AllocRW();
    if (r == NULL || rw == NULL) {
        free(r);
        SDL_FreeRW(rw);
        Close(fh);
        SDL_SetError("out of memory");
        return NULL;
    }
    r->fh = fh;
    Seek(fh, 0, OFFSET_END);
    r->size = Seek(fh, 0, OFFSET_BEGINNING);
    r->pos = 0;
    r->bufpos = 0;
    r->buflen = 0;
    rw->size = r_size;
    rw->seek = r_seek;
    rw->read = r_read;
    rw->write = r_write;
    rw->close = r_close;
    rw->type = SDL_RWOPS_STDFILE;
    rw->hidden.unknown.data1 = r;
    return rw;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
    FILE *f;
    SDL_RWops *rw;
    if (file == NULL || mode == NULL) {
        SDL_SetError("SDL_RWFromFile(): no file or mode");
        return NULL;
    }
    if (mode[0] == 'r' && strchr(mode, '+') == NULL) return open_read(file);
    f = fopen(file, mode);
    if (f == NULL) {
        SDL_SetError("Couldn't open %s", file);
        return NULL;
    }
    rw = SDL_AllocRW();
    if (rw == NULL) {
        fclose(f);
        return NULL;
    }
    rw->size = f_size;
    rw->seek = f_seek;
    rw->read = f_read;
    rw->write = f_write;
    rw->close = f_close;
    rw->type = SDL_RWOPS_STDFILE;
    rw->hidden.stdio.autoclose = SDL_TRUE;
    rw->hidden.stdio.fp = f;
    return rw;
}

/* ---- memory ---- */
static Sint64 m_size(SDL_RWops *ctx) { return ctx->hidden.mem.stop - ctx->hidden.mem.base; }

static Sint64 m_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    Uint8 *p;
    switch (whence) {
        case RW_SEEK_SET: p = ctx->hidden.mem.base + (long)offset; break;
        case RW_SEEK_CUR: p = ctx->hidden.mem.here + (long)offset; break;
        case RW_SEEK_END: p = ctx->hidden.mem.stop + (long)offset; break;
        default: return SDL_SetError("unknown whence");
    }
    if (p < ctx->hidden.mem.base) p = ctx->hidden.mem.base;
    if (p > ctx->hidden.mem.stop) p = ctx->hidden.mem.stop;
    ctx->hidden.mem.here = p;
    return p - ctx->hidden.mem.base;
}

static size_t m_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    size_t total, avail;
    if (size == 0 || maxnum == 0) return 0;
    total = size * maxnum;
    avail = (size_t)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
    if (total > avail) total = avail - avail % size;
    memcpy(ptr, ctx->hidden.mem.here, total);
    ctx->hidden.mem.here += total;
    return total / size;
}

static size_t m_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    size_t total, avail;
    if (ctx->type == SDL_RWOPS_MEMORY_RO) {
        SDL_SetError("can't write to read-only memory");
        return 0;
    }
    if (size == 0 || num == 0) return 0;
    total = size * num;
    avail = (size_t)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
    if (total > avail) total = avail - avail % size;
    memcpy(ctx->hidden.mem.here, ptr, total);
    ctx->hidden.mem.here += total;
    return total / size;
}

static int m_close(SDL_RWops *ctx)
{
    SDL_FreeRW(ctx);
    return 0;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
    SDL_RWops *rw = SDL_AllocRW();
    if (rw == NULL) return NULL;
    rw->size = m_size;
    rw->seek = m_seek;
    rw->read = m_read;
    rw->write = m_write;
    rw->close = m_close;
    rw->type = SDL_RWOPS_MEMORY;
    rw->hidden.mem.base = rw->hidden.mem.here = (Uint8 *)mem;
    rw->hidden.mem.stop = (Uint8 *)mem + size;
    return rw;
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    SDL_RWops *rw = SDL_RWFromMem((void *)mem, size);
    if (rw != NULL) rw->type = SDL_RWOPS_MEMORY_RO;
    return rw;
}
