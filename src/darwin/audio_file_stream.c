#include "audio_file_stream.h"
#include "../ca_decoder.h"
#include <AudioToolbox/AudioFileStream.h>
#include <AudioToolbox/AudioConverter.h>
#include <stdlib.h>

#define MAX_HEADER_SIZE 1024 * 1024
#define BUFFER_SIZE 4096
#define PACKET_AGGREGATION_COUNT 128
#define EOF_ON_READ_FAILED CA_TRUE
#define EOF_ZERO_READ_THRESHOLD 10

typedef struct
{
  AudioFileStreamID pStreamId;

  AudioStreamBasicDescription inputFormat;
  AudioStreamBasicDescription outputFormat;

  ca_decoder_read_proc readFunc;
  ca_decoder_seek_proc seekFunc;
  ca_decoder_tell_proc tellFunc;
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
    void *pData;
    UInt32 size;
  } magicCookie;

  ca_bool isDiscontinued;
  ca_bool contiguousZeroReadCount;
  ca_bool isReadFailed;
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

ca_result audio_file_stream_get_length(audio_file_stream *pStream, UInt64 *pLength)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  UInt64 packetCount;
  ca_result result = get_file_stream_property(pStream, kAudioFileStreamProperty_AudioDataPacketCount, sizeof(UInt64), &packetCount);
  if (result == ca_result_success)
  {
    *pLength = (UInt64)pData->inputFormat.mFramesPerPacket * packetCount;
    return result;
  }

  SInt64 dataOffset;
  result = get_file_stream_property(pStream, kAudioFileStreamProperty_DataOffset, sizeof(SInt64), &dataOffset);
  if (result == ca_result_success)
  {
    ca_uint64 lengthInBytes;
    ca_tell_result tellResult = pData->tellFunc(NULL, &lengthInBytes, pStream->pUserData);

    if (tellResult != ca_tell_result_success)
    {
      return ca_result_tell_failed;
    }

    UInt32 bitRate;
    result = get_file_stream_property(pStream, kAudioFileStreamProperty_BitRate, sizeof(UInt32), &bitRate);
    if (result != ca_result_success)
    {
      return result;
    }

    ca_uint64 audioDataLength = (ca_uint64)((SInt64)lengthInBytes - dataOffset);
    Float64 lengthInSeconds = audioDataLength / (Float64)(bitRate / 8);
    Float64 lengthInFrames = lengthInSeconds * (Float64)(pData->outputFormat.mSampleRate);
    *pLength = (UInt64)ceil(lengthInFrames);
  }

  return result;
}

static OSStatus audio_file_stream_packets_converter_input(AudioConverterRef inAudioConverter, UInt32 *ioNumberDataPackets, AudioBufferList *ioData, AudioStreamPacketDescription *_Nullable *outDataPacketDescription, void *inUserData)
{
  audio_file_stream *pStream = (audio_file_stream *)inUserData;
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  if (*ioNumberDataPackets == 0 || pData->input.buffer.mData == NULL)
  {
    ioData->mNumberBuffers = 0;
    *ioNumberDataPackets = 0;
    return kAudio_ParamError;
  }

  ioData->mNumberBuffers = 1;
  ioData->mBuffers[0] = pData->input.buffer;
  pData->input.buffer.mData = NULL;
  pData->input.buffer.mDataByteSize = 0;

  *ioNumberDataPackets = pData->input.packetCount;
  if (outDataPacketDescription != NULL)
  {
    *outDataPacketDescription = pData->input.packetDescriptions;
  }

  return noErr;
}

static void audio_file_stream_property_listener(void *inClientData, AudioFileStreamID inAudioFileStream, AudioFileStreamPropertyID inPropertyID, AudioFileStreamPropertyFlags *ioFlags)
{
  audio_file_stream *pStream = (audio_file_stream *)inClientData;
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  *ioFlags = kAudioFileStreamPropertyFlag_CacheProperty;

  if (inPropertyID == kAudioFileStreamProperty_MagicCookieData)
  {
    UInt32 magicCookieSize;
    Boolean writable;
    OSStatus status = AudioFileStreamGetPropertyInfo(pData->pStreamId, kAudioFileStreamProperty_MagicCookieData, &magicCookieSize, &writable);
    if (status != noErr)
    {
      return;
    }

    void *pMagicCookie = malloc(magicCookieSize);
    status = AudioFileStreamGetProperty(pData->pStreamId, kAudioFileStreamProperty_MagicCookieData, &magicCookieSize, pMagicCookie);
    if (status != noErr)
    {
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

    if (pData->magicCookie.pData != NULL)
    {
      result = osstatus_to_result(AudioConverterSetProperty(pData->pAudioConverter, kAudioConverterDecompressionMagicCookie, pData->magicCookie.size, pData->magicCookie.pData));
      if (result != ca_result_success)
      {
        return;
      }
    }

    pData->isAudioConverterReady = CA_TRUE;
  }

  void *pBufferIn = malloc(inNumberBytes);
  memcpy(pBufferIn, inInputData, inNumberBytes);

  // MEMO: PCM(WAVE)形式で AudioConverterFillComplexBuffer 処理を呼び出すとクラッキングノイズのようなものが混ざるため、 AudioConverterConvertBuffer を使用する
  if (pData->inputFormat.mFormatID == kAudioFormatLinearPCM)
  {
    UInt32 frameCount = inNumberBytes / pData->inputFormat.mBytesPerFrame;
    UInt32 bufferOutSize = pData->outputFormat.mBytesPerFrame * frameCount;
    void *pBufferOut = malloc(bufferOutSize);
    result = osstatus_to_result(AudioConverterConvertBuffer(pData->pAudioConverter, inNumberBytes, pBufferIn, &bufferOutSize, pBufferOut));
    if (result == ca_result_success)
    {
      pData->decodedFunc(bufferOutSize / pData->outputFormat.mBytesPerFrame, pBufferOut, pStream->pUserData);
    }

    free(pBufferOut);
  }
  else
  {
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
    
    UInt32 minBufferSize;
    result = get_converter_property(pStream, kAudioConverterPropertyMinimumOutputBufferSize, sizeof(UInt32), &minBufferSize);
    if (result != ca_result_success)
    {
      return;
    }

    UInt32 bufferOutSize = ca_max(maxOutputPacketSize * pData->outputFormat.mBytesPerPacket, minBufferSize);
    void *pBufferOut = malloc(bufferOutSize);

    AudioBufferList outBufferList;
    outBufferList.mNumberBuffers = 1;
    outBufferList.mBuffers[0].mDataByteSize = bufferOutSize;
    outBufferList.mBuffers[0].mNumberChannels = pData->outputFormat.mChannelsPerFrame;
    outBufferList.mBuffers[0].mData = pBufferOut;

    UInt32 maxDecodeSize = maxOutputPacketSize * pData->outputFormat.mBytesPerPacket * PACKET_AGGREGATION_COUNT;
    void *pDecodedOut = malloc(maxDecodeSize);
    UInt32 decodedSize = 0;

    while (CA_TRUE)
    {
      UInt32 outDataPacketSize = maxOutputPacketSize;
      result = osstatus_to_result(AudioConverterFillComplexBuffer(pData->pAudioConverter, audio_file_stream_packets_converter_input, inClientData, &outDataPacketSize, &outBufferList, NULL));

      if (result != ca_result_success)
      {
        break;
      }

      UInt32 outDataSize = outDataPacketSize * pData->outputFormat.mFramesPerPacket * pData->outputFormat.mBytesPerFrame;
      if (decodedSize + outDataSize > maxDecodeSize)
      {
        pData->decodedFunc(decodedSize / pData->outputFormat.mBytesPerFrame, pDecodedOut, pStream->pUserData);
        decodedSize = 0;
      }

      memcpy(pDecodedOut + decodedSize, pBufferOut, outDataSize);
      decodedSize += outDataSize;
    }

    if (decodedSize > 0)
    {
      pData->decodedFunc(decodedSize / pData->outputFormat.mBytesPerFrame, pDecodedOut, pStream->pUserData);
    }

    free(pBufferOut);
    free(pDecodedOut);
  }

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
      pData->isReadFailed = CA_TRUE;
      return ca_result_read_failed;
    }

    // MEMO: PCM(WAVE)ファイル形式の時は kAudioFileStreamParseFlag_Discontinuity を設定すると kAudioFileStreamError_DiscontinuityCantRecover エラーとなるため、常にフラグを立てない
    ca_bool shouldFlagDiscontinuity = pData->isDiscontinued && pData->inputFormat.mFormatID != kAudioFormatLinearPCM;
    ca_result result = osstatus_to_result(AudioFileStreamParseBytes(pData->pStreamId, bytesRead, pData->pParsingBuffer, shouldFlagDiscontinuity ? kAudioFileStreamParseFlag_Discontinuity : 0));
    if (result != ca_result_success)
    {
      return result;
    }

    pData->isDiscontinued = CA_FALSE;
    *pBytesRead += bytesRead;
    bytesLeft -= bytesRead;

    if (bytesRead == 0)
    {
      pData->contiguousZeroReadCount++;
    }
    else
    {
      pData->contiguousZeroReadCount = 0;
    }
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

ca_result audio_file_stream_init(audio_file_stream *pStream, ca_decoder_config config, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_tell_proc pTellProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData)
{
  audio_file_stream_data *pData = malloc(sizeof(audio_file_stream_data));

  pStream->config = config;
  pStream->pData = pData;
  pStream->pUserData = pUserData;

  pData->pStreamId = malloc(sizeof(AudioFileStreamID));
  pData->pAudioConverter = malloc(sizeof(AudioConverterRef));
  pData->readFunc = pReadProc;
  pData->seekFunc = pSeekProc;
  pData->tellFunc = pTellProc;
  pData->decodedFunc = pDecodedProc;
  pData->maxHeaderSize = MAX_HEADER_SIZE;
  pData->isAudioConverterReady = CA_FALSE;
  pData->isDiscontinued = CA_FALSE;
  pData->contiguousZeroReadCount = 0;
  pData->isReadFailed = CA_FALSE;

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

  pData->pParsingBuffer = malloc(BUFFER_SIZE);
  pData->parsingBufferSize = BUFFER_SIZE;

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

  UInt32 formatId = 0;
  get_file_stream_property(pStream, kAudioFileStreamProperty_FileFormat, sizeof(UInt32), &formatId);

  UInt64 length = 0;
  ca_result result = audio_file_stream_get_length(pStream, &length);
  if (result != ca_result_success)
  {
    return result;
  }

  pFormat->channels = pData->outputFormat.mChannelsPerFrame;
  pFormat->sample_rate = pData->outputFormat.mSampleRate;
  pFormat->sample_foramt = ca_sample_format_f32;
  pFormat->length = (ca_uint64)length;
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

ca_result audio_file_stream_seek(audio_file_stream *pStream, ca_uint64 frameIndex, ca_uint64 *pBytesOffset)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  UInt64 length;
  ca_result result = audio_file_stream_get_length(pStream, &length);

  pData->isDiscontinued = CA_TRUE;

  SInt64 packetOffset = (SInt64)(frameIndex / (ca_uint64)pData->inputFormat.mFramesPerPacket);
  SInt64 dataByteOffset = 0;
  AudioFileStreamSeekFlags flags;
  result = osstatus_to_result(AudioFileStreamSeek(pData->pStreamId, packetOffset, &dataByteOffset, &flags));
  if (result != ca_result_success)
  {
    return result;
  }

  SInt64 dataOffset;
  result = get_file_stream_property(pStream, kAudioFileStreamProperty_DataOffset, sizeof(SInt64), &dataOffset);
  if (result != ca_result_success)
  {
    dataOffset = 0;
  }

  if (pData->isAudioConverterReady)
  {
    result = osstatus_to_result(AudioConverterReset(pData->pAudioConverter));
    if (result != ca_result_success)
    {
      return result;
    }
  }

  *pBytesOffset = (ca_uint64)(dataByteOffset + dataOffset);
  return ca_result_success;
}

ca_result audio_file_stream_get_eof(audio_file_stream *pStream, ca_bool *pIsEOF)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;

  ca_uint64 position, length;
  ca_tell_result tellResult = pData->tellFunc(&position, &length, pStream->pUserData);
  if (tellResult == ca_tell_result_success)
  {
    *pIsEOF = position >= length;
    return ca_result_success;
  }

  if (pData->isReadFailed && EOF_ON_READ_FAILED)
  {
    *pIsEOF = CA_TRUE;
    return ca_result_success;
  }

  if (pData->contiguousZeroReadCount >= EOF_ZERO_READ_THRESHOLD)
  {
    *pIsEOF = CA_TRUE;
    return ca_result_success;
  }

  return ca_result_unknown_failed;
}

ca_result audio_file_stream_uninit(audio_file_stream *pStream)
{
  audio_file_stream_data *pData = (audio_file_stream_data *)pStream->pData;
  if (pData->magicCookie.pData != NULL)
  {
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
