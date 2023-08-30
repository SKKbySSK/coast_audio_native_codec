package work.kscafe.coast_audio_native_codec_example

import android.os.Bundle
import io.flutter.embedding.android.FlutterActivity
import work.kscafe.coast_audio_native_codec.NativeDecoder

class MainActivity : FlutterActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    NativeDecoder.prepare()
  }
}
