import 'dart:io';

import 'package:coast_audio/coast_audio.dart';
import 'package:coast_audio_native_codec/src/ca_sample_format.dart';
import 'package:coast_audio_native_codec/src/utils/four_cc.dart';

import 'bindings/ca_codec_bindings_generated.dart';

class NativeAudioFormat {
  const NativeAudioFormat({
    required this.channels,
    required this.sampleRate,
    required this.sampleFormat,
    required this.length,
    this.apple,
  });

  factory NativeAudioFormat.fromStruct(ca_audio_format format) {
    return NativeAudioFormat(
      channels: format.channels,
      sampleRate: format.sample_rate,
      sampleFormat: CaSampleFormat.values.firstWhere((f) => f.value == format.sample_foramt),
      length: format.length,
      apple: Platform.isIOS || Platform.isMacOS ? AppleAudioFormat(FourCC(format.apple.format_id)) : null,
    );
  }

  final int channels;
  final int sampleRate;
  final CaSampleFormat sampleFormat;
  final int length;
  final AppleAudioFormat? apple;

  AudioFormat? get audioFormat {
    final sampleFormat = this.sampleFormat.sampleFormat;
    if (sampleFormat == null) {
      return null;
    }

    return AudioFormat(sampleRate: sampleRate, channels: channels, sampleFormat: sampleFormat);
  }
}

class AppleAudioFormat {
  const AppleAudioFormat(this.foramtId);
  final FourCC foramtId;
}
