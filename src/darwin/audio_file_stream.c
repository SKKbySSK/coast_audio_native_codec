#include "audio_file_stream.h"
#include "../ca_decoder.h"
#include <AudioToolbox/AudioFileStream.h>
#include <AudioToolbox/AudioConverter.h>
#include <stdlib.h>

typedef struct
{
  AudioFileStreamID pStreamId;

  AudioStreamBasicDescription inputFormat;
  AudioStreamBasicDescription outputFormat;

  ca_decoder_read_proc readFunc;
  ca_decoder_seek_proc seekFunc;
  ca_decoder_decoded_proc decodedFunc;

  ca_uint32 maxHeaderSize;

  ca_uint32 parsingBufferSize;
  void *pParsingBuffer;

  ca_bool isAudioConverterReady;
  AudioConverterRef pAudioConverter;

  struct
  {
    AudioBuffer buffer;
    UInt32 packetCount;
    AudioStreamPacketDescription *packetDescriptions;
  } input;
  
  struct
  {
    void* pData;
    UInt32 size;
  } magicCookie;
} audio_file_stream_data;

static inline ca_result osstatus_to_result(OSStatus status)
{
  if (status == noErr)
  {
    return ca_result_success;
  }

  return status;
}

static inline ca_result get_file_stream_property(audio_file_stream *pStream, AudioFileStreamPropertyID propertyId, UInt32 propertySize, void *pDataOut)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  return osstatus_to_result(AudioFileStreamGetProperty(pData->pStreamId, propertyId, &propertySize, pDataOut));
}

static inline ca_result get_converter_property(audio_file_stream *pStream, AudioFileStreamPropertyID propertyId, UInt32 propertySize, void *pDataOut)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  return osstatus_to_result(AudioConverterGetProperty(pData->pAudioConverter, propertyId, &propertySize, pDataOut));
}

static OSStatus audio_file_stream_packets_converter_input(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription *_Nullable *outDataPacketDescription, void *inUserData)
{
  audio_file_stream *pStream = (audio_file_stream *)inUserData;
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  
  if (*ioNumberDataPackets == 0 || pData->input.buffer.mData == NULL) {
    ioData->mNumberBuffers = 0;
    *ioNumberDataPackets = 0;
    return kAudio_ParamError;
  }

  ioData->mNumberBuffers = 1;
  ioData->mBuffers[0] = pData->input.buffer;
  pData->input.buffer.mData = NULL;
  pData->input.buffer.mDataByteSize = 0;

  *ioNumberDataPackets = pData->input.packetCount;
  *outDataPacketDescription = pData->input.packetDescriptions;

  return noErr;
}

static void audio_file_stream_property_listener(void *inClientData, AudioFileStreamID inAudioFileStream, AudioFileStreamPropertyID inPropertyID, AudioFileStreamPropertyFlags *ioFlags)
{
  audio_file_stream *pStream = (audio_file_stream *)inClientData;
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  
  *ioFlags = kAudioFileStreamPropertyFlag_CacheProperty;
  
  if (inPropertyID == kAudioFileStreamProperty_MagicCookieData) {
    UInt32 magicCookieSize;
    Boolean writable;
    OSStatus status = AudioFileStreamGetPropertyInfo(pData->pStreamId, kAudioFileStreamProperty_MagicCookieData, &magicCookieSize, &writable);
    if (status != noErr) {
      return;
    }
    
    void* pMagicCookie = malloc(magicCookieSize);
    status = AudioFileStreamGetProperty(pData->pStreamId, kAudioFileStreamProperty_MagicCookieData, &magicCookieSize, pMagicCookie);
    if (status != noErr) {
      return;
    }
    
    pData->magicCookie.pData = pMagicCookie;
    pData->magicCookie.size = magicCookieSize;
  }
}

static void audio_file_stream_packets(void *inClientData, UInt32 inNumberBytes, UInt32 inNumberPackets, const void *inInputData, AudioStreamPacketDescription *inPacketDescriptions)
{
  audio_file_stream *pStream = (audio_file_stream *)inClientData;
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  ca_result result;

  if (!pData->isAudioConverterReady)
  {
    result = get_file_stream_property(pStream, kAudioFileStreamProperty_DataFormat, sizeof(AudioStreamBasicDescription), &pData->inputFormat);
    if (result != ca_result_success)
    {
      return;
    }

    {
      pData->outputFormat.mSampleRate = pData->inputFormat.mSampleRate;
      pData->outputFormat.mChannelsPerFrame = pData->inputFormat.mChannelsPerFrame;

      pData->outputFormat.mFormatID = kAudioFormatLinearPCM;
      pData->outputFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
      pData->outputFormat.mBytesPerFrame = sizeof(float) * pData->inputFormat.mChannelsPerFrame;
      pData->outputFormat.mFramesPerPacket = 1;
      pData->outputFormat.mBitsPerChannel = sizeof(float) * 8;

      pData->outputFormat.mBytesPerPacket = pData->outputFormat.mBytesPerFrame * pData->outputFormat.mFramesPerPacket;
    }

    result = osstatus_to_result(AudioConverterNew(&pData->inputFormat, &pData->outputFormat, &pData->pAudioConverter));
    if (result != ca_result_success)
    {
      return;
    }
    
    result = osstatus_to_result(AudioConverterSetProperty(pData->pAudioConverter, kAudioConverterDecompressionMagicCookie, pData->magicCookie.size, pData->magicCookie.pData));
    if (result != ca_result_success)
    {
      return;
    }

    pData->isAudioConverterReady = CA_TRUE;
  }

  void *pBufferIn = malloc(inNumberBytes);
  memcpy(pBufferIn, inInputData, inNumberBytes);

  {
    pData->input.buffer.mNumberChannels = pData->inputFormat.mChannelsPerFrame;
    pData->input.buffer.mDataByteSize = inNumberBytes;
    pData->input.buffer.mData = pBufferIn;
    pData->input.packetCount = inNumberPackets;
    pData->input.packetDescriptions = inPacketDescriptions;
  }

  UInt32 maxOutputPacketSize;
  result = get_converter_property(pStream, kAudioConverterPropertyMaximumOutputPacketSize, sizeof(UInt32), &maxOutputPacketSize);
  if (result != ca_result_success)
  {
    return;
  }

  UInt32 bufferOutSize = maxOutputPacketSize * pData->outputFormat.mBytesPerPacket;
  void *pBufferOut = malloc(bufferOutSize);

  do {
    AudioBufferList outBufferList;
    outBufferList.mNumberBuffers = 1;
    outBufferList.mBuffers[0].mDataByteSize = bufferOutSize;
    outBufferList.mBuffers[0].mNumberChannels = pData->outputFormat.mChannelsPerFrame;
    outBufferList.mBuffers[0].mData = pBufferOut;

    UInt32 outDataPacketSize = maxOutputPacketSize;
    result = osstatus_to_result(AudioConverterFillComplexBuffer(pData->pAudioConverter, audio_file_stream_packets_converter_input, inClientData, &outDataPacketSize, &outBufferList, NULL));
    if (result != ca_result_success)
    {
      free(pBufferOut);
      free(pBufferIn);
      return;
    }

    if (outDataPacketSize > 0)
    {
      UInt32 bytesConverted = outDataPacketSize * pData->outputFormat.mFramesPerPacket * pData->outputFormat.mBytesPerFrame;
      pData->decodedFunc(bytesConverted / pData->outputFormat.mBytesPerFrame, pBufferOut, pStream->pUserData);
    }
  } while (result == ca_result_success);

  free(pBufferOut);
  free(pBufferIn);
}

static ca_result audio_file_stream_parse_bytes(audio_file_stream *pStream, ca_uint32 *pBytesRead)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  ca_uint32 bytesLeft = *pBytesRead;
  ca_uint32 bytesRead;

  *pBytesRead = 0;
  do
  {
    bytesRead = ca_min(pData->parsingBufferSize, bytesLeft);
    ca_read_result readResult = pData->readFunc(pData->pParsingBuffer, bytesRead, &bytesRead, pStream->pUserData);
    if (readResult != ca_read_result_success)
    {
      return ca_result_read_failed;
    }

    ca_result result = osstatus_to_result(AudioFileStreamParseBytes(pData->pStreamId, bytesRead, pData->pParsingBuffer, 0));
    if (result != ca_result_success)
    {
      return result;
    }

    *pBytesRead += bytesRead;
    bytesLeft -= bytesRead;
  } while (bytesLeft > 0 && bytesRead > 0);

  return ca_result_success;
}

static ca_result audio_file_stream_load(audio_file_stream *pStream)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  ca_seek_result seekResult = pData->seekFunc == NULL ? ca_seek_result_unsupported : pData->seekFunc(0, ca_seek_origin_start, pStream->pUserData);
  if (seekResult != ca_seek_result_success && seekResult != ca_seek_result_unsupported)
  {
    return ca_result_seek_failed;
  }

  ca_result result = ca_result_read_failed;
  ca_uint32 totalBytesRead = 0;
  while (totalBytesRead < pData->maxHeaderSize)
  {
    ca_uint32 bytesToRead = ca_min(pData->parsingBufferSize, pData->maxHeaderSize - totalBytesRead);
    result = audio_file_stream_parse_bytes(pStream, &bytesToRead);
    if (result != ca_result_success)
    {
      return result;
    }

    totalBytesRead += bytesToRead;

    UInt32 isReadyToProducePackets;
    result = get_file_stream_property(pStream, kAudioFileStreamProperty_ReadyToProducePackets, sizeof(UInt32), &isReadyToProducePackets);
    if (result != ca_result_success)
    {
      return result;
    }

    if (isReadyToProducePackets == 1)
    {
      break;
    }
  }

  return result;
}

ca_result audio_file_stream_init(audio_file_stream *pStream, ca_decoder_config config, int maxHeaderSize, int parsingBufferSize, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData)
{
  if (maxHeaderSize <= 0)
  {
    return ca_result_invalid_args;
  }

  if (parsingBufferSize <= 0)
  {
    return ca_result_invalid_args;
  }

  audio_file_stream_data *pData = malloc(sizeof(audio_file_stream_data));

  pStream->config = config;
  pStream->pData = pData;
  pStream->pUserData = pUserData;

  pData->pStreamId = malloc(sizeof(AudioFileStreamID));
  pData->pAudioConverter = malloc(sizeof(AudioConverterRef));
  pData->readFunc = pReadProc;
  pData->seekFunc = pSeekProc;
  pData->decodedFunc = pDecodedProc;
  pData->maxHeaderSize = maxHeaderSize;
  pData->isAudioConverterReady = CA_FALSE;
  
  pData->magicCookie.pData = NULL;
  pData->magicCookie.size = 0;

  ca_result result = osstatus_to_result(
      AudioFileStreamOpen(
          pStream,
          audio_file_stream_property_listener,
          audio_file_stream_packets,
          config.appleFileTypeHint,
          &pData->pStreamId));
  if (result != ca_result_success)
  {
    free(pData);
    return result;
  }

  pData->pParsingBuffer = malloc(parsingBufferSize);
  pData->parsingBufferSize = parsingBufferSize;

  result = audio_file_stream_load(pStream);
  if (result != ca_result_success)
  {
    free(pData->pParsingBuffer);
    free(pData);
    return result;
  }

  return result;
}

ca_result audio_file_stream_get_format(audio_file_stream *pStream, audio_file_stream_format *pFormat)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  UInt32 formatId;
  ca_result result = get_file_stream_property(pStream, kAudioFileStreamProperty_FileFormat, sizeof(UInt32), &formatId);
  if (result != ca_result_success)
  {
    return result;
  }

  pFormat->channels = pData->outputFormat.mChannelsPerFrame;
  pFormat->sample_rate = pData->outputFormat.mSampleRate;
  pFormat->sample_foramt = ca_sample_format_f32;
  pFormat->format_id = formatId;

  return result;
}

ca_result audio_file_stream_decode(audio_file_stream *pStream, ca_uint32 bytesToRead, ca_uint32 *pBytesRead)
{
  ca_uint32 bytesRead = bytesToRead;
  ca_result result = audio_file_stream_parse_bytes(pStream, &bytesRead);
  if (result != ca_read_result_success)
  {
    *pBytesRead = 0;
    return result;
  }

  *pBytesRead = bytesRead;
  return result;
}

ca_result audio_file_stream_uninit(audio_file_stream *pStream)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  if (pData->magicCookie.pData != NULL) {
    free(pData->magicCookie.pData);
  }
  
  free(pData->pParsingBuffer);
  free(pData);

  ca_result result = osstatus_to_result(AudioFileStreamClose(pData->pStreamId));
  if (pData->isAudioConverterReady)
  {
    result = osstatus_to_result(AudioConverterDispose(pData->pAudioConverter));
  }

  return result;
}
