import 'dart:ffi';

import 'package:coast_audio/coast_audio.dart';

import 'ca_codec_bindings_generated.dart';

class CaDecoderCallback {
  CaDecoderCallback(
    this.onRead,
    this.onSeek,
    this.onDecoded,
    this._dataSource,
    this._onDecoded,
  );
  final ca_decoder_read_proc onRead;
  final ca_decoder_seek_proc onSeek;
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
      Pointer.fromFunction(_onRead, 0),
      dataSource.canSeek ? Pointer.fromFunction(_onSeek, 0) : nullptr,
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
