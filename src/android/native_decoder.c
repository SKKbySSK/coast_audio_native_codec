//
// Created by kaisei on 2023/08/29.
//
#include "native_decoder.h"
#include "../ca_decoder.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#define DECODER_CLASS_NAME "work/kscafe/coast_audio_native_codec/NativeDecoder"
#define AUDIO_BUFFER_CLASS_NAME "work/kscafe/coast_audio_native_codec/AudioBuffer"

JavaVM *jvm = NULL;
jobject classLoader;
jmethodID loadClassMethod;

ca_result get_jni_env(JNIEnv **env)
{
  if (jvm == NULL)
  {
    return ca_result_not_initialized;
  }

  if ((*jvm)->GetEnv(jvm, env, JNI_VERSION_1_6) == JNI_EDETACHED)
  {
    return (*jvm)->AttachCurrentThread(jvm, env, NULL) == JNI_OK ? ca_result_success : ca_result_unknown_failed;
  }

  return ca_result_success;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
  jvm = vm;
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved)
{
  jvm = NULL;
}

typedef struct
{
  jobject decoder;

  ca_decoder_read_proc readFunc;
  ca_decoder_seek_proc seekFunc;
  ca_decoder_tell_proc tellFunc;
  ca_decoder_decoded_proc decodedFunc;
} native_decoder_data;

#pragma pack(push, 1)
typedef struct
{
  jint sample_rate;
  jint channels;
  jint sample_format;
  jlong length;
} native_audio_format;
#pragma pack(pop)

static jclass load_class(JNIEnv *env, const char *className)
{
  jstring classStr = (*env)->NewStringUTF(env, className);
  jclass class = (*env)->CallObjectMethod(env, classLoader, loadClassMethod, classStr);
  (*env)->DeleteLocalRef(env, classStr);

  return class;
}

JNIEXPORT void JNICALL
Java_work_kscafe_coast_1audio_1native_1codec_NativeDecoder_00024Companion_setClassLoader(
    JNIEnv *env, jobject thiz, jobject class_loader)
{
  // https://developer.android.com/training/articles/perf-jni?hl=ja#faq-why-didnt-findclass-find-my-class
  jclass class = (*env)->GetObjectClass(env, class_loader);
  classLoader = (*env)->NewGlobalRef(env, class_loader);
  loadClassMethod = (*env)->GetMethodID(env, class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
}

JNIEXPORT jint JNICALL
Java_work_kscafe_coast_1audio_1native_1codec_NativeDecoder_read(JNIEnv *env, jobject thiz,
                                                                jlong p_client_data, jlong position,
                                                                jbyteArray buffer, jint offset,
                                                                jint size)
{
  native_decoder *pDecoder = (native_decoder *)p_client_data;
  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;

  ca_uint64 pos;
  ca_uint64 len;
  ca_tell_result tellResult = pData->tellFunc(&pos, &len, pDecoder->pUserData);
  if (tellResult != ca_tell_result_success && tellResult != ca_tell_result_unknown_length)
  {
    return ca_result_tell_failed;
  }

  if (pos != position)
  {
    ca_seek_result seekResult = pData->seekFunc(position, ca_seek_origin_start, pDecoder->pUserData);
    if (seekResult != ca_seek_result_success)
    {
      return ca_result_seek_failed;
    }
    pos = position;
  }

  ca_uint32 bytesRead;
  jbyte *pBufferOut = (*env)->GetByteArrayElements(env, buffer, NULL);
  ca_read_result readResult = pData->readFunc(pBufferOut, size, &bytesRead, pDecoder->pUserData);
  (*env)->ReleaseByteArrayElements(env, buffer, pBufferOut, 0);

  if (readResult != ca_read_result_success)
  {
    return ca_result_read_failed;
  }

  if (tellResult != ca_tell_result_unknown_length && pos + bytesRead >= len)
  {
    // 末尾に到達した時は -1 を返す
    // https://developer.android.com/reference/android/media/MediaDataSource#readAt(long,%20byte[],%20int,%20int)
    return -1;
  }

  return (jint)bytesRead;
}

JNIEXPORT jlong JNICALL
Java_work_kscafe_coast_1audio_1native_1codec_NativeDecoder_getLength(JNIEnv *env, jobject thiz,
                                                                     jlong p_client_data)
{
  native_decoder *pDecoder = (native_decoder *)p_client_data;
  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;

  ca_uint64 len;
  ca_tell_result tellResult = pData->tellFunc(NULL, &len, pDecoder->pUserData);
  if (tellResult != ca_tell_result_success)
  {
    return ca_result_tell_failed;
  }

  return (jlong)len;
}

ca_result native_decoder_init(native_decoder *pDecoder, ca_decoder_config config, ca_decoder_read_proc pReadProc, ca_decoder_seek_proc pSeekProc, ca_decoder_tell_proc pTellProc, ca_decoder_decoded_proc pDecodedProc, void *pUserData)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = malloc(sizeof(native_decoder_data));

  pDecoder->config = config;
  pDecoder->pData = pData;
  pDecoder->pUserData = pUserData;

  pData->readFunc = pReadProc;
  pData->seekFunc = pSeekProc;
  pData->tellFunc = pTellProc;
  pData->decodedFunc = pDecodedProc;

  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);
  jmethodID constructor = (*env)->GetMethodID(env, decoderClass, "<init>", "(J)V");
  jobject decoder = (*env)->NewObject(env, decoderClass, constructor, pDecoder);
  if ((*env)->ExceptionCheck(env))
  {
    (*env)->ExceptionClear(env);
    free(pData);
    return ca_result_unknown_failed;
  }

  jmethodID prepareMethod = (*env)->GetMethodID(env, decoderClass, "prepare", "()Z");
  jboolean prepared = (*env)->CallBooleanMethod(env, decoder, prepareMethod);
  if (!prepared)
  {
    return ca_result_unsupported_format;
  }

  pData->decoder = (*env)->NewGlobalRef(env, decoder);

  return ca_result_success;
}

ca_result native_decoder_get_format(native_decoder *pDecoder, ca_audio_format *pFormat)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;
  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);

  native_audio_format *pNativeFormat = (native_audio_format *)malloc(sizeof(native_audio_format));
  jobject byteBuffer = (*env)->NewDirectByteBuffer(env, pNativeFormat, sizeof(native_audio_format));
  jmethodID getFormatMethod = (*env)->GetMethodID(env, decoderClass, "getOutputNativeAudioFormat", "(Ljava/nio/ByteBuffer;)V");
  (*env)->CallVoidMethod(env, pData->decoder, getFormatMethod, byteBuffer);

  if ((*env)->ExceptionCheck(env))
  {
    (*env)->ExceptionClear(env);
    return ca_result_unknown_failed;
  }

  pFormat->channels = pNativeFormat->channels;
  pFormat->sample_rate = pNativeFormat->sample_rate;
  pFormat->sample_foramt = pNativeFormat->sample_format;
  pFormat->length = (ca_uint64)pNativeFormat->length;

  free(pNativeFormat);

  return ca_result_success;
}

ca_result native_decoder_decode_next(native_decoder *pDecoder)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;
  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);
  jclass audioBufferClass = load_class(env, AUDIO_BUFFER_CLASS_NAME);

  jmethodID decodeNextMethod = (*env)->GetMethodID(env, decoderClass, "decodeNext", "()Lwork/kscafe/coast_audio_native_codec/AudioBuffer;");
  jobject audioBuffer = (*env)->CallObjectMethod(env, pData->decoder, decodeNextMethod);
  while (audioBuffer == NULL)
  {
    audioBuffer = (*env)->CallObjectMethod(env, pData->decoder, decodeNextMethod);
  }

  jfieldID bufferField = (*env)->GetFieldID(env, audioBufferClass, "buffer", "Ljava/nio/ByteBuffer;");
  jobject buffer = (*env)->GetObjectField(env, audioBuffer, bufferField);

  jfieldID frameCountField = (*env)->GetFieldID(env, audioBufferClass, "frameCount", "I");
  jint frameCount = (*env)->GetIntField(env, audioBuffer, frameCountField);

  void *pBufferOut = (*env)->GetDirectBufferAddress(env, buffer);
  pData->decodedFunc(frameCount, pBufferOut, pDecoder->pUserData);

  return ca_result_success;
}

ca_result native_decoder_seek(native_decoder *pDecoder, ca_uint64 frameIndex)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;
  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);
  jmethodID seekMethod = (*env)->GetMethodID(env, decoderClass, "seek", "(J)V");
  (*env)->CallVoidMethod(env, pData->decoder, seekMethod, frameIndex);

  if ((*env)->ExceptionCheck(env))
  {
    (*env)->ExceptionClear(env);
    return ca_result_unknown_failed;
  }

  return ca_result_success;
}

ca_result native_decoder_get_eof(native_decoder *pDecoder, ca_bool *pIsEOF)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;
  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);
  jmethodID getEOFMethod = (*env)->GetMethodID(env, decoderClass, "getEOF", "()Z");
  jboolean isEOF = (*env)->CallBooleanMethod(env, pData->decoder, getEOFMethod);
  *pIsEOF = isEOF;

  if ((*env)->ExceptionCheck(env))
  {
    (*env)->ExceptionClear(env);
    return ca_result_unknown_failed;
  }

  return ca_result_success;
}

ca_result native_decoder_uninit(native_decoder *pDecoder)
{
  JNIEnv *env;
  ca_result result = get_jni_env(&env);
  if (result != ca_result_success)
  {
    return result;
  }

  native_decoder_data *pData = (native_decoder_data *)pDecoder->pData;

  jclass decoderClass = load_class(env, DECODER_CLASS_NAME);
  jmethodID disposeMethod = (*env)->GetMethodID(env, decoderClass, "dispose", "()V");
  (*env)->CallVoidMethod(env, pData->decoder, disposeMethod);

  (*env)->DeleteGlobalRef(env, pData->decoder);

  free(pData);

  if ((*env)->ExceptionCheck(env))
  {
    (*env)->ExceptionClear(env);
    return ca_result_unknown_failed;
  }

  return ca_result_success;
}
