import 'package:coast_audio_native_codec/coast_audio_native_codec.dart';

import '../bindings/ca_codec_bindings_generated.dart';

extension CaResultExtenson on int {
  void throwIfNeeded() {
    if (this != 0) {
      throw NativeAudioDecoderException(this);
    }
  }

  String? get name {
    switch (this) {
      case ca_result.ca_result_success:
        return 'ca_result_success';
      case ca_result.ca_result_invalid_args:
        return 'ca_result_invalid_args';
      case ca_result.ca_result_seek_failed:
        return 'ca_result_seek_failed';
      case ca_result.ca_result_read_failed:
        return 'ca_result_read_failed';
      case ca_result.ca_result_tell_failed:
        return 'ca_result_tell_failed';
      case ca_result.ca_result_not_initialized:
        return 'ca_result_not_initialized';
      case ca_result.ca_result_unsupported_format:
        return 'ca_result_unsupported_format';
      case ca_result.ca_result_unknown_failed:
        return 'ca_result_unknown_failed';
      default:
        return null;
    }
  }
}
