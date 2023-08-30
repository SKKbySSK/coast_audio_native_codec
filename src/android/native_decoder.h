#pragma once
#include "../ca_decoder.h"
#include <jni.h>

typedef struct
{
  ca_decoder_config config;
  void *pUserData;
  void *pData;
} native_decoder;

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved);

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved);

ca_result native_decoder_init(native_decoder *pDecoder, ca_decoder_config config, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_tell_proc pTellProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData);

ca_result native_decoder_get_format(native_decoder *pDecoder, ca_audio_format *pFormat);

ca_result native_decoder_decode_next(native_decoder *pDecoder);

ca_result native_decoder_seek(native_decoder *pDecoder, ca_uint64 frameIndex);

ca_result native_decoder_get_eof(native_decoder *pDecoder, ca_bool *pIsEOF);

ca_result native_decoder_uninit(native_decoder *pDecoder);
