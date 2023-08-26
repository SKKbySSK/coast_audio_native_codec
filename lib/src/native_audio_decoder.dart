import 'dart:collection';
import 'dart:ffi';
import 'dart:math';

import 'package:coast_audio/coast_audio.dart';
import 'package:coast_audio_native_codec/src/ca_decoder_callback.dart';
import 'package:coast_audio_native_codec/src/native_audio_buffer.dart';
import 'package:coast_audio_native_codec/src/native_audio_format.dart';

import 'ca_codec_bindings_generated.dart';
import 'native_audio_codec.dart';

class _AudioFramesQueue {
  _AudioFramesQueue();

  final _queue = Queue<AllocatedAudioFrames>();
  var _frameOffset = 0;

  int get availableFrames {
    return _queue.fold(0, (previousValue, element) => previousValue + element.sizeInFrames) - _frameOffset;
  }

  int read(AudioBuffer destination) {
    if (_queue.isEmpty) {
      return 0;
    }

    var buffer = destination;
    final disposeFrames = <AllocatedAudioFrames>[];

    while (_queue.isNotEmpty && buffer.sizeInFrames > 0) {
      final queueFrames = _queue.first;
      final queueBuffer = queueFrames.lock().offset(_frameOffset);
      final copyFrameCount = min(queueBuffer.sizeInFrames, buffer.sizeInFrames);

      if (queueBuffer.sizeInFrames < buffer.sizeInFrames) {
        _queue.removeFirst();
        disposeFrames.add(queueFrames);
        _frameOffset = 0;
      } else {
        _frameOffset += queueBuffer.sizeInFrames - buffer.sizeInFrames;
      }

      queueBuffer.copyTo(buffer, frames: copyFrameCount);
      buffer = buffer.offset(copyFrameCount);

      queueFrames.unlock();
    }

    for (final frames in disposeFrames) {
      frames.dispose();
    }

    return destination.sizeInFrames - buffer.sizeInFrames;
  }

  void push(AllocatedAudioFrames frames) {
    _queue.add(frames);
  }
}

class NativeAudioDecoder extends NativeAudioCodecBase implements AudioDecoder {
  NativeAudioDecoder({
    required this.dataSource,
    super.memory,
  }) {
    final config = bindings.ca_decoder_config_init();
    final callback = CaDecoderCallbackRegistry.registerDataSource(_pDecoder, dataSource, _onDecoded);
    bindings.ca_decoder_init(_pDecoder, config, callback.onRead, callback.onSeek, callback.onDecoded, _pDecoder.cast()).throwIfNeeded();
  }

  final AudioInputDataSource dataSource;

  late final _pDecoder = allocate<ca_decoder>(sizeOf<ca_decoder>());

  late final _pBytesRead = allocate<UnsignedInt>(sizeOf<UnsignedInt>());

  final _queue = _AudioFramesQueue();

  late final NativeAudioFormat nativeFormat = () {
    final pFormat = allocate<ca_audio_format>(sizeOf<ca_audio_format>());
    bindings.ca_decoder_get_format(_pDecoder, pFormat).throwIfNeeded();
    return NativeAudioFormat.fromStruct(pFormat.ref);
  }();

  void _onDecoded(int frameCount, Pointer<Void> pBuffer) {
    final bufferIn = NativeAudioBuffer(pBuffer: pBuffer.cast(), sizeInFrames: frameCount, format: outputFormat);
    final frames = AllocatedAudioFrames(length: frameCount, format: outputFormat);
    frames.acquireBuffer(bufferIn.copyTo);
    _queue.push(frames);
  }

  @override
  int cursorInFrames = 0;

  @override
  AudioDecodeResult decode({required AudioBuffer destination}) {
    while (_queue.availableFrames <= destination.sizeInFrames) {
      if (dataSource.length == dataSource.position) {
        break;
      }
      bindings.ca_decoder_decode(_pDecoder, 1024, _pBytesRead).throwIfNeeded();
    }

    final framesRead = _queue.read(destination);
    cursorInFrames += framesRead;

    return AudioDecodeResult(
      frames: framesRead,
      isEnd: false,
    );
  }

  @override
  int get lengthInFrames => throw UnimplementedError();

  @override
  late final AudioFormat outputFormat = nativeFormat.audioFormat!;

  @override
  void uninit() {
    bindings.ca_decoder_uninit(_pDecoder).throwIfNeeded();
  }
}
