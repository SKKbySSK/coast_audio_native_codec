#pragma once
#include "../ca_decoder.h"

typedef struct
{
  ca_decoder_config config;
  void *pUserData;
  void *pData;
} audio_file_stream;

typedef struct
{
  ca_uint32 channels;
  ca_uint32 sample_rate;
  ca_sample_format sample_foramt;
  ca_uint32 format_id;
} audio_file_stream_format;

ca_result audio_file_stream_init(audio_file_stream *pStream, ca_decoder_config config, int maxHeaderSize, int parsingBufferSize, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData);

ca_result audio_file_stream_get_format(audio_file_stream *pStream, audio_file_stream_format *pFormat);

ca_result audio_file_stream_decode(audio_file_stream *pStream, ca_uint32 bytesToRead, ca_uint32 *pBytesRead);

ca_result audio_file_stream_uninit(audio_file_stream *pStream);
