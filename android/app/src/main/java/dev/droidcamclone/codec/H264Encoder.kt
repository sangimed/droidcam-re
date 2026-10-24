package dev.droidcamclone.codec

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import dev.droidcamclone.net.VideoProfile
import java.nio.ByteBuffer

data class EncoderChoice(val name: String, val minBitrate: Int, val maxBitrate: Int)

data class EncodedAccessUnit(
    val data: ByteArray,
    val presentationTimeUs: Long,
    val isCodecConfig: Boolean,
    val isKeyFrame: Boolean,
)

class H264Encoder(
    private val profile: VideoProfile,
    private val listener: Listener,
) {
    interface Listener {
        fun onAccessUnit(accessUnit: EncodedAccessUnit)
        fun onEncoderError(message: String)
    }

    private var codec: MediaCodec? = null
    private var callbackThread: HandlerThread? = null
    private var started = false

    fun start(): Surface {
        check(!started)
        val choice = profile.encoderName?.let { name -> findEncoder(profile.width, profile.height, profile.fps, name) }
            ?: findEncoder(profile.width, profile.height, profile.fps)
            ?: throw IllegalStateException("No H.264 encoder supports 1920×1080 at ${profile.fps} fps")

        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, profile.width, profile.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, profile.bitrate.coerceIn(choice.minBitrate, choice.maxBitrate))
            setInteger(MediaFormat.KEY_FRAME_RATE, profile.fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            val capabilities = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
                .first { it.name == choice.name }.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC)
            if (capabilities.encoderCapabilities.isBitrateModeSupported(
                    MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR,
                )
            ) {
                setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
            }
        }

        callbackThread = HandlerThread("CloneH264Encoder").apply { start() }
        val handler = Handler(callbackThread!!.looper)
        val instance = MediaCodec.createByCodecName(choice.name)
        codec = instance
        instance.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) = Unit

            override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                try {
                    val source = codec.getOutputBuffer(index) ?: return
                    val data = ByteArray(info.size)
                    source.position(info.offset)
                    source.limit(info.offset + info.size)
                    source.get(data)
                    if (data.isNotEmpty()) {
                        listener.onAccessUnit(
                            EncodedAccessUnit(
                                data = data,
                                presentationTimeUs = info.presentationTimeUs,
                                isCodecConfig = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0,
                                isKeyFrame = info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0,
                            ),
                        )
                    }
                } catch (error: Exception) {
                    listener.onEncoderError(error.message ?: "Encoder output error")
                } finally {
                    runCatching { codec.releaseOutputBuffer(index, false) }
                }
            }

            override fun onError(codec: MediaCodec, error: MediaCodec.CodecException) {
                listener.onEncoderError(error.diagnosticInfo ?: error.message ?: "MediaCodec error")
            }

            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                val config = combineCodecConfig(format.getByteBuffer("csd-0"), format.getByteBuffer("csd-1"))
                if (config.isNotEmpty()) {
                    listener.onAccessUnit(EncodedAccessUnit(config, 0L, isCodecConfig = true, isKeyFrame = false))
                }
            }
        }, handler)
        instance.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        val inputSurface = instance.createInputSurface()
        instance.start()
        started = true
        return inputSurface
    }

    fun requestKeyFrame() {
        val current = codec ?: return
        if (!started) return
        runCatching {
            current.setParameters(Bundle().apply { putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0) })
        }.onFailure { listener.onEncoderError("Could not request keyframe: ${it.message}") }
    }

    fun stop() {
        val current = codec
        codec = null
        started = false
        if (current != null) {
            runCatching { current.stop() }
            runCatching { current.release() }
        }
        callbackThread?.quitSafely()
        callbackThread = null
    }

    companion object {
        fun findEncoder(width: Int, height: Int, fps: Int, preferredName: String? = null): EncoderChoice? {
            val infos = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
                .filter { info -> info.isEncoder && info.supportedTypes.any { it.equals(MediaFormat.MIMETYPE_VIDEO_AVC, true) } }
                .let { list ->
                    if (preferredName != null) list.filter { it.name == preferredName } else list
                }
                .sortedByDescending { info ->
                    Build.VERSION.SDK_INT >= 29 && info.isHardwareAccelerated
                }
            return infos.firstNotNullOfOrNull { info ->
                runCatching {
                    val caps = info.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC)
                    if (MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface !in caps.colorFormats) return@runCatching null
                    val video = caps.videoCapabilities
                    if (!video.areSizeAndRateSupported(width, height, fps.toDouble())) return@runCatching null
                    EncoderChoice(info.name, video.bitrateRange.lower, video.bitrateRange.upper)
                }.getOrNull()
            }
        }

        private fun combineCodecConfig(first: ByteBuffer?, second: ByteBuffer?): ByteArray {
            val buffers = listOfNotNull(first, second).map { it.duplicate() }
            val total = buffers.sumOf { it.remaining() }
            if (total == 0) return byteArrayOf()
            val result = ByteArray(total)
            var offset = 0
            buffers.forEach { buffer ->
                val count = buffer.remaining()
                buffer.get(result, offset, count)
                offset += count
            }
            return result
        }
    }
}
