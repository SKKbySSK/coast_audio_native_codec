import 'dart:ffi';
import 'dart:io';

import 'package:coast_audio/coast_audio.dart';
import 'package:coast_audio_native_codec/coast_audio_native_codec.dart';

import 'ca_codec_bindings_generated.dart';

/// The dynamic library in which the symbols for [CoastAudioNativeCodecBindings] can be found.
final _dylib = () {
  const libName = 'coast_audio_native_codec';
  if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.open('$libName.framework/$libName');
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$libName.so');
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('$libName.dll');
  }
  throw UnsupportedError('Unknown platform: ${Platform.operatingSystem}');
}();

/// The bindings to the native functions in [_dylib].
final _bindings = CaCodecBindings(_dylib);

/// An abstract class for implementing mabridge's functionality
abstract class NativeAudioCodecBase extends SyncDisposable {
  NativeAudioCodecBase({required Memory? memory}) : memory = memory ?? Memory();

  final Memory memory;

  CaCodecBindings get bindings => _bindings;

  final _disposableBag = SyncDisposableBag();

  /// Allocates the memory and store it in the [SyncDisposableBag]
  /// It will be freed when the [dispose] is called.
  Pointer<T> allocate<T extends NativeType>(int size) {
    final ptr = memory.allocator.allocate<T>(size);
    addPtrToDisposableBag(ptr);
    return ptr;
  }

  void addPtrToDisposableBag<T extends NativeType>(Pointer<T> ptr) {
    final disposable = SyncCallbackDisposable(() => memory.allocator.free(ptr));
    _disposableBag.add(disposable);
  }

  @override
  bool get isDisposed => _disposableBag.isDisposed;

  @override
  void dispose() {
    if (_disposableBag.isDisposed) {
      return;
    }
    try {
      uninit();
    } finally {
      _disposableBag.dispose();
    }
  }

  /// An abstract method to uninit all internal resources.
  /// Do not call this method directly because this will be called inside the [dispose] method.
  void uninit();
}

extension CaResultExtenson on int {
  void throwIfNeeded() {
    if (this != 0) {
      throw NativeAudioCodecException(this);
    }
  }
}

class NativeAudioCodecException implements Exception {
  const NativeAudioCodecException(this.code);
  final int code;

  @override
  String toString() {
    if (Platform.isIOS || Platform.isMacOS) {
      return 'CoastAudioNativeCodecException(code: $code, four_cc: ${FourCC(code).text})';
    } else {
      return 'CoastAudioNativeCodecException(code: $code)';
    }
  }
}
