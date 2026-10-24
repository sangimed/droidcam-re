package dev.droidcamclone.telemetry

import java.util.concurrent.atomic.AtomicLong

class FrameStats {
    private val encoded = AtomicLong()
    private val sent = AtomicLong()
    private val queueDrops = AtomicLong()
    private val receivedRequests = AtomicLong()
    private val lastEncodeNs = AtomicLong()
    private val lastSendNs = AtomicLong()

    fun onEncoded() { encoded.incrementAndGet(); lastEncodeNs.set(System.nanoTime()) }
    fun onSent() { sent.incrementAndGet(); lastSendNs.set(System.nanoTime()) }
    fun onQueueDrop() { queueDrops.incrementAndGet() }
    fun onKeyframeRequest() { receivedRequests.incrementAndGet() }

    fun snapshot(): Snapshot = Snapshot(
        encoded = encoded.get(),
        sent = sent.get(),
        queueDrops = queueDrops.get(),
        keyframeRequests = receivedRequests.get(),
        encoderOutputAgeMs = ageMs(lastEncodeNs.get()),
        lastSendAgeMs = ageMs(lastSendNs.get()),
    )

    private fun ageMs(stamp: Long): Long = if (stamp == 0L) -1L else (System.nanoTime() - stamp) / 1_000_000L

    data class Snapshot(
        val encoded: Long,
        val sent: Long,
        val queueDrops: Long,
        val keyframeRequests: Long,
        val encoderOutputAgeMs: Long,
        val lastSendAgeMs: Long,
    )
}
