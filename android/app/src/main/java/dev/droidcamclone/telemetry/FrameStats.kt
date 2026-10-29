package dev.droidcamclone.telemetry

import java.util.ArrayDeque
import java.util.concurrent.atomic.AtomicLong

class FrameStats(private val monotonicNowNs: () -> Long = System::nanoTime) {
    private val encodedFrames = AtomicLong()
    private val cameraFrames = AtomicLong()
    private val sent = AtomicLong()
    private val queueDrops = AtomicLong()
    private val receivedRequests = AtomicLong()
    private val lastEncodeNs = AtomicLong()
    private val lastSendNs = AtomicLong()
    private val lastSequenceNumber = AtomicLong()
    private val lastCameraFrameNumber = AtomicLong()
    private var lastCameraArrivalNs = 0L
    private val recentFrames = ArrayDeque<Long>()
    private val recentCameraFrames = ArrayDeque<Long>()

    @Synchronized
    fun onCameraFrame(sensorTimestampNs: Long, frameNumber: Long) {
        if (recentCameraFrames.isNotEmpty() && sensorTimestampNs <= recentCameraFrames.last) return
        cameraFrames.incrementAndGet()
        lastCameraFrameNumber.set(frameNumber)
        lastCameraArrivalNs = monotonicNowNs()
        recentCameraFrames.addLast(sensorTimestampNs)
        while (recentCameraFrames.isNotEmpty() && sensorTimestampNs - recentCameraFrames.first > 2_000_000_000L) {
            recentCameraFrames.removeFirst()
        }
    }

    @Synchronized
    fun onEncoded(sequence: Long, timestampNs: Long) {
        if (sequence <= 0L) return
        encodedFrames.incrementAndGet()
        lastSequenceNumber.set(sequence)
        lastEncodeNs.set(timestampNs)
        recentFrames.addLast(timestampNs)
        trimRecent(timestampNs)
    }
    fun onSent() { sent.incrementAndGet(); lastSendNs.set(monotonicNowNs()) }
    fun onQueueDrop() { queueDrops.incrementAndGet() }
    fun onKeyframeRequest() { receivedRequests.incrementAndGet() }

    @Synchronized
    fun snapshot(queueDepth: Int, oldestQueuedNs: Long): Snapshot {
        val now = monotonicNowNs()
        trimRecent(now)
        val cameraFrameAgeMs = ageMs(lastCameraArrivalNs)
        val fps = if (recentFrames.size < 2) 0.0 else {
            val durationNs = recentFrames.last - recentFrames.first
            if (durationNs <= 0L) 0.0 else (recentFrames.size - 1) * 1_000_000_000.0 / durationNs
        }
        val cameraFps = if (cameraFrameAgeMs < 0L || cameraFrameAgeMs > 2_000L || recentCameraFrames.size < 2) 0.0 else {
            val durationNs = recentCameraFrames.last - recentCameraFrames.first
            if (durationNs <= 0L) 0.0 else (recentCameraFrames.size - 1) * 1_000_000_000.0 / durationNs
        }
        return Snapshot(
            encoded = encodedFrames.get(),
            cameraFrames = cameraFrames.get(),
            lastCameraFrameNumber = lastCameraFrameNumber.get(),
            cameraFrameAgeMs = cameraFrameAgeMs,
            lastSequenceNumber = lastSequenceNumber.get(),
            cameraFps = cameraFps,
            encodeFps = fps,
            sent = sent.get(),
            queueDrops = queueDrops.get(),
            keyframeRequests = receivedRequests.get(),
            encoderOutputAgeMs = ageMs(lastEncodeNs.get()),
            lastSendAgeMs = ageMs(lastSendNs.get()),
            queueDepth = queueDepth,
            queueAgeMs = ageMs(oldestQueuedNs),
        )
    }

    private fun trimRecent(now: Long) {
        while (recentFrames.isNotEmpty() && now - recentFrames.first > 2_000_000_000L) recentFrames.removeFirst()
    }

    private fun ageMs(stamp: Long): Long = if (stamp == 0L) -1L else (monotonicNowNs() - stamp) / 1_000_000L

    data class Snapshot(
        val encoded: Long,
        val cameraFrames: Long,
        val lastCameraFrameNumber: Long,
        val cameraFrameAgeMs: Long,
        val lastSequenceNumber: Long,
        val cameraFps: Double,
        val encodeFps: Double,
        val sent: Long,
        val queueDrops: Long,
        val keyframeRequests: Long,
        val encoderOutputAgeMs: Long,
        val lastSendAgeMs: Long,
        val queueDepth: Int,
        val queueAgeMs: Long,
    )
}
