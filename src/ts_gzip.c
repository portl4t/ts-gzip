
#include "ts_gzip.h"

static ts_gzip_info * ts_gzip_info_create(int type, int flags);
static void ts_gzip_info_destroy(ts_gzip_info *zinfo);
static int ts_gzip_transform_entry(TSCont contp, TSEvent event, void *edata);
static void ts_gzip_destroy_transform_ctx(ts_gzip_transform_ctx *transform_ctx);
static int ts_gzip_transform_handler(TSCont contp, ts_gzip_transform_ctx *transform_ctx);


static ts_gzip_info *
ts_gzip_info_create(int type, int flags)
{
    ts_gzip_info    *zinfo;

    zinfo = (ts_gzip_info*)TSmalloc(sizeof(ts_gzip_info));

    if (zinfo == NULL)
        return NULL;

    zinfo->z_strm.zalloc = Z_NULL;
    zinfo->z_strm.zfree = Z_NULL;
    zinfo->z_strm.opaque = Z_NULL;
    zinfo->z_strm.next_in = 0;
    zinfo->z_strm.avail_in = 0;

    if (type == TS_GZIP_TYPE_COMPRESS) {
        deflateInit2(&zinfo->z_strm, TS_GZIP_COMPRESSION_LEVEL, Z_DEFLATED, -MAX_WBITS, TS_GZIP_ZLIB_MEM_LEVEL, Z_DEFAULT_STRATEGY);

    } else {
        inflateInit2(&zinfo->z_strm, -MAX_WBITS);
    }

    zinfo->type = type;
    zinfo->state = 0;
    zinfo->src_len = 0;
    zinfo->flags = flags;
    zinfo->crc = crc32(0, Z_NULL, 0);
    zinfo->writed = 0;

    return zinfo;
}

static void
ts_gzip_info_destroy(ts_gzip_info *zinfo)
{
    if (!zinfo)
        return;

    if (zinfo->state != TS_GZIP_STATE_DONE) {

        if (zinfo->type == TS_GZIP_TYPE_COMPRESS) {
            deflateEnd(&zinfo->z_strm);

        } else {
            inflateEnd(&zinfo->z_strm);
        }
    }

    TSfree(zinfo);
}

ts_gzip_info *
ts_gzip_deflate_info_create(int flags)
{
    return ts_gzip_info_create(TS_GZIP_TYPE_COMPRESS, flags);
}

ts_gzip_info *
ts_gzip_inflate_info_create(int flags)
{
    return ts_gzip_info_create(TS_GZIP_TYPE_DECOMPRESS, flags);
}

void
ts_gzip_deflate_info_destroy(ts_gzip_info *zinfo)
{
    return ts_gzip_info_destroy(zinfo);
}

void
ts_gzip_inflate_info_destroy(ts_gzip_info *zinfo)
{
    return ts_gzip_info_destroy(zinfo);
}

int
ts_gzip_deflate(ts_gzip_info *zinfo, TSIOBufferReader readerp, TSIOBuffer bufp, int end)
{
    int64_t             bytes, avail, wavail;
    char                *start;
    int                 tail[2];
    int                 wlen, flush, res;
    TSIOBufferBlock     blk, write_blk;

    if (zinfo->state == TS_GZIP_STATE_DONE || zinfo->state == TS_GZIP_STATE_ERROR) {
        return -1;
    }

    blk = TSIOBufferReaderStart(readerp);
    write_blk = TSIOBufferStart(bufp);

    avail = TSIOBufferReaderAvail(readerp);
    flush = 0;

    if (!zinfo->writed) {
        TSIOBufferWrite(bufp, gzheader, sizeof(gzheader));
        zinfo->writed = 1;
    }

    zinfo->src_len += avail;

    do {
        if (avail && blk) {
            zinfo->z_strm.next_in = (Bytef*)TSIOBufferBlockReadStart(blk, readerp, &bytes);
            zinfo->z_strm.avail_in = bytes;
            zinfo->crc = crc32(zinfo->crc, zinfo->z_strm.next_in, zinfo->z_strm.avail_in);

        } else if (end) {
            flush = Z_FINISH;

        } else {
            break;
        }

        do {
            start = TSIOBufferBlockWriteStart(write_blk, &wavail);
            zinfo->z_strm.next_out = (Bytef*)start;

            if (wavail == 0) {
                write_blk = TSIOBufferStart(bufp);
                continue;
            }

            zinfo->z_strm.avail_out = wavail;
            res = deflate(&zinfo->z_strm, flush); 

            if (res == Z_OK || res == Z_STREAM_END || res == Z_BUF_ERROR) {
                wlen = wavail - zinfo->z_strm.avail_out;
                TSIOBufferProduce(bufp, wlen);

            } else {
                return -1;
            }

            if (res == Z_STREAM_END) {
                break;

            } else if (!zinfo->z_strm.avail_in && flush != Z_FINISH) {
                break;
            }

        } while (1);

        if (res == Z_STREAM_END) {
            deflateEnd(&zinfo->z_strm);
            zinfo->state = TS_GZIP_STATE_DONE;

            tail[0] = zinfo->crc;
            tail[1] = zinfo->src_len;
            TSIOBufferWrite(bufp, tail, sizeof(tail));
            break;

        } else  if (blk) {
            blk = TSIOBufferBlockNext(blk);
        }

    } while (1);

    TSIOBufferReaderConsume(readerp, avail);

    return 0;
}

int
ts_gzip_inflate(ts_gzip_info *zinfo, TSIOBufferReader readerp, TSIOBuffer bufp, int end)
{
    int64_t             bytes, avail, wavail, ravail, already, left, need;
    const char          *start;
    int                 tail[2];
    int                 wlen, flush, res;
    TSIOBufferBlock     blk, write_blk;

    if (zinfo->state == TS_GZIP_STATE_DONE || zinfo->state == TS_GZIP_STATE_ERROR) {
        return -1;
    }

    avail = TSIOBufferReaderAvail(readerp);

    if (!zinfo->writed) {

        if (avail < sizeof(gzheader))
            return 0;

        zinfo->writed = 1;
        TSIOBufferReaderConsume(readerp, sizeof(gzheader));

        avail = TSIOBufferReaderAvail(readerp);
    }

    blk = TSIOBufferReaderStart(readerp);

    if (avail <= TS_GZIP_CRC_LENGTH)
        return 0;

    ravail = avail - TS_GZIP_CRC_LENGTH;        // real use

    write_blk = TSIOBufferStart(bufp);
    flush = 0;
    already = 0;

    do {
        if (ravail && blk && already < ravail) {
            zinfo->z_strm.next_in = (Bytef*)TSIOBufferBlockReadStart(blk, readerp, &bytes);

            if (already + bytes > ravail) {
                zinfo->z_strm.avail_in = ravail - already;
                already = ravail;

            } else {
                zinfo->z_strm.avail_in = bytes;
                already += bytes;
            }

        } else if (end) {
            flush = Z_FINISH;

        } else {
            break;
        }

        do {
            start = TSIOBufferBlockWriteStart(write_blk, &wavail);
            zinfo->z_strm.next_out = (Bytef*)start;

            if (wavail == 0) {
                write_blk = TSIOBufferStart(bufp);
                continue;
            }

            zinfo->z_strm.avail_out = wavail;
            res = inflate(&zinfo->z_strm, flush); 

            if (res == Z_OK || res == Z_STREAM_END || res == Z_BUF_ERROR) {
                wlen = wavail - zinfo->z_strm.avail_out;
                TSIOBufferProduce(bufp, wlen);

                zinfo->src_len += wlen;
                zinfo->crc = crc32(zinfo->crc, (Bytef*)start, wlen);

            } else {
                zinfo->state = TS_GZIP_STATE_ERROR;
                return -1;
            }

            if (res == Z_STREAM_END) {
                break;

            } else if (!zinfo->z_strm.avail_in && flush != Z_FINISH) {
                break;
            }

        } while (1);

        if (res == Z_STREAM_END) {
            inflateEnd(&zinfo->z_strm);
            zinfo->state = TS_GZIP_STATE_DONE;

            break;

        } else  if (blk) {
            blk = TSIOBufferBlockNext(blk);
        }

    } while (1);

    TSIOBufferReaderConsume(readerp, ravail);

    if (end) {
        left = TSIOBufferReaderAvail(readerp);
        if (left < TS_GZIP_CRC_LENGTH)
            return -1;

        blk = TSIOBufferReaderStart(readerp);
        need = already = 0;

        do {
            start = TSIOBufferBlockReadStart(blk, readerp, &bytes);

            if (already + bytes >= TS_GZIP_CRC_LENGTH) {
                need = TS_GZIP_CRC_LENGTH - already;

            } else {
                need = bytes;
            }

            memcpy((char*)&tail + already, start, need);
            already += need;
            if (already >= TS_GZIP_CRC_LENGTH)
                break;

            blk = TSIOBufferBlockNext(blk);
        } while (blk);

        TSIOBufferReaderConsume(readerp, left);
        if (tail[0] != zinfo->crc || tail[1] != zinfo->src_len)
            return -1;
    }

    return zinfo->state;
}

int
ts_gzip_deflate_transform(TSHttpTxn txnp)
{
    TSVConn                 connp;
    ts_gzip_transform_ctx   *ctx;

    ctx = (ts_gzip_transform_ctx*)TSmalloc(sizeof(ts_gzip_transform_ctx));
    memset(ctx, 0, sizeof(ts_gzip_transform_ctx));
    ctx->zinfo = ts_gzip_deflate_info_create(0);

    connp = TSTransformCreate(ts_gzip_transform_entry, txnp);
    TSContDataSet(connp, ctx);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

    return  0;
}

int
ts_gzip_inflate_transform(TSHttpTxn txnp)
{
    TSVConn                 connp;
    ts_gzip_transform_ctx   *ctx;

    ctx = (ts_gzip_transform_ctx*)TSmalloc(sizeof(ts_gzip_transform_ctx));
    memset(ctx, 0, sizeof(ts_gzip_transform_ctx));
    ctx->zinfo = ts_gzip_inflate_info_create(0);

    connp = TSTransformCreate(ts_gzip_transform_entry, txnp);
    TSContDataSet(connp, ctx);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

    return 0;
}

static int
ts_gzip_transform_entry(TSCont contp, TSEvent event, void *edata)
{
    TSVIO       input_vio;

    ts_gzip_transform_ctx *transform_ctx = (ts_gzip_transform_ctx*)TSContDataGet(contp);

    if (TSVConnClosedGet(contp)) {
        TSContDestroy(contp);
        ts_gzip_destroy_transform_ctx(transform_ctx);
        return 0;
    }

    switch (event) {

        case TS_EVENT_ERROR:
            input_vio = TSVConnWriteVIOGet(contp);
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
            break;

        case TS_EVENT_VCONN_WRITE_COMPLETE:
            TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
            break;

        case TS_EVENT_VCONN_WRITE_READY:
        default:
            ts_gzip_transform_handler(contp, transform_ctx);
            break;
    }

    return 0;
}

static void
ts_gzip_destroy_transform_ctx(ts_gzip_transform_ctx *transform_ctx)
{
    if (!transform_ctx)
        return;

    if (transform_ctx->output_reader)
        TSIOBufferReaderFree(transform_ctx->output_reader);

    if (transform_ctx->output_buffer)
        TSIOBufferDestroy(transform_ctx->output_buffer);

    ts_gzip_info_destroy(transform_ctx->zinfo);

    TSfree(transform_ctx);
}

static int
ts_gzip_transform_handler(TSCont contp, ts_gzip_transform_ctx *tc)
{
    TSVConn             output_conn;
    TSVIO               input_vio;
    TSIOBufferReader    input_reader;
    int64_t             towrite, upstream_done, avail, left, writen;
    int                 ret, eos;

    output_conn = TSTransformOutputVConnGet(contp);
    input_vio = TSVConnWriteVIOGet(contp);
    input_reader = TSVIOReaderGet(input_vio);

    if (!tc->output_buffer) {
        tc->output_buffer = TSIOBufferCreate();
        tc->output_reader = TSIOBufferReaderAlloc(tc->output_buffer);
        tc->output_vio = TSVConnWrite(output_conn, contp, tc->output_reader, INT64_MAX);
    }

    if (!TSVIOBufferGet(input_vio)) {
        TSVIONBytesSet(tc->output_vio, tc->total);
        TSVIOReenable(tc->output_vio);
        return 1;
    }

    towrite = TSVIONTodoGet(input_vio);
    upstream_done = TSVIONDoneGet(input_vio);

    avail = TSIOBufferReaderAvail(input_reader);

    if (towrite > avail) {
        towrite = avail;
        eos = 0;

    } else {
        eos = 1;
    }

    if (tc->zinfo->state == TS_GZIP_STATE_OK) {

        if (tc->zinfo->type == TS_GZIP_TYPE_COMPRESS) {
            ret = ts_gzip_deflate(tc->zinfo, input_reader, tc->output_buffer, eos);

        } else {
            ret = ts_gzip_inflate(tc->zinfo, input_reader, tc->output_buffer, eos);
        }

    } else {
        TSIOBufferReaderConsume(input_reader, towrite);
        ret = 0;
    }

    left = TSIOBufferReaderAvail(input_reader);
    writen = towrite - left;

    if (ret < 0 || eos) {
        TSVIONDoneSet(input_vio, upstream_done + writen);
        tc->total = TSVIONDoneGet(tc->output_vio) + TSIOBufferReaderAvail(tc->output_reader);
        TSVIONBytesSet(tc->output_vio, tc->total);
        TSVIOReenable(tc->output_vio);

        if (ret < 0) {
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);

        } else {
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
        }

        return 1;
    }

    // unfinished

    if (TSIOBufferReaderAvail(tc->output_reader) > 0)
        TSVIOReenable(tc->output_vio);

    if (writen > 0) {
        TSVIONDoneSet(input_vio, upstream_done + writen);
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }

    return 1;
}

