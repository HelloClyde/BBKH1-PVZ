/* Immutable resources are checked by the offline packer/CI/installer.
 * Runtime decompression only reads the already trusted payload.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#pragma once
#include <zlib.h>
inline int H1ResourceInflateInit(z_streamp stream){
    int result=inflateInit(stream);
    if(result==Z_OK)inflateValidate(stream,0);
    return result;
}
inline int H1ResourceInflateReset(z_streamp stream){
    int result=inflateReset(stream);
    if(result==Z_OK)inflateValidate(stream,0);
    return result;
}
inline int H1ResourceUncompress(Bytef* output,uLongf* bytes,const Bytef* input,uLong inputBytes){
    z_stream stream{};
    int result=H1ResourceInflateInit(&stream);
    if(result!=Z_OK)return result;
    stream.next_in=const_cast<Bytef*>(input);stream.avail_in=inputBytes;
    // An authored transparent marker has a valid empty zlib stream. zlib
    // still requires a non-null output pointer, even when it writes no bytes.
    unsigned char emptyOutput;
    stream.next_out=*bytes?output:&emptyOutput;stream.avail_out=*bytes?*bytes:1;
    result=inflate(&stream,Z_FINISH);*bytes=stream.total_out;
    inflateEnd(&stream);
    return result==Z_STREAM_END?Z_OK:result;
}
