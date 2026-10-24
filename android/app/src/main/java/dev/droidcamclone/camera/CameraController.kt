package dev.droidcamclone.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.os.Handler
import android.os.HandlerThread
import android.util.Range
import android.util.Size
import android.view.Surface
import dev.droidcamclone.codec.EncodedAccessUnit
import dev.droidcamclone.codec.H264Encoder
import dev.droidcamclone.net.VideoProfile
import java.util.concurrent.atomic.AtomicBoolean

class CameraController(context: Context) {
    interface Listener {
        fun onState(message: String)
        fun onError(message: String)
        fun onAccessUnit(accessUnit: EncodedAccessUnit)
    }

    private val appContext = context.applicationContext
    private val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var encoder: H264Encoder? = null
    private var encoderSurface: Surface? = null
    private val stopping = AtomicBoolean(false)

    fun queryProfiles(): List<VideoProfile> {
        if (appContext.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return emptyList()
        val cameraId = preferredCameraId() ?: return emptyList()
        val characteristics = cameraManager.getCameraCharacteristics(cameraId)
        val streamMap = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return emptyList()
        val size = Size(1920, 1080)
        val sizes = streamMap.getOutputSizes(SurfaceTexture::class.java)?.toSet().orEmpty()
        if (size !in sizes) return emptyList()

        val frameRanges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?.toList().orEmpty()
        val frameDuration = runCatching { streamMap.getOutputMinFrameDuration(SurfaceTexture::class.java, size) }
            .getOrDefault(0L)

        return listOf(60, 30).mapNotNull { fps ->
            val range = frameRanges
                .filter { it.upper == fps && it.lower <= fps }
                .minByOrNull { it.lower }
                ?: return@mapNotNull null
            if (frameDuration > 0 && frameDuration > 1_000_000_000L / fps) return@mapNotNull null
            val choice = H264Encoder.findEncoder(size.width, size.height, fps) ?: return@mapNotNull null
            val targetBitrate = if (fps == 60) 16_000_000 else 8_000_000
            val bitrate = targetBitrate.coerceIn(choice.minBitrate, choice.maxBitrate)
            VideoProfile(
                width = size.width,
                height = size.height,
                fps = fps,
                bitrate = bitrate,
                fpsRangeMin = range.lower,
                fpsRangeMax = range.upper,
                encoderName = choice.name,
            )
        }
    }

    fun start(previewSurface: Surface, profile: VideoProfile, listener: Listener) {
        check(cameraThread == null) { "Camera already active" }
        if (!previewSurface.isValid) {
            listener.onError("Android preview surface is not ready")
            return
        }
        if (appContext.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            listener.onError("Camera permission is missing")
            return
        }
        stopping.set(false)
        cameraThread = HandlerThread("CloneCamera2").apply { start() }
        cameraHandler = Handler(cameraThread!!.looper)

        try {
            val activeEncoder = H264Encoder(profile, object : H264Encoder.Listener {
                override fun onAccessUnit(accessUnit: EncodedAccessUnit) = listener.onAccessUnit(accessUnit)
                override fun onEncoderError(message: String) = listener.onError(message)
            })
            encoder = activeEncoder
            encoderSurface = activeEncoder.start()
            val cameraId = preferredCameraId() ?: throw IllegalStateException("No usable camera found")
            cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    if (stopping.get()) {
                        camera.close()
                        return
                    }
                    cameraDevice = camera
                    createSession(camera, previewSurface, encoderSurface!!, profile, listener)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    camera.close()
                    if (!stopping.get()) listener.onError("Camera disconnected")
                    stop()
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    camera.close()
                    listener.onError("Camera failed with error $error")
                    stop()
                }
            }, cameraHandler)
        } catch (error: Exception) {
            listener.onError(error.message ?: "Could not start camera")
            stop()
        }
    }

    fun requestKeyFrame() = encoder?.requestKeyFrame()

    fun stop() {
        if (!stopping.compareAndSet(false, true)) return
        runCatching { captureSession?.stopRepeating() }
        runCatching { captureSession?.close() }
        captureSession = null
        runCatching { cameraDevice?.close() }
        cameraDevice = null
        encoder?.stop()
        encoder = null
        runCatching { encoderSurface?.release() }
        encoderSurface = null
        cameraThread?.quitSafely()
        cameraThread = null
        cameraHandler = null
    }

    private fun createSession(
        camera: CameraDevice,
        previewSurface: Surface,
        encodedSurface: Surface,
        profile: VideoProfile,
        listener: Listener,
    ) {
        try {
            camera.createCaptureSession(listOf(previewSurface, encodedSurface), object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(session: CameraCaptureSession) {
                    if (stopping.get()) {
                        session.close()
                        return
                    }
                    captureSession = session
                    try {
                        val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                            addTarget(previewSurface)
                            addTarget(encodedSurface)
                            set(CaptureRequest.CONTROL_CAPTURE_INTENT, CaptureRequest.CONTROL_CAPTURE_INTENT_VIDEO_RECORD)
                            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                            set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(profile.fpsRangeMin, profile.fpsRangeMax))
                        }.build()
                        session.setRepeatingRequest(request, null, cameraHandler)
                        listener.onState("Camera active: 1920×1080 ${profile.fps} fps")
                    } catch (error: CameraAccessException) {
                        listener.onError("Could not start capture: ${error.message}")
                        stop()
                    }
                }

                override fun onConfigureFailed(session: CameraCaptureSession) {
                    session.close()
                    listener.onError("This phone cannot run the 1080p preview and encoder surfaces together at ${profile.fps} fps")
                    stop()
                }
            }, cameraHandler)
        } catch (error: CameraAccessException) {
            listener.onError("Could not configure camera surfaces: ${error.message}")
            stop()
        }
    }

    private fun preferredCameraId(): String? {
        val ids = cameraManager.cameraIdList
        return ids.firstOrNull { id ->
            cameraManager.getCameraCharacteristics(id).get(CameraCharacteristics.LENS_FACING) ==
                CameraCharacteristics.LENS_FACING_BACK
        } ?: ids.firstOrNull()
    }
}
