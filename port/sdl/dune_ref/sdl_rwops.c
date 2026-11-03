/* Mini-SDL2: RWops on stdio files and on memory. */
#include <stdio.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <SDL.h>

#define RW_FILE 2
#define RW_MEM  4
#define RW_CMEM 5

SDL_RWops *SDL_AllocRW(void)
{
    SDL_RWops *rw = (SDL_RWops *)calloc(1, sizeof(SDL_RWops));
    if (rw == NULL) SDL_SetError("out of memory");
    return rw;
}

void SDL_FreeRW(SDL_RWops *area) { free(area); }

/* ---- files ---- */
static Sint64 f_size(SDL_RWops *ctx)
{
    FILE *f = (FILE *)ctx->hidden.stdio.fp;
    long pos = ftell(f), end;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

static Sint64 f_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    FILE *f = (FILE *)ctx->hidden.stdio.fp;
    int w = whence == RW_SEEK_SET ? SEEK_SET : whence == RW_SEEK_CUR ? SEEK_CUR : SEEK_END;
    if (!(whence == RW_SEEK_CUR && offset == 0) && fseek(f, (long)offset, w) != 0) {
        return SDL_SetError("seek failed");
    }
    return ftell(f);
}

static size_t f_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    return fread(ptr, size, maxnum, (FILE *)ctx->hidden.stdio.fp);
}

static size_t f_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    return fwrite(ptr, size, num, (FILE *)ctx->hidden.stdio.fp);
}

static int f_close(SDL_RWops *ctx)
{
    int r = 0;
    if (ctx != NULL) {
        if (fclose((FILE *)ctx->hidden.stdio.fp) != 0) r = SDL_SetError("close failed");
        SDL_FreeRW(ctx);
    }
    return r;
}

/* ---- read-only files: dos.library with a 32 KB buffer ----
   The PAK reader seeks before every read, also before each single byte of
   a VOC header; libnix's fseek() drops its buffer each time, so every byte
   became a Seek() and Read() on the disk (half of the 49 s start-up was
   waiting for them). Here a seek inside the buffer costs nothing. */
#define RBUF 32768

typedef struct {
    BPTR fh;
    Sint64 size;
    Sint64 pos;         /* logical position */
    Sint64 bufpos;      /* file offset of buf[0] */
    LONG buflen;
    Uint8 buf[RBUF];
} RFile;

static Sint64 r_size(SDL_RWops *ctx) { return ((RFile *)ctx->hidden.unknown.data1)->size; }

static Sint64 r_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    RFile *r = (RFile *)ctx->hidden.unknown.data1;
    Sint64 p = whence == RW_SEEK_SET ? offset : whence == RW_SEEK_CUR ? r->pos + offset : r->size + offset;
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
    if ((Sint64)want > r->size - r->pos) want = (size_t)(r->size - r->pos);
    while (got < want) {
        if (r->pos >= r->bufpos && r->pos < r->bufpos + r->buflen) {
            size_t off = (size_t)(r->pos - r->bufpos);
            size_t n = (size_t)r->buflen - off;
            if (n > want - got) n = want - got;
            memcpy(d + got, r->buf + off, n);
            got += n;
            r->pos += n;
        } else if (want - got >= RBUF) {
            /* big reads straight into the caller's memory */
            LONG n;
            if (Seek(r->fh, (LONG)r->pos, OFFSET_BEGINNING) < 0) break;
            n = Read(r->fh, d + got, (LONG)(want - got));
            if (n <= 0) break;
            got += n;
            r->pos += n;
        } else {
            LONG n;
            if (Seek(r->fh, (LONG)r->pos, OFFSET_BEGINNING) < 0) break;
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
    rw->type = RW_FILE;
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
    rw->type = RW_FILE;
    rw->hidden.stdio.fp = f;
    return rw;
}

/* ---- memory ---- */
static Sint64 m_size(SDL_RWops *ctx) { return ctx->hidden.mem.stop - ctx->hidden.mem.base; }

static Sint64 m_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    Uint8 *p;
    switch (whence) {
        case RW_SEEK_SET: p = ctx->hidden.mem.base + offset; break;
        case RW_SEEK_CUR: p = ctx->hidden.mem.here + offset; break;
        case RW_SEEK_END: p = ctx->hidden.mem.stop + offset; break;
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
    if (ctx->type == RW_CMEM) {
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
    rw->type = RW_MEM;
    rw->hidden.mem.base = rw->hidden.mem.here = (Uint8 *)mem;
    rw->hidden.mem.stop = (Uint8 *)mem + size;
    return rw;
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    SDL_RWops *rw = SDL_RWFromMem((void *)mem, size);
    if (rw != NULL) rw->type = RW_CMEM;
    return rw;
}
