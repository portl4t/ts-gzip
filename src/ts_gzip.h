
#ifndef _TS_GZIP_H
#define _TS_GZIP_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>
#include <ts/experimental.h>
#include <zlib.h>

#define TS_GZIP_ZLIB_MEM_LEVEL     8
#define TS_GZIP_COMPRESSION_LEVEL  6
#define TS_GZIP_CRC_LENGTH         8

typedef enum {
    TS_GZIP_STATE_OK = 0,
    TS_GZIP_STATE_DONE = 1,
    TS_GZIP_STATE_ERROR = -1,
} ts_gzip_state;

typedef enum {
    TS_GZIP_TYPE_COMPRESS = 0,
    TS_GZIP_TYPE_DECOMPRESS,
} ts_gzip_type;

typedef struct {
    TSIOBuffer          reserved_buffer;
    TSIOBufferReader    reserved_reader;

    z_stream    z_strm;
    uint32_t    src_len;
    uint32_t    crc;
    int         state;
    int         type;
    int         flags;

    int         writed:1;
} ts_gzip_info;

typedef struct {
    ts_gzip_info        *zinfo;
    TSVIO               output_vio;
    TSIOBuffer          output_buffer;
    TSIOBufferReader    output_reader;

    int64_t             total;
} ts_gzip_transform_ctx;

static u_char  gzheader[10] = {0x1f, 0x8b, Z_DEFLATED, 0, 0, 0, 0, 0, 0, 3};


ts_gzip_info * ts_gzip_deflate_info_create(int flags);
void ts_gzip_deflate_info_destroy(ts_gzip_info *zinfo);

ts_gzip_info * ts_gzip_inflate_info_create(int flags);
void ts_gzip_deflate_info_destroy(ts_gzip_info *zinfo);

int ts_gzip_deflate(ts_gzip_info *zinfo, TSIOBufferReader readerp, TSIOBuffer bufp, int end);       // compress
int ts_gzip_inflate(ts_gzip_info *zinfo, TSIOBufferReader readerp, TSIOBuffer bufp, int end);       // decompress

int ts_gzip_deflate_transform(TSHttpTxn txnp);        // compress
int ts_gzip_inflate_transform(TSHttpTxn txnp);        // decompress

#ifdef  __cplusplus
}
#endif

#endif

