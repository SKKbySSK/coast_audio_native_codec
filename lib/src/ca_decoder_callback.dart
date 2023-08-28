import 'dart:ffi';

import 'package:coast_audio/coast_audio.dart';

import 'ca_codec_bindings_generated.dart';

class CaDecoderCallback {
  CaDecoderCallback(
    this.onRead,
    this.onSeek,
    this.onTell,
    this.onDecoded,
    this._dataSource,
    this._onDecoded,
  );
  final ca_decoder_read_proc onRead;
  final ca_decoder_seek_proc onSeek;
  final ca_decoder_tell_proc onTell;
  final ca_decoder_decoded_proc onDecoded;

  final AudioInputDataSource _dataSource;
  void Function(int frameCount, Pointer<Void> pBuffer) _onDecoded;
}

class CaDecoderCallbackRegistry {
  CaDecoderCallbackRegistry._();

  static int _onRead(Pointer<Void> pBufferOut, int bytesToRead, Pointer<UnsignedInt> pBytesRead, Pointer<Void> pUserData) {
    final cb = _callbacks[pUserData.address];
    if (cb == null) {
      return ca_read_result.ca_read_result_failed;
    }

    pBytesRead.value = cb._dataSource.readBytes(pBufferOut.cast<Uint8>().asTypedList(bytesToRead), 0, bytesToRead);
    return ca_read_result.ca_read_result_success;
  }

  static int _onSeek(int byteOffset, int origin, Pointer<Void> pUserData) {
    final cb = _callbacks[pUserData.address];
    if (cb == null) {
      return ca_seek_result.ca_seek_result_failed;
    }

    if (!cb._dataSource.canSeek) {
      return ca_seek_result.ca_seek_result_unsupported;
    }

    final SeekOrigin dsOrigin;
    switch (origin) {
      case ca_seek_origin.ca_seek_origin_start:
        dsOrigin = SeekOrigin.begin;
        break;
      case ca_seek_origin.ca_seek_origin_current:
        dsOrigin = SeekOrigin.current;
        break;
      default:
        return ca_seek_result.ca_seek_result_failed;
    }

    cb._dataSource.seek(byteOffset, dsOrigin);
    return ca_seek_result.ca_seek_result_success;
  }

  static int _onTell(Pointer<UnsignedLongLong> pPosition, Pointer<UnsignedLongLong> pLength, Pointer<Void> pUserData) {
    final cb = _callbacks[pUserData.address];
    if (cb == null) {
      return ca_tell_result.ca_tell_result_failed;
    }

    final length = cb._dataSource.length;
    if (pPosition != nullptr) {
      pPosition.value = cb._dataSource.position;
    }
    if (pLength != nullptr) {
      pLength.value = length ?? 0;
    }
    return length == null ? ca_tell_result.ca_tell_result_unknown_length : ca_tell_result.ca_tell_result_success;
  }

  static void _onDecoded(int frameCount, Pointer<Void> pBuffer, Pointer<Void> pUserData) {
    final cb = _callbacks[pUserData.address];
    cb?._onDecoded(frameCount, pBuffer);
  }

  static final Map<int, CaDecoderCallback> _callbacks = {};

  static CaDecoderCallback registerDataSource(
    Pointer<ca_decoder> pDecoder,
    AudioInputDataSource dataSource,
    void Function(int frameCount, Pointer<Void> pBuffer) onDecoded,
  ) {
    final cb = CaDecoderCallback(
      Pointer.fromFunction(_onRead, ca_read_result.ca_read_result_failed),
      dataSource.canSeek ? Pointer.fromFunction(_onSeek, ca_seek_result.ca_seek_result_failed) : nullptr,
      Pointer.fromFunction(_onTell, ca_tell_result.ca_tell_result_failed),
      Pointer.fromFunction(_onDecoded),
      dataSource,
      onDecoded,
    );

    _callbacks[pDecoder.address] = cb;
    return cb;
  }

  static void unregister(Pointer<ca_decoder> pDecoder) {
    _callbacks.remove(pDecoder.address);
  }
}
