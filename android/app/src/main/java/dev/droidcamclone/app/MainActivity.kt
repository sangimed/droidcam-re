package dev.droidcamclone.app

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ApplicationInfo
import android.graphics.Color
import android.os.Bundle
import android.os.BatteryManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.Process
import android.os.SystemClock
import android.net.Uri
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.FrameLayout
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import dev.droidcamclone.camera.CameraController
import dev.droidcamclone.codec.EncodedAccessUnit
import dev.droidcamclone.net.SessionAccept
import dev.droidcamclone.net.SessionClient
import dev.droidcamclone.net.VideoProfile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private data class DiagnosticSample(
    val elapsedSeconds: Double,
    val cameraFps: Double,
    val cameraFrameNumber: Long,
    val cameraFrameAgeMs: Long,
    val encoderFps: Double,
    val encoderSequence: Long,
    val encoderOutputAgeMs: Long,
    val processCpuPercent: Double,
    val batteryTemperatureC: Double?,
    val thermalStatus: String,
    val udpPacketsSent: Long,
    val senderQueueDepth: Int,
    val senderQueueAgeMs: Long,
    val senderQueueDrops: Long,
    val keyframeRequests: Long,
)

class MainActivity : Activity(), SurfaceHolder.Callback {
    private val isDebugBuild: Boolean
        get() = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var cameraController: CameraController
    private lateinit var preview: SurfaceView
    private lateinit var addressInput: EditText
    private lateinit var profilePicker: Spinner
    private lateinit var statusView: TextView
    private lateinit var telemetryView: TextView
    private lateinit var connectButton: Button
    private lateinit var exportButton: Button
    private lateinit var dimButton: Button
    private lateinit var dimOverlay: TextView
    private var profiles: List<VideoProfile> = emptyList()
    private var sessionClient: SessionClient? = null
    private var active = false
    private var connecting = false
    private var activeProfile: VideoProfile? = null
    private var lastCpuTimeMs = 0L
    private var lastElapsedTimeMs = 0L
    private var sessionStartedElapsedMs = 0L
    private var sessionStartedAt = ""
    private var activeReceiverAddress = ""
    private var diagnosticProfile: VideoProfile? = null
    private val diagnosticSamples = ArrayList<DiagnosticSample>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cameraController = CameraController(this)
        buildUi()
        preview.holder.addCallback(this)
        refreshProfiles()
    }

    private fun buildUi() {
        addressInput = EditText(this).apply {
            hint = "Windows IPv4 address"
            setSingleLine(true)
            setTextColor(Color.BLACK)
            setHintTextColor(Color.DKGRAY)
        }
        profilePicker = Spinner(this)
        connectButton = Button(this).apply { text = "Connect" }
        exportButton = Button(this).apply {
            text = "Export diagnostics CSV"
            isEnabled = false
            setOnClickListener { requestDiagnosticExport() }
        }
        dimButton = Button(this).apply {
            text = "Dim"
            isEnabled = false
            setOnClickListener { setDimmed(dimOverlay.visibility != View.VISIBLE) }
        }
        statusView = TextView(this).apply {
            text = "Waiting for camera permission"
            setTextColor(Color.DKGRAY)
            textSize = 14f
        }
        telemetryView = TextView(this).apply {
            text = "Capture telemetry appears when streaming starts"
            setTextColor(Color.DKGRAY)
            textSize = 12f
        }
        preview = SurfaceView(this)
        val previewContainer = FrameLayout(this).apply {
            addView(preview, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        }
        dimOverlay = TextView(this).apply {
            text = "Screen dimmed · tap to restore"
            gravity = Gravity.CENTER
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.BLACK)
            textSize = 18f
            visibility = View.GONE
            setOnClickListener { setDimmed(false) }
        }
        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(addressInput, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 2f))
            addView(profilePicker, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.4f))
            addView(connectButton, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT))
            addView(dimButton, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        }
        connectButton.setOnClickListener { if (active || connecting) disconnect() else connect() }
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            addView(controls)
            addView(LinearLayout(this@MainActivity).apply {
                gravity = Gravity.END
                addView(exportButton)
            })
            addView(statusView)
            addView(telemetryView)
            addView(previewContainer, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        }
        setContentView(FrameLayout(this).apply {
            addView(content, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            addView(dimOverlay, FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
        })
    }

    private fun refreshProfiles() {
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_REQUEST)
            return
        }
        profiles = runCatching { cameraController.queryProfiles() }.getOrElse {
            statusView.text = "Could not inspect camera capabilities: ${it.message}"
            emptyList()
        }
        profilePicker.adapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_dropdown_item,
            profiles.map { "${it.width}×${it.height} ${it.fps} fps · ${it.encoderMode} · ${it.encoderName}" },
        )
        val defaultProfile = profiles.indexOfFirst { it.width == 1920 && it.fps == 30 }.takeIf { it >= 0 }
            ?: profiles.indexOfFirst { it.fps == 30 }.takeIf { it >= 0 }
            ?: 0
        profilePicker.setSelection(defaultProfile)
        statusView.text = when {
            profiles.isEmpty() -> "This camera/encoder does not report a supported 720p or 1080p profile"
            isDebugBuild -> "Ready · debug build; use the benchmark APK for performance measurements"
            else -> "Ready · select an available video profile"
        }
        connectButton.isEnabled = profiles.isNotEmpty()
    }

    private fun connect() {
        val address = addressInput.text.toString().trim()
        if (!isIpv4(address)) {
            statusView.text = "Enter the Windows receiver's IPv4 address"
            return
        }
        val profile = profiles.getOrNull(profilePicker.selectedItemPosition) ?: run {
            statusView.text = "No supported video profile selected"
            return
        }
        preview.holder.setFixedSize(profile.width, profile.height)
        if (!preview.holder.surface.isValid) {
            statusView.text = "Android preview is not ready"
            return
        }

        connecting = true
        activeReceiverAddress = address
        connectButton.text = "Cancel"
        connectButton.isEnabled = true
        profilePicker.isEnabled = false
        statusView.text = "Connecting to $address:${dev.droidcamclone.net.SessionProtocol.CONTROL_PORT}…"
        val client = SessionClient()
        sessionClient = client
        client.connect(address, profile, object : SessionClient.Listener {
            override fun onConnected(accept: SessionAccept) {
                mainHandler.post {
                    if (client !== sessionClient) return@post
                    connecting = false
                    startCamera(profile, client, accept)
                }
            }

            override fun onKeyframeRequested() {
                mainHandler.post { cameraController.requestKeyFrame() }
            }

            override fun onDisconnected(message: String) {
                mainHandler.post {
                    if (client !== sessionClient) return@post
                    connecting = false
                    disconnect("Disconnected: $message")
                }
            }
        })
    }

    private fun startCamera(profile: VideoProfile, client: SessionClient, accept: SessionAccept) {
        if (client !== sessionClient || isFinishing) return
        connecting = false
        active = true
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        dimButton.isEnabled = true
        activeProfile = profile
        diagnosticProfile = profile
        diagnosticSamples.clear()
        sessionStartedAt = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSZ", Locale.US).format(Date())
        sessionStartedElapsedMs = SystemClock.elapsedRealtime()
        exportButton.isEnabled = false
        connectButton.text = "Disconnect"
        connectButton.isEnabled = true
        profilePicker.isEnabled = false
        lastCpuTimeMs = Process.getElapsedCpuTime()
        lastElapsedTimeMs = SystemClock.elapsedRealtime()
        statusView.text = "Session accepted · UDP ${accept.videoPort} · starting camera"
        cameraController.start(preview.holder.surface, profile, object : CameraController.Listener {
            override fun onState(message: String) {
                mainHandler.post { if (active) statusView.text = message }
            }

            override fun onError(message: String) {
                mainHandler.post {
                    disconnect(message)
                }
            }

            override fun onAccessUnit(accessUnit: EncodedAccessUnit) {
                client.sendAccessUnit(
                    accessUnit.data,
                    accessUnit.presentationTimeUs,
                    accessUnit.sequenceNumber,
                    accessUnit.encodedAtMonotonicNs,
                )
            }

            override fun onCaptureFrame(sensorTimestampNs: Long, frameNumber: Long) {
                client.onCameraFrame(sensorTimestampNs, frameNumber)
            }
        })
        mainHandler.postDelayed(statsRefresh, 1000)
    }

    private val statsRefresh = object : Runnable {
        override fun run() {
            if (!active) return
            val stats = sessionClient?.statsSnapshot()
            if (stats != null) {
                val profileText = activeProfile?.let { "${it.width}×${it.height}@${it.fps} ${it.encoderMode}" } ?: "video"
                val queueAge = if (stats.queueAgeMs < 0) "—" else "${stats.queueAgeMs} ms"
                val elapsedNow = SystemClock.elapsedRealtime()
                val cpuNow = Process.getElapsedCpuTime()
                val elapsedDelta = elapsedNow - lastElapsedTimeMs
                val cpuPercent = if (elapsedDelta > 0L) 100.0 * (cpuNow - lastCpuTimeMs) / elapsedDelta else 0.0
                lastElapsedTimeMs = elapsedNow
                lastCpuTimeMs = cpuNow
                val battery = registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
                val tempTenthsC = battery?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1) ?: -1
                val batteryTemp = if (tempTenthsC >= 0) "${"%.1f".format(tempTenthsC / 10.0)}°C" else "—"
                val cameraFrameAge = if (stats.cameraFrameAgeMs < 0L) "—" else "${stats.cameraFrameAgeMs} ms"
                val thermalStatus = thermalStatusName()
                telemetryView.text = "$profileText · Camera ${"%.1f".format(stats.cameraFps)} fps (#${stats.lastCameraFrameNumber}, $cameraFrameAge) · " +
                    "encoder ${"%.1f".format(stats.encodeFps)} fps (#${stats.lastSequenceNumber}) · CPU/core ${"%.1f".format(cpuPercent)}% · " +
                    "battery $batteryTemp · thermal $thermalStatus · UDP ${stats.sent} · queue ${stats.queueDepth}/$queueAge · " +
                    "drops ${stats.queueDrops} · keyframes ${stats.keyframeRequests}"
                diagnosticSamples.add(
                    DiagnosticSample(
                        elapsedSeconds = (elapsedNow - sessionStartedElapsedMs) / 1000.0,
                        cameraFps = stats.cameraFps,
                        cameraFrameNumber = stats.lastCameraFrameNumber,
                        cameraFrameAgeMs = stats.cameraFrameAgeMs,
                        encoderFps = stats.encodeFps,
                        encoderSequence = stats.lastSequenceNumber,
                        encoderOutputAgeMs = stats.encoderOutputAgeMs,
                        processCpuPercent = cpuPercent,
                        batteryTemperatureC = if (tempTenthsC >= 0) tempTenthsC / 10.0 else null,
                        thermalStatus = thermalStatus,
                        udpPacketsSent = stats.sent,
                        senderQueueDepth = stats.queueDepth,
                        senderQueueAgeMs = stats.queueAgeMs,
                        senderQueueDrops = stats.queueDrops,
                        keyframeRequests = stats.keyframeRequests,
                    ),
                )
                while (diagnosticSamples.size > MAX_DIAGNOSTIC_SAMPLES) diagnosticSamples.removeAt(0)
                exportButton.isEnabled = diagnosticSamples.isNotEmpty()
            }
            mainHandler.postDelayed(this, 1000)
        }
    }

    private fun disconnect(statusMessage: String? = null) {
        val wasConnecting = connecting
        connecting = false
        active = false
        window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setDimmed(false)
        dimButton.isEnabled = false
        mainHandler.removeCallbacks(statsRefresh)
        cameraController.stop()
        sessionClient?.close()
        sessionClient = null
        activeProfile = null
        telemetryView.text = "Capture telemetry appears when streaming starts"
        profilePicker.isEnabled = true
        connectButton.text = "Connect"
        connectButton.isEnabled = profiles.isNotEmpty()
        statusView.text = statusMessage ?: if (wasConnecting) "Connection cancelled" else "Ready"
    }

    private fun requestDiagnosticExport() {
        if (diagnosticSamples.isEmpty()) return
        runCatching {
            val fileStamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
            val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "text/csv"
                putExtra(Intent.EXTRA_TITLE, "droidcam-clone-diagnostics-$fileStamp.csv")
            }
            startActivityForResult(intent, DIAGNOSTIC_EXPORT_REQUEST)
        }.onFailure { error ->
            statusView.text = "Could not open document picker: ${error.message ?: error.javaClass.simpleName}"
        }
    }

    @Deprecated("Deprecated in Android API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != DIAGNOSTIC_EXPORT_REQUEST || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return
        runCatching {
            contentResolver.openOutputStream(uri, "wt")?.bufferedWriter(Charsets.UTF_8)?.use { writer ->
                writer.appendLine("# phone,${csvField("${Build.MANUFACTURER} ${Build.MODEL}")}")
                writer.appendLine("# debuggable,$isDebugBuild")
                writer.appendLine("# android_version,${csvField(Build.VERSION.RELEASE)}")
                writer.appendLine("# receiver_ipv4,${csvField(activeReceiverAddress)}")
                writer.appendLine("# session_started_local,${csvField(sessionStartedAt)}")
                writer.appendLine("# profile,${csvField(diagnosticProfile?.let { "${it.width}x${it.height}@${it.fps}" } ?: "unknown")}")
                writer.appendLine("# encoder,${csvField(diagnosticProfile?.encoderName ?: "unknown")}")
                writer.appendLine("# camera_id,${csvField(diagnosticProfile?.cameraId ?: "unknown")}")
                writer.appendLine("elapsed_seconds,camera_fps,camera_frame_number,camera_callback_age_ms,encoder_fps,encoder_sequence,encoder_output_age_ms,process_cpu_percent_of_one_core,battery_temperature_c,thermal_status,udp_packets_sent,sender_queue_depth,sender_queue_age_ms,sender_queue_drops,keyframe_requests")
                diagnosticSamples.forEach { sample ->
                    writer.appendLine(
                        listOf(
                            number(sample.elapsedSeconds),
                            number(sample.cameraFps),
                            sample.cameraFrameNumber.toString(),
                            sample.cameraFrameAgeMs.toString(),
                            number(sample.encoderFps),
                            sample.encoderSequence.toString(),
                            sample.encoderOutputAgeMs.toString(),
                            number(sample.processCpuPercent),
                            sample.batteryTemperatureC?.let(::number) ?: "",
                            csvField(sample.thermalStatus),
                            sample.udpPacketsSent.toString(),
                            sample.senderQueueDepth.toString(),
                            sample.senderQueueAgeMs.toString(),
                            sample.senderQueueDrops.toString(),
                            sample.keyframeRequests.toString(),
                        ).joinToString(","),
                    )
                }
            } ?: error("Could not open the selected document")
        }.onSuccess {
            statusView.text = "Diagnostics exported (${diagnosticSamples.size} samples)"
        }.onFailure { error ->
            statusView.text = "Could not export diagnostics: ${error.message ?: error.javaClass.simpleName}"
        }
    }

    private fun csvField(value: String): String = "\"${value.replace("\"", "\"\"")}\""

    private fun number(value: Double): String = String.format(Locale.US, "%.3f", value)

    override fun surfaceCreated(holder: SurfaceHolder) = Unit

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        if (active) disconnect()
    }

    @Deprecated("Deprecated in Android API")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == CAMERA_PERMISSION_REQUEST) refreshProfiles()
    }

    override fun onDestroy() {
        disconnect()
        super.onDestroy()
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun setDimmed(dimmed: Boolean) {
        if (active) cameraController.setPreviewEnabled(!dimmed)
        dimOverlay.visibility = if (dimmed && active) View.VISIBLE else View.GONE
        dimButton.text = if (dimmed && active) "Restore" else "Dim"
        window.decorView.systemUiVisibility = if (dimmed && active) {
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        } else {
            View.SYSTEM_UI_FLAG_VISIBLE
        }
    }

    private fun thermalStatusName(): String {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return "unavailable"
        val powerManager = getSystemService(PowerManager::class.java)
        return when (powerManager.currentThermalStatus) {
            PowerManager.THERMAL_STATUS_NONE -> "none"
            PowerManager.THERMAL_STATUS_LIGHT -> "light"
            PowerManager.THERMAL_STATUS_MODERATE -> "moderate"
            PowerManager.THERMAL_STATUS_SEVERE -> "severe"
            PowerManager.THERMAL_STATUS_CRITICAL -> "critical"
            PowerManager.THERMAL_STATUS_EMERGENCY -> "emergency"
            PowerManager.THERMAL_STATUS_SHUTDOWN -> "shutdown"
            else -> "unknown"
        }
    }

    private fun isIpv4(value: String): Boolean {
        val parts = value.split('.')
        return parts.size == 4 && parts.all { part ->
            part.isNotEmpty() && part.all(Char::isDigit) && part.toIntOrNull()?.let { it in 0..255 } == true
        }
    }

    companion object {
        private const val CAMERA_PERMISSION_REQUEST = 1
        private const val DIAGNOSTIC_EXPORT_REQUEST = 2
        private const val MAX_DIAGNOSTIC_SAMPLES = 10_000
    }
}
