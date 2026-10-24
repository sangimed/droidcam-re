package dev.droidcamclone.net

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

data class VideoProfile(
    val width: Int,
    val height: Int,
    val fps: Int,
    val bitrate: Int,
    val fpsRangeMin: Int = fps,
    val fpsRangeMax: Int = fps,
    val encoderName: String? = null,
) {
    init {
        require(width == 1920 && height == 1080)
        require(fps == 30 || fps == 60)
        require(fpsRangeMin in 1..fps && fpsRangeMax == fps)
        require(bitrate > 0)
    }
}

data class SessionAccept(val sessionId: UUID, val videoPort: Int, val maxRtpPayload: Int)
enum class ControlSignal { KEYFRAME_REQUEST, CLOSE }

object SessionProtocol {
    const val VERSION = 1
    const val CONTROL_PORT = 48200
    const val MAX_RECORD_LENGTH = 4096
    const val MAX_RTP_PAYLOAD = 1200
    const val RTP_PAYLOAD_TYPE = 96

    private const val HELLO = 1
    private const val ACCEPT = 2
    private const val REJECT = 3
    private const val KEYFRAME_REQUEST = 4
    private const val CLOSE = 5

    fun encodeHello(sessionId: UUID, profile: VideoProfile, ssrc: Long): ByteArray {
        require(ssrc in 0..0xffff_ffffL)
        val body = ByteBuffer.allocate(29).order(ByteOrder.BIG_ENDIAN)
            .put(VERSION.toByte()).put(HELLO.toByte())
            .putLong(sessionId.mostSignificantBits).putLong(sessionId.leastSignificantBits)
            .putShort(profile.width.toShort()).putShort(profile.height.toShort())
            .put(profile.fps.toByte()).put(1.toByte())
            .put(RTP_PAYLOAD_TYPE.toByte()).putInt(ssrc.toInt()).array()
        return frame(body)
    }

    fun decodeAccept(record: ByteArray): SessionAccept {
        val body = unframe(record, expectedLength = 22)
        require((body.get().toInt() and 0xff) == VERSION) { "Unsupported protocol version" }
        require((body.get().toInt() and 0xff) == ACCEPT) { "Expected ACCEPT" }
        val id = UUID(body.long, body.long)
        val port = body.short.toInt() and 0xffff
        val maxPayload = body.short.toInt() and 0xffff
        require(port in 1..65535 && maxPayload in 64..MAX_RTP_PAYLOAD) { "Invalid ACCEPT values" }
        return SessionAccept(id, port, maxPayload)
    }

    fun encodeKeyFrameRequest(sessionId: UUID): ByteArray = encodeSessionMessage(KEYFRAME_REQUEST, sessionId)
    fun encodeClose(sessionId: UUID): ByteArray = encodeSessionMessage(CLOSE, sessionId)

    fun decodeControlSignal(record: ByteArray, expectedSession: UUID): ControlSignal {
        val body = unframe(record, expectedLength = 18)
        require((body.get().toInt() and 0xff) == VERSION) { "Unsupported protocol version" }
        val type = body.get().toInt() and 0xff
        val id = UUID(body.long, body.long)
        require(id == expectedSession) { "Control message belongs to another session" }
        return when (type) {
            KEYFRAME_REQUEST -> ControlSignal.KEYFRAME_REQUEST
            CLOSE -> ControlSignal.CLOSE
            else -> throw IllegalArgumentException("Unexpected control message type $type")
        }
    }

    private fun encodeSessionMessage(type: Int, sessionId: UUID): ByteArray {
        val body = ByteBuffer.allocate(18).order(ByteOrder.BIG_ENDIAN)
            .put(VERSION.toByte()).put(type.toByte())
            .putLong(sessionId.mostSignificantBits).putLong(sessionId.leastSignificantBits).array()
        return frame(body)
    }

    private fun frame(body: ByteArray): ByteArray {
        require(body.size in 2..MAX_RECORD_LENGTH)
        return ByteBuffer.allocate(4 + body.size).order(ByteOrder.BIG_ENDIAN)
            .putInt(body.size).put(body).array()
    }

    private fun unframe(record: ByteArray, expectedLength: Int): ByteBuffer {
        require(record.size >= 6) { "Truncated control record" }
        val input = ByteBuffer.wrap(record).order(ByteOrder.BIG_ENDIAN)
        val length = input.int
        require(length in 2..MAX_RECORD_LENGTH && length == expectedLength && record.size == length + 4) {
            "Invalid control record length"
        }
        return input.slice().order(ByteOrder.BIG_ENDIAN)
    }
}
