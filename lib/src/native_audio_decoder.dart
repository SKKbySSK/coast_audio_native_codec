import 'dart:ffi';
import 'dart:math';

import 'package:coast_audio/coast_audio.dart';
import 'package:coast_audio_native_codec/src/ca_decoder_callback.dart';
import 'package:coast_audio_native_codec/src/native_audio_buffer.dart';
import 'package:coast_audio_native_codec/src/native_audio_format.dart';

import 'ca_codec_bindings_generated.dart';
import 'native_audio_codec.dart';

class _AudioFramesQueue {
  _AudioFramesQueue(this.format);

  final AudioFormat format;
  final _samples = <num>[];

  int get availableFrames {
    return _samples.length ~/ format.channels;
  }

  int read(AudioBuffer destination) {
    var buffer = destination;
    final list = buffer.asNativeListView();
    final length = min(list.length, _samples.length);
    for (var i = 0; length > i; i++) {
      list[i] = _samples[i];
    }
    _samples.removeRange(0, length);
    return length ~/ format.channels;
  }

  void push(NativeAudioBuffer buffer) {
    _samples.addAll(buffer.asNativeListView());
  }

  void clear() {
    _samples.clear();
  }
}

class NativeAudioDecoder extends NativeAudioCodecBase implements AudioDecoder {
  NativeAudioDecoder({
    required this.dataSource,
    this.minBufferFrameCount = 2048,
    super.memory,
  }) {
    final config = bindings.ca_decoder_config_init();
    final callback = CaDecoderCallbackRegistry.registerDataSource(_pDecoder, dataSource, _onDecoded);
    bindings
        .ca_decoder_init(_pDecoder, config, callback.onRead, callback.onSeek, callback.onTell, callback.onDecoded, _pDecoder.cast())
        .throwIfNeeded();
  }

  final AudioInputDataSource dataSource;

  final int minBufferFrameCount;

  late final _pDecoder = allocate<ca_decoder>(sizeOf<ca_decoder>());

  late final _pIsEOF = allocate<Int>(sizeOf<Int>());

  late final _queue = _AudioFramesQueue(outputFormat);

  bool get _isNativeEOF {
    bindings.ca_decoder_get_eof(_pDecoder, _pIsEOF).throwIfNeeded();
    return _pIsEOF.value != 0;
  }

  bool get isEOF {
    return _isNativeEOF && _queue.availableFrames == 0;
  }

  late final NativeAudioFormat nativeFormat = () {
    final pFormat = allocate<ca_audio_format>(sizeOf<ca_audio_format>());
    bindings.ca_decoder_get_format(_pDecoder, pFormat).throwIfNeeded();
    return NativeAudioFormat.fromStruct(pFormat.ref);
  }();

  void _onDecoded(int frameCount, Pointer<Void> pBuffer) {
    final bufferIn = NativeAudioBuffer(pBuffer: pBuffer.cast(), sizeInFrames: frameCount, format: outputFormat);
    _queue.push(bufferIn);
  }

  var _cursorInFrames = 0;
  @override
  int get cursorInFrames => _cursorInFrames;

  @override
  set cursorInFrames(int frameIndex) {
    bindings.ca_decoder_seek(_pDecoder, frameIndex).throwIfNeeded();
    _cursorInFrames = frameIndex;
    _queue.clear();
  }

  @override
  int get lengthInFrames => nativeFormat.length;

  @override
  late final AudioFormat outputFormat = nativeFormat.audioFormat!;

  @override
  bool get canSeek => true;

  void prepare() {
    while (_queue.availableFrames <= minBufferFrameCount && !isEOF) {
      bindings.ca_decoder_decode_next(_pDecoder).throwIfNeeded();
    }
  }

  @override
  AudioDecodeResult decode({required AudioBuffer destination}) {
    prepare();

    while (_queue.availableFrames <= destination.sizeInFrames && !isEOF) {
      bindings.ca_decoder_decode_next(_pDecoder).throwIfNeeded();
    }

    final framesRead = _queue.read(destination);
    _cursorInFrames += framesRead;

    return AudioDecodeResult(
      frames: framesRead,
      isEnd: isEOF,
    );
  }

  @override
  void uninit() {
    bindings.ca_decoder_uninit(_pDecoder).throwIfNeeded();
  }
}
