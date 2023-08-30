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

extension AudioBufferExtension on AudioBuffer {
  List<num> asNativeListView() {
    switch (format.sampleFormat) {
      case SampleFormat.uint8:
        return asUint8ListViewFrames();
      case SampleFormat.int16:
        return asInt16ListView();
      case SampleFormat.int32:
        return asInt32ListView();
      case SampleFormat.float32:
        return asFloat32ListView();
    }
  }
}
