import 'package:coast_audio/coast_audio.dart';

class NativeAudioBuffer extends AudioBuffer {
  NativeAudioBuffer({
    required super.pBuffer,
    required super.sizeInFrames,
    required super.format,
  }) : super(
          memory: FfiMemory(),
          sizeInBytes: format.bytesPerFrame * sizeInFrames,
        );
}
