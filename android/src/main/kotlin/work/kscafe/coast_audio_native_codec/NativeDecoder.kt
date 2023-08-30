package work.kscafe.coast_audio_native_codec

import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaDataSource
import android.media.MediaExtractor
import android.media.MediaFormat
import java.nio.ByteBuffer
import java.nio.ByteOrder

private val MediaFormat.sampleRate
  get() = getInteger(MediaFormat.KEY_SAMPLE_RATE)

private val MediaFormat.channels: Int
  get() = getInteger(MediaFormat.KEY_CHANNEL_COUNT)

private val MediaFormat.bitsPerSample: Int
  get() {
    return when (if (containsKey(MediaFormat.KEY_PCM_ENCODING)) getInteger(MediaFormat.KEY_PCM_ENCODING) else AudioFormat.ENCODING_PCM_16BIT) {
      AudioFormat.ENCODING_PCM_16BIT -> 16
      AudioFormat.ENCODING_PCM_32BIT -> 32
      AudioFormat.ENCODING_PCM_24BIT_PACKED -> 24
      AudioFormat.ENCODING_PCM_8BIT -> 8
      AudioFormat.ENCODING_PCM_FLOAT -> 32
      else -> 0
    }
  }

private val MediaFormat.bytesPerFrame: Int
  get() = (bitsPerSample / 8) * channels

private val MediaFormat.caSampleFormat: Int
  get() {
    return when (if (containsKey(MediaFormat.KEY_PCM_ENCODING)) getInteger(MediaFormat.KEY_PCM_ENCODING) else AudioFormat.ENCODING_PCM_16BIT) {
      AudioFormat.ENCODING_PCM_8BIT -> 1
      AudioFormat.ENCODING_PCM_16BIT -> 2
      AudioFormat.ENCODING_PCM_24BIT_PACKED -> 3
      AudioFormat.ENCODING_PCM_32BIT -> 4
      AudioFormat.ENCODING_PCM_FLOAT -> 5
      else -> 0
    }
  }

private val MediaFormat.lengthInFrames: Long?
  get() {
    val durationUs = if (containsKey(MediaFormat.KEY_DURATION)) getLong(MediaFormat.KEY_DURATION) else return null
    val durationSec = durationUs / 1_000_000.0
    return (sampleRate * durationSec).toLong()
  }

public class NativeDecoder constructor(private val pClientData: Long) : MediaDataSource() {
  private val extractor = MediaExtractor().also { it.setDataSource(this) }

  private val codec: MediaCodec
  private val trackIndex: Int

  private val trackFormat: MediaFormat
  private val outputFormat: MediaFormat

  private val bufferInfo = MediaCodec.BufferInfo()

  private var endOfFile = false
  private var bytesToCutAfterSeek = 0

  init {
    val (codec, trackIndex) = prepare()
    this.codec = codec
    this.trackIndex = trackIndex
    this.trackFormat = extractor.getTrackFormat(trackIndex)
    this.outputFormat = codec.outputFormat
  }

  private fun prepare(): Pair<MediaCodec, Int> {
    // Check the source for valid audio content.
    val trackCount = extractor.trackCount
    if (trackCount <= 0) {
      throw NativeDecoderException("No track")
    }

    for (trackIndex in 0 until trackCount) {
      val format = extractor.getTrackFormat(trackIndex)
      if (format.containsKey(MediaFormat.KEY_MIME) && format.containsKey(MediaFormat.KEY_SAMPLE_RATE) && format.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
        val mime = format.getString(MediaFormat.KEY_MIME)!!
        extractor.selectTrack(trackIndex)
        try {
          val codec = MediaCodec.createDecoderByType(mime)
          codec.configure(format, null, null, 0)
          codec.start()
          return Pair(codec, trackIndex)
        } catch (e: Exception) {
          throw NativeDecoderException(e.localizedMessage)
        }
      }
    }

    throw NativeDecoderException("No valid track found")
  }

  // Returns false on error.
  private fun seek(frameIndex: Long) {
    val sampleRate = outputFormat.sampleRate
    val bytesPerFrame = outputFormat.bytesPerFrame

    val timeSec = frameIndex.toDouble() / sampleRate.toDouble()
    val timeUs = timeSec * 1000000.0
    extractor.seekTo(timeUs.toLong(), MediaExtractor.SEEK_TO_PREVIOUS_SYNC)

    // Check how many bytes should be cut at the beginning for precise frame positioning.
    val timeToCutUs: Double = timeUs - extractor.sampleTime
    bytesToCutAfterSeek = if (timeToCutUs <= 0) 0 else (timeToCutUs / 1000000.0 * sampleRate).toInt() * bytesPerFrame
    codec.flush()
    endOfFile = false
  }

  private fun extractNextSample(): Int? {
    val inputBufferIndex = codec.dequeueInputBuffer(0)
    if (inputBufferIndex < 0) {
      return null
    }

    val inputBuffer = codec.getInputBuffer(inputBufferIndex)!!
    val sampleSize = extractor.readSampleData(inputBuffer, 0)

    if (sampleSize < 0) { // Submit with eof.
      codec.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
      return 0
    } else { // Regular submit.
      codec.queueInputBuffer(inputBufferIndex, 0, sampleSize, extractor.sampleTime, 0)
      extractor.advance()
      return sampleSize
    }
  }

  private fun decode(): AudioBuffer? {
    val outputBufferIndex = codec.dequeueOutputBuffer(bufferInfo, 0)

    when (outputBufferIndex) {
      MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> return null
      MediaCodec.INFO_TRY_AGAIN_LATER -> return null
    }

    val outputBuffer = codec.getOutputBuffer(outputBufferIndex)!!
    val outputBufferSize = outputBuffer.remaining()
    val copiedBuffer: ByteBuffer

    var bytesRead = outputBufferSize
    // Cut the frames after seek.
    if (bytesToCutAfterSeek > 0) {
      bytesRead -= bytesToCutAfterSeek
      if (bytesRead <= 0) { // The cut is larger than the actual bytes read.
        bytesToCutAfterSeek -= outputBufferSize
        return null
      } else { // Finish cutting.
        outputBuffer.position(bytesToCutAfterSeek)

        copiedBuffer = ByteBuffer.allocateDirect(bytesRead)
        copiedBuffer.put(outputBuffer.slice())
        bytesToCutAfterSeek = 0
      }
    } else {
      copiedBuffer = ByteBuffer.allocateDirect(bytesRead)
      copiedBuffer.put(outputBuffer)
    }

    codec.releaseOutputBuffer(outputBufferIndex, false)
    val isEOF = bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
    endOfFile = isEOF
    return AudioBuffer(copiedBuffer, bytesRead / outputFormat.bytesPerFrame, isEOF)
  }

  private fun decodeNext(): AudioBuffer? {
    extractNextSample()

    while (bytesToCutAfterSeek > 0) {
      extractNextSample()
      val buffer = decode()
      if (buffer != null) {
        return buffer
      }
    }

    return decode()
  }

  fun dispose() {
    codec.stop()
  }

  // MediaDataSource implementation
  override fun close() {}

  override fun readAt(position: Long, buffer: ByteArray, offset: Int, size: Int): Int {
    return read(pClientData, position, buffer, offset, size)
  }

  override fun getSize(): Long {
    return getLength(pClientData)
  }

  private fun getEOF(): Boolean {
    return endOfFile
  }

  private fun getOutputNativeAudioFormat(buffer: ByteBuffer) {
    NativeAudioFormat(
      outputFormat.sampleRate,
      outputFormat.channels,
      outputFormat.caSampleFormat,
      trackFormat.lengthInFrames ?: -1
    ).writeStructBytes(buffer)
  }

  // JNI methods
  external fun read(pClientData: Long, position: Long, buffer: ByteArray?, offset: Int, size: Int): Int

  external fun getLength(pClientData: Long): Long

  companion object {
    fun prepare() {
      System.loadLibrary("coast_audio_native_codec")
      setClassLoader(this.javaClass.classLoader)
    }

    private external fun setClassLoader(classLoader: ClassLoader)
  }
}

data class NativeAudioFormat(val sampleRate: Int, val channels: Int, val sampleFormat: Int, val length: Long) {
  fun writeStructBytes(buffer: ByteBuffer) {
    buffer.apply {
      order(ByteOrder.nativeOrder())
      putInt(sampleRate)
      putInt(channels)
      putInt(sampleFormat)
      putLong(length)
    }
  }
}

data class AudioBuffer(val buffer: ByteBuffer, val frameCount: Int, val isEOF: Boolean)

class NativeDecoderException(override val message: String) : Exception()
