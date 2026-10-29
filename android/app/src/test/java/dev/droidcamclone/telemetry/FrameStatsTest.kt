package dev.droidcamclone.telemetry

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FrameStatsTest {
    @Test
    fun staleCameraCallbacksReportZeroFpsAndIncreasingAge() {
        var nowNs = 1_000_000_000L
        val stats = FrameStats { nowNs }
        stats.onCameraFrame(5_000_000_000L, 1)
        nowNs += 33_333_333L
        stats.onCameraFrame(5_033_333_333L, 2)

        val live = stats.snapshot(0, 0)
        assertTrue(live.cameraFps in 29.0..31.0)
        assertEquals(0L, live.cameraFrameAgeMs)

        nowNs += 2_100_000_000L
        val stale = stats.snapshot(0, 0)
        assertEquals(0.0, stale.cameraFps, 0.0)
        assertTrue(stale.cameraFrameAgeMs >= 2_100L)
    }

    @Test
    fun outOfOrderCameraSensorTimestampsAreIgnored() {
        var nowNs = 2_000_000_000L
        val stats = FrameStats { nowNs }
        stats.onCameraFrame(100_000_000L, 10)
        nowNs += 20_000_000L
        stats.onCameraFrame(99_000_000L, 9)

        val snapshot = stats.snapshot(0, 0)
        assertEquals(1L, snapshot.cameraFrames)
        assertEquals(10L, snapshot.lastCameraFrameNumber)
    }
}
