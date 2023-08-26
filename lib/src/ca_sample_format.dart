import 'package:coast_audio/coast_audio.dart';

import 'ca_codec_bindings_generated.dart';

enum CaSampleFormat {
  unknown(ca_sample_format.ca_sample_format_unknown),
  uint8(ca_sample_format.ca_sample_format_u8),
  int16(ca_sample_format.ca_sample_format_s16),
  int24(ca_sample_format.ca_sample_format_s24),
  int32(ca_sample_format.ca_sample_format_s32),
  float32(ca_sample_format.ca_sample_format_f32);

  const CaSampleFormat(this.value);
  final int value;

  SampleFormat? get sampleFormat {
    switch (this) {
      case CaSampleFormat.uint8:
        return SampleFormat.uint8;
      case CaSampleFormat.int16:
        return SampleFormat.int16;
      case CaSampleFormat.int32:
        return SampleFormat.int32;
      case CaSampleFormat.float32:
        return SampleFormat.float32;
      case CaSampleFormat.int24:
      case CaSampleFormat.unknown:
        return null;
    }
  }
}
