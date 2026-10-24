package dev.droidcamclone.net

import dev.droidcamclone.telemetry.FrameStats
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.EOFException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import java.security.SecureRandom
import java.util.UUID
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.atomic.AtomicBoolean

class SessionClient(private val stats: FrameStats = FrameStats()) {
    interface Listener {
        fun onConnected(accept: SessionAccept)
        fun onKeyframeRequested()
        fun onDisconnected(message: String)
    }

    private data class PacketBatch(val datagrams: List<ByteArray>)

    private val closing = AtomicBoolean(false)
    private val packetQueue = ArrayBlockingQueue<PacketBatch>(2)
    private val writeLock = Any()
    @Volatile private var controlSocket: Socket? = null
    @Volatile private var videoSocket: DatagramSocket? = null
    @Volatile private var controlOutput: DataOutputStream? = null
    @Volatile private var packetizer: RtpH264Packetizer? = null
    @Volatile private var destination: InetSocketAddress? = null
    @Volatile private var sessionId: UUID? = null
    private var controlThread: Thread? = null
    private var senderThread: Thread? = null

    fun connect(host: String, profile: VideoProfile, listener: Listener) {
        check(controlThread == null) { "Session already started" }
        closing.set(false)
        controlThread = Thread({ runSession(host, profile, listener) }, "CloneSessionControl").apply { start() }
    }

    fun sendAccessUnit(accessUnit: ByteArray, presentationTimeUs: Long) {
        if (closing.get()) return
        val currentPacketizer = packetizer ?: return
        try {
            stats.onEncoded()
            val datagrams = currentPacketizer.packetize(accessUnit, presentationTimeUs)
            if (datagrams.isEmpty()) return
            if (!packetQueue.offer(PacketBatch(datagrams))) {
                // Prefer the newest complete frame when the network sender falls behind.
                packetQueue.poll()
                packetQueue.offer(PacketBatch(datagrams))
                stats.onQueueDrop()
            }
        } catch (_: IllegalArgumentException) {
            stats.onQueueDrop()
        }
    }

    fun statsSnapshot(): FrameStats.Snapshot = stats.snapshot()

    fun close() {
        if (!closing.compareAndSet(false, true)) return
        val id = sessionId
        if (id != null) runCatching { sendControl(SessionProtocol.encodeClose(id)) }
        runCatching { controlSocket?.close() }
        runCatching { videoSocket?.close() }
        controlSocket = null
        videoSocket = null
        packetQueue.clear()
        senderThread?.interrupt()
        senderThread = null
        controlThread = null
        packetizer = null
        destination = null
        sessionId = null
        controlOutput = null
    }

    private fun runSession(host: String, profile: VideoProfile, listener: Listener) {
        try {
            val address = InetAddress.getByName(host)
            val socket = Socket().apply {
                tcpNoDelay = true
                connect(InetSocketAddress(address, SessionProtocol.CONTROL_PORT), CONNECT_TIMEOUT_MS)
            }
            controlSocket = socket
            val output = DataOutputStream(BufferedOutputStream(socket.getOutputStream()))
            val input = DataInputStream(BufferedInputStream(socket.getInputStream()))
            controlOutput = output

            val id = UUID.randomUUID()
            val ssrc = SecureRandom().nextInt().toLong() and 0xffff_ffffL
            sessionId = id
            sendControl(SessionProtocol.encodeHello(id, profile, ssrc))
            val accept = SessionProtocol.decodeAccept(readRecord(input))
            require(accept.sessionId == id) { "Windows accepted a different session ID" }

            val udp = DatagramSocket()
            videoSocket = udp
            destination = InetSocketAddress(address, accept.videoPort)
            packetizer = RtpH264Packetizer(ssrc, accept.maxRtpPayload, SecureRandom().nextInt(0x10000))
            startSender(listener)
            listener.onConnected(accept)

            while (!closing.get()) {
                when (SessionProtocol.decodeControlSignal(readRecord(input), id)) {
                    ControlSignal.KEYFRAME_REQUEST -> {
                        stats.onKeyframeRequest()
                        listener.onKeyframeRequested()
                    }
                    ControlSignal.CLOSE -> break
                }
            }
            if (!closing.get()) listener.onDisconnected("Windows closed the session")
        } catch (error: Exception) {
            if (!closing.get()) listener.onDisconnected(error.message ?: error.javaClass.simpleName)
        } finally {
            if (!closing.get()) close()
        }
    }

    private fun startSender(listener: Listener) {
        senderThread = Thread({
            while (!closing.get()) {
                try {
                    val batch = packetQueue.take()
                    val socket = videoSocket ?: break
                    val target = destination ?: break
                    batch.datagrams.forEach { bytes ->
                        socket.send(DatagramPacket(bytes, bytes.size, target))
                        stats.onSent()
                    }
                } catch (_: InterruptedException) {
                    break
                } catch (_: Exception) {
                    if (!closing.get()) {
                        listener.onDisconnected("UDP video send failed")
                        close()
                    }
                    break
                }
            }
        }, "CloneRtpSender").apply { start() }
    }

    private fun sendControl(record: ByteArray) {
        synchronized(writeLock) {
            val output = controlOutput ?: throw IllegalStateException("Control channel is not connected")
            output.write(record)
            output.flush()
        }
    }

    private fun readRecord(input: DataInputStream): ByteArray {
        val length = try {
            input.readInt()
        } catch (eof: EOFException) {
            throw EOFException("Windows closed the control connection")
        }
        require(length in 2..SessionProtocol.MAX_RECORD_LENGTH) { "Invalid control record length $length" }
        val body = ByteArray(length)
        input.readFully(body)
        return ByteBuffer.allocate(4 + length).putInt(length).put(body).array()
    }

    companion object {
        private const val CONNECT_TIMEOUT_MS = 5000
    }
}
