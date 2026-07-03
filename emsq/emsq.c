/* see copyright notice in squirrel.h */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define EMSQ_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define EMSQ_KEEPALIVE
#endif

#include <squirrel.h>
#include <sqstdaux.h>
#include <sqstdblob.h>
#include <sqstdio.h>
#include <sqstdmath.h>
#include <sqstdstring.h>

#ifdef SQUNICODE
#error "emsq supports the default UTF-8/char Squirrel build only."
#endif

typedef struct EmsqBuffer {
    char *data;
    size_t length;
    size_t capacity;
} EmsqBuffer;

static HSQUIRRELVM g_vm = NULL;
static EmsqBuffer g_output = { NULL, 0, 0 };
static EmsqBuffer g_error = { NULL, 0, 0 };

static int emsq_buffer_reserve(EmsqBuffer *buffer, size_t capacity)
{
    char *data;
    size_t newcapacity = buffer->capacity ? buffer->capacity : 256;
    if(capacity <= buffer->capacity) {
        return 0;
    }
    while(newcapacity < capacity) {
        newcapacity *= 2;
    }
    data = (char *)realloc(buffer->data, newcapacity);
    if(!data) {
        return -1;
    }
    buffer->data = data;
    buffer->capacity = newcapacity;
    return 0;
}

static void emsq_buffer_clear(EmsqBuffer *buffer)
{
    buffer->length = 0;
    if(buffer->data) {
        buffer->data[0] = '\0';
    }
}

static void emsq_buffer_free(EmsqBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void emsq_buffer_append(EmsqBuffer *buffer, const char *text)
{
    size_t length;
    if(!text) {
        return;
    }
    length = strlen(text);
    if(emsq_buffer_reserve(buffer, buffer->length + length + 1) != 0) {
        return;
    }
    memcpy(buffer->data + buffer->length, text, length + 1);
    buffer->length += length;
}

static void emsq_buffer_append_vformat(EmsqBuffer *buffer, const char *format, va_list args)
{
    int needed;
    size_t offset;
    va_list copy;
    if(!format) {
        return;
    }
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if(needed < 0) {
        return;
    }
    offset = buffer->length;
    if(emsq_buffer_reserve(buffer, offset + (size_t)needed + 1) != 0) {
        return;
    }
    vsnprintf(buffer->data + offset, (size_t)needed + 1, format, args);
    buffer->length = offset + (size_t)needed;
}

static void emsq_printfunc(HSQUIRRELVM v, const SQChar *s, ...)
{
    va_list vl;
    (void)v;
    va_start(vl, s);
    emsq_buffer_append_vformat(&g_output, s, vl);
    va_end(vl);
}

static void emsq_errorfunc(HSQUIRRELVM v, const SQChar *s, ...)
{
    va_list vl;
    (void)v;
    va_start(vl, s);
    emsq_buffer_append_vformat(&g_error, s, vl);
    va_end(vl);
}

static void emsq_reset_buffers(void)
{
    emsq_buffer_clear(&g_output);
    emsq_buffer_clear(&g_error);
}

static void emsq_append_lasterror(const char *prefix)
{
    const SQChar *err = NULL;
    if(!g_vm) {
        return;
    }
    sq_getlasterror(g_vm);
    if(SQ_SUCCEEDED(sq_getstring(g_vm, -1, &err)) && err) {
        emsq_buffer_append(&g_error, prefix);
        emsq_buffer_append(&g_error, err);
        emsq_buffer_append(&g_error, "\n");
    }
    else if(prefix) {
        emsq_buffer_append(&g_error, prefix);
        emsq_buffer_append(&g_error, "unknown error\n");
    }
    sq_pop(g_vm, 1);
}

static int emsq_call_loaded_closure(SQInteger oldtop)
{
    sq_pushroottable(g_vm);
    if(SQ_FAILED(sq_call(g_vm, 1, SQFalse, SQTrue))) {
        sq_settop(g_vm, oldtop);
        if(g_error.length == 0) {
            emsq_append_lasterror("runtime error: ");
        }
        return -1;
    }
    sq_settop(g_vm, oldtop);
    return 0;
}

EMSQ_KEEPALIVE int emsq_init(void)
{
    if(g_vm) {
        return 0;
    }

    emsq_reset_buffers();
    g_vm = sq_open(1024);
    if(!g_vm) {
        emsq_buffer_append(&g_error, "sq_open failed\n");
        return -1;
    }

    sq_setprintfunc(g_vm, emsq_printfunc, emsq_errorfunc);
    sq_pushroottable(g_vm);
    sqstd_register_bloblib(g_vm);
    sqstd_register_iolib(g_vm);
    sqstd_register_mathlib(g_vm);
    sqstd_register_stringlib(g_vm);
    sqstd_seterrorhandlers(g_vm);
    sq_pop(g_vm, 1);
    return 0;
}

EMSQ_KEEPALIVE void emsq_shutdown(void)
{
    if(g_vm) {
        sq_close(g_vm);
        g_vm = NULL;
    }
    emsq_buffer_free(&g_output);
    emsq_buffer_free(&g_error);
}

EMSQ_KEEPALIVE int emsq_run_string(const char *source, const char *source_name)
{
    SQInteger oldtop;
    if(emsq_init() != 0) {
        return -1;
    }
    emsq_reset_buffers();
    if(!source) {
        emsq_buffer_append(&g_error, "source is null\n");
        return -1;
    }

    oldtop = sq_gettop(g_vm);
    if(SQ_FAILED(sq_compilebuffer(g_vm, source, (SQInteger)strlen(source),
                                  source_name ? source_name : "emsq",
                                  SQTrue))) {
        sq_settop(g_vm, oldtop);
        if(g_error.length == 0) {
            emsq_append_lasterror("compile error: ");
        }
        return -1;
    }
    return emsq_call_loaded_closure(oldtop);
}

EMSQ_KEEPALIVE int emsq_run_file(const char *path)
{
    SQInteger oldtop;
    if(emsq_init() != 0) {
        return -1;
    }
    emsq_reset_buffers();
    if(!path) {
        emsq_buffer_append(&g_error, "path is null\n");
        return -1;
    }

    oldtop = sq_gettop(g_vm);
    if(SQ_FAILED(sqstd_loadfile(g_vm, path, SQTrue))) {
        sq_settop(g_vm, oldtop);
        if(g_error.length == 0) {
            emsq_append_lasterror("load error: ");
        }
        return -1;
    }
    return emsq_call_loaded_closure(oldtop);
}

EMSQ_KEEPALIVE const char *emsq_get_output(void)
{
    return g_output.data ? g_output.data : "";
}

EMSQ_KEEPALIVE void emsq_clear_output(void)
{
    emsq_reset_buffers();
}

EMSQ_KEEPALIVE const char *emsq_get_error(void)
{
    return g_error.data ? g_error.data : "";
}
