package dev.droidcamclone.camera

internal fun chooseFpsRange(ranges: List<Pair<Int, Int>>, targetFps: Int): Pair<Int, Int>? {
    val candidates = ranges.filter { (lower, upper) -> lower <= targetFps && upper == targetFps }
    return candidates.firstOrNull { (lower, upper) -> lower == targetFps && upper == targetFps }
        ?: candidates.maxByOrNull { it.first }
}
