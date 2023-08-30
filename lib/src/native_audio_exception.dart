import 'package:coast_audio_native_codec/src/utils/ca_result_extension.dart';
import 'package:coast_audio_native_codec/src/utils/four_cc.dart';

import 'bindings/ca_codec_bindings_generated.dart';

class NativeAudioDecoderException implements Exception {
  const NativeAudioDecoderException(this.code);
  final int code;

  @override
  String toString() {
    if (code > ca_result.ca_result_success) {
      return 'CoastAudioNativeCodecException(code: $code, name: ${code.name ?? 'unknown'}, four_cc: ${FourCC(code).text})';
    } else {
      return 'CoastAudioNativeCodecException(code: $code, name: ${code.name ?? 'unknown'})';
    }
  }
}
