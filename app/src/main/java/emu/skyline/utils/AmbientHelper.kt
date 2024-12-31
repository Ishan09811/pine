
package emu.skyline.utils

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.view.SurfaceView
import androidx.palette.graphics.Palette

class AmbientHelper(private val surfaceView: SurfaceView) {

    interface AmbientCallback {
        fun onColorsExtracted(vibrantColor: Int, mutedColor: Int, dominantColor: Int)
        fun onError(error: String)
    }

    /**
     * Starts capturing and processing the SurfaceView for ambient effects.
     */
    fun captureAmbientEffect(callback: AmbientCallback) {
        val bitmap = Bitmap.createBitmap(
            surfaceView.width,
            surfaceView.height,
            Bitmap.Config.ARGB_8888
        )
        val location = IntArray(2)
        surfaceView.getLocationInWindow(location)

        val rect = Rect(
            location[0],
            location[1],
            location[0] + surfaceView.width,
            location[1] + surfaceView.height
        )

        // Use PixelCopy to capture the SurfaceView contents
        PixelCopy.request(
            surfaceView,
            bitmap,
            { result ->
                if (result == PixelCopy.SUCCESS) {
                    extractColors(bitmap, callback)
                } else {
                    callback.onError("Failed to capture surface content. PixelCopy result: $result")
                }
            },
            Handler(Looper.getMainLooper())
        )
    }

    /**
     * Extracts colors from a Bitmap using the Palette library.
     */
    private fun extractColors(bitmap: Bitmap, callback: AmbientCallback) {
        Palette.from(bitmap).generate { palette ->
            if (palette != null) {
                val vibrantColor = palette.getVibrantColor(Color.BLACK)
                val mutedColor = palette.getMutedColor(Color.GRAY)
                val dominantColor = palette.getDominantColor(Color.DKGRAY)

                callback.onColorsExtracted(vibrantColor, mutedColor, dominantColor)
            } else {
                callback.onError("Failed to generate palette from bitmap.")
            }
        }
    }
}
