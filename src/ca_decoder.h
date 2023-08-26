#pragma once

#include "ca_defs.h"

typedef struct
{
  int appleFileTypeHint;
} ca_decoder_config;

typedef struct
{
  void *pDecoder;
  void *pUserData;
} ca_decoder;

typedef ca_read_result (*ca_decoder_read_proc)(void *pBufferIn, ca_uint32 bytesToRead, ca_uint32 *pBytesRead, void *pUserData);

typedef ca_seek_result (*ca_decoder_seek_proc)(ca_uint32 byteOffset, ca_seek_origin origin, void *pUserData);

typedef void (*ca_decoder_decoded_proc)(ca_uint32 frameCount, void *pBuffer, void *pUserData);

FFI_PLUGIN_EXPORT ca_decoder_config ca_decoder_config_init();

FFI_PLUGIN_EXPORT ca_result ca_decoder_init(ca_decoder *pDecoder, ca_decoder_config config, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData);

FFI_PLUGIN_EXPORT ca_result ca_decoder_decode(ca_decoder *pDecoder, ca_uint32 bytesToRead, ca_uint32 *pBytesRead);

FFI_PLUGIN_EXPORT ca_result ca_decoder_get_format(ca_decoder *pDecoder, ca_audio_format *pFormat);

FFI_PLUGIN_EXPORT ca_result ca_decoder_uninit(ca_decoder *pDecoder);
