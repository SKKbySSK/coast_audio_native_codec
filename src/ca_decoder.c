#include "ca_decoder.h"

#if __APPLE__
#include "darwin/audio_file_stream.h"
#endif

#if ANDROID
#include <malloc.h>
#include "android/native_decoder.h"
#endif

FFI_PLUGIN_EXPORT ca_decoder_config ca_decoder_config_init()
{
  ca_decoder_config config = {
#if __APPLE__
    .appleFileTypeHint = 0,
#endif
  };
  return config;
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_init(ca_decoder *pDecoder, ca_decoder_config config, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_tell_proc pTellProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData)
{
  ca_result result = ca_result_unknown_failed;

#if __APPLE__
  audio_file_stream *pStream = (audio_file_stream *)malloc(sizeof(audio_file_stream));
  pDecoder->pDecoder = pStream;
  result = audio_file_stream_init(pStream, config, pReadProc, pSeekProc, pTellProc, pDecodedProc, pUserData);
#endif

#if ANDROID
  native_decoder *pNativeDecoder = (native_decoder *)malloc(sizeof(native_decoder));
  pDecoder->pDecoder = pNativeDecoder;
  result = native_decoder_init(pNativeDecoder, config, pReadProc, pSeekProc, pTellProc, pDecodedProc, pUserData);
#endif

  if (result != ca_result_success)
  {
    free(pDecoder->pDecoder);
    return result;
  }

  pDecoder->pUserData = pUserData;
  return result;
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_get_format(ca_decoder *pDecoder, ca_audio_format *pFormat)
{
#if __APPLE__
  audio_file_stream_format format;
  ca_result result = audio_file_stream_get_format((audio_file_stream *)pDecoder->pDecoder, &format);
  if (result != ca_result_success)
  {
    return result;
  }

  pFormat->channels = format.channels;
  pFormat->sample_rate = format.sample_rate;
  pFormat->sample_foramt = format.sample_foramt;
  pFormat->length = format.length;
  pFormat->apple.format_id = format.format_id;
  return ca_result_success;
#endif

#if ANDROID
  return native_decoder_get_format((native_decoder *)pDecoder->pDecoder, pFormat);
#endif
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_decode_next(ca_decoder *pDecoder)
{
#if __APPLE__
  return audio_file_stream_decode_next((audio_file_stream *)pDecoder->pDecoder);
#endif

#if ANDROID
  return native_decoder_decode_next((native_decoder *)pDecoder->pDecoder);
#endif
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_seek(ca_decoder *pDecoder, ca_uint64 frameIndex)
{
#if __APPLE__
  return audio_file_stream_seek((audio_file_stream *)pDecoder->pDecoder, frameIndex);
#endif

#if ANDROID
  return native_decoder_seek((native_decoder *)pDecoder->pDecoder, frameIndex);
#endif
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_get_eof(ca_decoder *pDecoder, ca_bool *pIsEOF)
{
#if __APPLE__
  return audio_file_stream_get_eof((audio_file_stream *)pDecoder->pDecoder, pIsEOF);
#endif

#if ANDROID
  return native_decoder_get_eof((native_decoder *)pDecoder->pDecoder, pIsEOF);
#endif
}

FFI_PLUGIN_EXPORT ca_result ca_decoder_uninit(ca_decoder *pDecoder)
{
#if __APPLE__
  return audio_file_stream_uninit((audio_file_stream *)pDecoder->pDecoder);
#endif

#if ANDROID
  return native_decoder_uninit((native_decoder *)pDecoder->pDecoder);
#endif
}
