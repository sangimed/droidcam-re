package dev.droidcamclone.camera

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
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
        fun onCaptureFrame(sensorTimestampNs: Long, frameNumber: Long)
    }

    private val appContext = context.applicationContext
    private val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null
    private var encoder: H264Encoder? = null
    private var encoderSurface: Surface? = null
    private var activePreviewSurface: Surface? = null
    private var activeProfile: VideoProfile? = null
    private var activeListener: Listener? = null
    @Volatile private var previewEnabled = true
    private val stopping = AtomicBoolean(false)

    fun queryProfiles(): List<VideoProfile> {
        if (appContext.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return emptyList()
        val cameraId = preferredCameraId() ?: return emptyList()
        val characteristics = cameraManager.getCameraCharacteristics(cameraId)
        val streamMap = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: return emptyList()
        val sizes = streamMap.getOutputSizes(SurfaceTexture::class.java)?.toSet().orEmpty()
        val supportedSizes = listOf(Size(1280, 720), Size(1920, 1080)).filter { it in sizes }
        if (supportedSizes.isEmpty()) return emptyList()

        val frameRanges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?.toList().orEmpty()
        return supportedSizes.flatMap { size -> listOf(60, 30).flatMap { fps ->
            val range = chooseFpsRange(frameRanges.map { it.lower to it.upper }, fps)
                ?: return@flatMap emptyList()
            val frameDuration = runCatching { streamMap.getOutputMinFrameDuration(SurfaceTexture::class.java, size) }
                .getOrDefault(0L)
            if (frameDuration > 0 && frameDuration > 1_000_000_000L / fps) return@flatMap emptyList()
            val choices = H264Encoder.findEncoders(size.width, size.height, fps)
            val targetBitrate = when {
                fps == 60 -> 16_000_000
                size.width == 1280 -> 4_000_000
                else -> 8_000_000
            }
            choices.map { choice ->
                VideoProfile(
                    width = size.width,
                    height = size.height,
                    fps = fps,
                    bitrate = targetBitrate.coerceIn(choice.minBitrate, choice.maxBitrate),
                    fpsRangeMin = range.first,
                    fpsRangeMax = range.second,
                    encoderName = choice.name,
                    encoderMode = choice.mode,
                    cameraId = cameraId,
                )
            }
        } }
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
        previewEnabled = true
        activePreviewSurface = previewSurface
        activeProfile = profile
        activeListener = listener
        cameraThread = HandlerThread("CloneCamera2").apply { start() }
        cameraHandler = Handler(cameraThread!!.looper)

        try {
            val activeEncoder = H264Encoder(profile, object : H264Encoder.Listener {
                override fun onAccessUnit(accessUnit: EncodedAccessUnit) = listener.onAccessUnit(accessUnit)
                override fun onEncoderError(message: String) = listener.onError(message)
            })
            encoder = activeEncoder
            encoderSurface = activeEncoder.start()
            val cameraId = profile.cameraId ?: preferredCameraId() ?: throw IllegalStateException("No usable camera found")
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

    fun setPreviewEnabled(enabled: Boolean) {
        previewEnabled = enabled
        cameraHandler?.post {
            if (stopping.get()) return@post
            val camera = cameraDevice ?: return@post
            val session = captureSession ?: return@post
            val preview = activePreviewSurface ?: return@post
            val encoded = encoderSurface ?: return@post
            val profile = activeProfile ?: return@post
            val listener = activeListener ?: return@post
            submitRepeatingRequest(camera, session, preview, encoded, profile, listener)
        }
    }

    fun stop() {
        if (!stopping.compareAndSet(false, true)) return
        runCatching { captureSession?.stopRepeating() }
        runCatching { captureSession?.close() }
        captureSession = null
        runCatching { cameraDevice?.close() }
        cameraDevice = null
        activePreviewSurface = null
        activeProfile = null
        activeListener = null
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
                    submitRepeatingRequest(camera, session, previewSurface, encodedSurface, profile, listener)
                    if (stopping.get()) return
                    listener.onState("Camera ${camera.id} active: ${profile.width}×${profile.height} ${profile.fps} fps · ${profile.encoderMode} · ${profile.encoderName}")
                }

                override fun onConfigureFailed(session: CameraCaptureSession) {
                    session.close()
                    listener.onError("This phone cannot run the ${profile.width}×${profile.height} preview and encoder surfaces together at ${profile.fps} fps")
                    stop()
                }
            }, cameraHandler)
        } catch (error: CameraAccessException) {
            listener.onError("Could not configure camera surfaces: ${error.message}")
            stop()
        }
    }

    private fun submitRepeatingRequest(
        camera: CameraDevice,
        session: CameraCaptureSession,
        previewSurface: Surface,
        encodedSurface: Surface,
        profile: VideoProfile,
        listener: Listener,
    ) {
        try {
            val request = camera.createCaptureRequest(CameraDevice.TEMPLATE_RECORD).apply {
                if (previewEnabled) addTarget(previewSurface)
                addTarget(encodedSurface)
                set(CaptureRequest.CONTROL_CAPTURE_INTENT, CaptureRequest.CONTROL_CAPTURE_INTENT_VIDEO_RECORD)
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(profile.fpsRangeMin, profile.fpsRangeMax))
            }.build()
            session.setRepeatingRequest(request, object : CameraCaptureSession.CaptureCallback() {
                override fun onCaptureCompleted(
                    session: CameraCaptureSession,
                    request: CaptureRequest,
                    result: TotalCaptureResult,
                ) {
                    val timestampNs = result.get(CaptureResult.SENSOR_TIMESTAMP) ?: return
                    listener.onCaptureFrame(timestampNs, result.frameNumber)
                }
            }, cameraHandler)
        } catch (error: CameraAccessException) {
            listener.onError("Could not update capture targets: ${error.message}")
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
