package dev.droidcamclone.app

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.SurfaceHolder
import android.view.SurfaceView
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

class MainActivity : Activity(), SurfaceHolder.Callback {
    private val mainHandler = Handler(Looper.getMainLooper())
    private lateinit var cameraController: CameraController
    private lateinit var preview: SurfaceView
    private lateinit var addressInput: EditText
    private lateinit var profilePicker: Spinner
    private lateinit var statusView: TextView
    private lateinit var connectButton: Button
    private var profiles: List<VideoProfile> = emptyList()
    private var sessionClient: SessionClient? = null
    private var active = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        cameraController = CameraController(this)
        buildUi()
        preview.holder.addCallback(this)
        preview.holder.setFixedSize(1920, 1080)
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
        statusView = TextView(this).apply {
            text = "Waiting for camera permission"
            setTextColor(Color.DKGRAY)
            textSize = 14f
        }
        preview = SurfaceView(this)
        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(addressInput, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 2f))
            addView(profilePicker, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.4f))
            addView(connectButton, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        }
        connectButton.setOnClickListener { if (active) disconnect() else connect() }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(12), dp(8), dp(12), dp(8))
            addView(controls)
            addView(statusView)
            addView(preview, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
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
            profiles.map { "1080p ${it.fps} fps · ${it.bitrate / 1_000_000} Mbps" },
        )
        statusView.text = when {
            profiles.isEmpty() -> "This camera/encoder does not report a supported 1080p30 or 1080p60 profile"
            profiles.none { it.fps == 60 } -> "1080p60 unavailable; 1080p30 is available"
            profiles.none { it.fps == 30 } -> "1080p60 available; 1080p30 unavailable"
            else -> "Ready · select 1080p60 or 1080p30"
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
            statusView.text = "No supported 1080p profile selected"
            return
        }
        if (!preview.holder.surface.isValid) {
            statusView.text = "Android preview is not ready"
            return
        }

        connectButton.isEnabled = false
        statusView.text = "Connecting to $address:${dev.droidcamclone.net.SessionProtocol.CONTROL_PORT}…"
        val client = SessionClient()
        sessionClient = client
        client.connect(address, profile, object : SessionClient.Listener {
            override fun onConnected(accept: SessionAccept) {
                mainHandler.post { startCamera(profile, client, accept) }
            }

            override fun onKeyframeRequested() {
                mainHandler.post { cameraController.requestKeyFrame() }
            }

            override fun onDisconnected(message: String) {
                mainHandler.post {
                    statusView.text = "Disconnected: $message"
                    disconnect()
                }
            }
        })
    }

    private fun startCamera(profile: VideoProfile, client: SessionClient, accept: SessionAccept) {
        if (client !== sessionClient || isFinishing) return
        active = true
        connectButton.text = "Disconnect"
        connectButton.isEnabled = true
        statusView.text = "Session accepted · UDP ${accept.videoPort} · starting camera"
        cameraController.start(preview.holder.surface, profile, object : CameraController.Listener {
            override fun onState(message: String) {
                mainHandler.post { if (active) statusView.text = message }
            }

            override fun onError(message: String) {
                mainHandler.post {
                    statusView.text = message
                    disconnect()
                }
            }

            override fun onAccessUnit(accessUnit: EncodedAccessUnit) {
                client.sendAccessUnit(accessUnit.data, accessUnit.presentationTimeUs)
            }
        })
        mainHandler.postDelayed(statsRefresh, 1000)
    }

    private val statsRefresh = object : Runnable {
        override fun run() {
            if (!active) return
            val stats = sessionClient?.statsSnapshot()
            if (stats != null) {
                statusView.text = "Access units ${stats.encoded} · UDP packets ${stats.sent} · queue drops ${stats.queueDrops} · " +
                    "keyframe requests ${stats.keyframeRequests}"
            }
            mainHandler.postDelayed(this, 1000)
        }
    }

    private fun disconnect() {
        active = false
        mainHandler.removeCallbacks(statsRefresh)
        cameraController.stop()
        sessionClient?.close()
        sessionClient = null
        connectButton.text = "Connect"
        connectButton.isEnabled = profiles.isNotEmpty()
    }

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

    private fun isIpv4(value: String): Boolean {
        val parts = value.split('.')
        return parts.size == 4 && parts.all { part ->
            part.isNotEmpty() && part.all(Char::isDigit) && part.toIntOrNull()?.let { it in 0..255 } == true
        }
    }

    companion object {
        private const val CAMERA_PERMISSION_REQUEST = 1
    }
}
