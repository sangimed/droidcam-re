package dev.droidcamclone.camera

import org.junit.Assert.assertEquals
import org.junit.Test

class FpsRangeSelectorTest {
    @Test
    fun prefersAnExactFixedRateRange() {
        val selected = chooseFpsRange(
            listOf(8 to 30, 10 to 30, 15 to 30, 30 to 30),
            30,
        )

        assertEquals(30 to 30, selected)
    }

    @Test
    fun prefersTheNarrowestRangeWhenNoFixedRangeExists() {
        val selected = chooseFpsRange(
            listOf(8 to 30, 10 to 30, 15 to 30),
            30,
        )

        assertEquals(15 to 30, selected)
    }
}
