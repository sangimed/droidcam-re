package dev.droidcamclone.net

import java.nio.ByteBuffer
import java.nio.ByteOrder

class RtpH264Packetizer(
    private val ssrc: Long,
    private val maxDatagramBytes: Int = SessionProtocol.MAX_RTP_PAYLOAD,
    initialSequence: Int = 0,
) {
    private var sequence = initialSequence and 0xffff
    private data class NalRange(val start: Int, val end: Int) {
        val size: Int get() = end - start
    }

    init {
        require(ssrc in 0..0xffff_ffffL)
        require(maxDatagramBytes in 64..SessionProtocol.MAX_RTP_PAYLOAD)
    }

    @Synchronized
    fun packetize(accessUnit: ByteBuffer, ptsUs: Long): List<ByteArray> = packetize(accessUnit.toRemainingByteArray(), ptsUs)

    @Synchronized
    fun packetize(accessUnit: ByteArray, ptsUs: Long): List<ByteArray> {
        if (accessUnit.isEmpty()) return emptyList()
        val nalUnits = findNalRanges(accessUnit)
        require(nalUnits.isNotEmpty()) { "No H.264 NAL units found" }
        val timestamp = ((ptsUs * 90L) / 1000L).toInt()
        val packets = ArrayList<ByteArray>()
        nalUnits.forEachIndexed { nalIndex, nal ->
            require(nal.size > 0) { "Empty H.264 NAL unit" }
            val markerOnLastPacket = nalIndex == nalUnits.lastIndex
            if (nal.size + RTP_HEADER_BYTES <= maxDatagramBytes) {
                packets += makePacket(accessUnit, nal.start, nal.size, timestamp, markerOnLastPacket)
            } else {
                packetizeFragmentedNal(accessUnit, nal, timestamp, markerOnLastPacket, packets)
            }
        }
        return packets
    }

    private fun packetizeFragmentedNal(
        accessUnit: ByteArray,
        nal: NalRange,
        timestamp: Int,
        marker: Boolean,
        output: MutableList<ByteArray>,
    ) {
        require(nal.size > 1) { "NAL unit is too short to fragment" }
        val chunkSize = maxDatagramBytes - RTP_HEADER_BYTES - FU_HEADER_BYTES
        val originalHeader = accessUnit[nal.start].toInt() and 0xff
        val fuIndicator = (originalHeader and 0xe0) or FU_A_TYPE
        val nalType = originalHeader and 0x1f
        var offset = 1
        while (offset < nal.size) {
            val size = minOf(chunkSize, nal.size - offset)
            val isStart = offset == 1
            val isEnd = offset + size == nal.size
            val packet = newPacket(RTP_HEADER_BYTES + FU_HEADER_BYTES + size, timestamp, marker && isEnd)
            packet[RTP_HEADER_BYTES] = fuIndicator.toByte()
            packet[RTP_HEADER_BYTES + 1] = ((if (isStart) 0x80 else 0) or (if (isEnd) 0x40 else 0) or nalType).toByte()
            accessUnit.copyInto(packet, RTP_HEADER_BYTES + FU_HEADER_BYTES, nal.start + offset, nal.start + offset + size)
            output += packet
            offset += size
        }
    }

    private fun makePacket(data: ByteArray, offset: Int, size: Int, timestamp: Int, marker: Boolean): ByteArray {
        val packet = newPacket(RTP_HEADER_BYTES + size, timestamp, marker)
        data.copyInto(packet, RTP_HEADER_BYTES, offset, offset + size)
        return packet
    }

    private fun newPacket(size: Int, timestamp: Int, marker: Boolean): ByteArray {
        val packet = ByteArray(size)
        packet[0] = 0x80.toByte() // RTP v2, no padding/extension/CSRC.
        packet[1] = ((if (marker) 0x80 else 0) or SessionProtocol.RTP_PAYLOAD_TYPE).toByte()
        packet[2] = (sequence ushr 8).toByte()
        packet[3] = sequence.toByte()
        sequence = (sequence + 1) and 0xffff
        ByteBuffer.wrap(packet, 4, 4).order(ByteOrder.BIG_ENDIAN).putInt(timestamp)
        ByteBuffer.wrap(packet, 8, 4).order(ByteOrder.BIG_ENDIAN).putInt(ssrc.toInt())
        return packet
    }

    private fun findNalRanges(data: ByteArray): List<NalRange> {
        val starts = ArrayList<Pair<Int, Int>>()
        var index = 0
        while (index < data.size - 2) {
            when {
                index + 3 < data.size && data[index] == 0.toByte() && data[index + 1] == 0.toByte() &&
                    data[index + 2] == 0.toByte() && data[index + 3] == 1.toByte() -> {
                    starts += index to 4
                    index += 4
                }
                data[index] == 0.toByte() && data[index + 1] == 0.toByte() && data[index + 2] == 1.toByte() -> {
                    starts += index to 3
                    index += 3
                }
                else -> index++
            }
        }
        if (starts.isNotEmpty()) {
            return starts.mapIndexedNotNull { position, (start, prefix) ->
                val nalStart = start + prefix
                val nalEnd = starts.getOrNull(position + 1)?.first ?: data.size
                if (nalStart >= nalEnd) null else NalRange(nalStart, nalEnd)
            }
        }

        val lengthPrefixed = findLengthPrefixedNalRanges(data)
        if (lengthPrefixed != null) return lengthPrefixed
        return listOf(NalRange(0, data.size))
    }

    private fun findLengthPrefixedNalRanges(data: ByteArray): List<NalRange>? {
        val result = ArrayList<NalRange>()
        var offset = 0
        while (offset + 4 <= data.size) {
            val length = ((data[offset].toInt() and 0xff) shl 24) or
                ((data[offset + 1].toInt() and 0xff) shl 16) or
                ((data[offset + 2].toInt() and 0xff) shl 8) or
                (data[offset + 3].toInt() and 0xff)
            if (length <= 0 || length > data.size - offset - 4) return null
            val start = offset + 4
            result += NalRange(start, start + length)
            offset = start + length
        }
        return result.takeIf { it.isNotEmpty() && offset == data.size }
    }

    private fun ByteBuffer.toRemainingByteArray(): ByteArray = duplicate().let { duplicate ->
        ByteArray(duplicate.remaining()).also(duplicate::get)
    }

    private companion object {
        const val RTP_HEADER_BYTES = 12
        const val FU_HEADER_BYTES = 2
        const val FU_A_TYPE = 28
    }
}
