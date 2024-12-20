/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline

import android.app.Application
import android.content.Context
import android.graphics.Color
import androidx.annotation.ColorInt
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import emu.skyline.di.getSettings
import java.io.File
import kotlin.math.roundToInt

/**
 * @return The optimal directory for putting public files inside, this may return a private directory if a public directory cannot be retrieved
 */
fun Context.getPublicFilesDir() : File = getExternalFilesDir(null) ?: filesDir

@HiltAndroidApp
class SkylineApplication : Application() {
    init {
        instance = this
    }

    companion object {
        lateinit var instance : SkylineApplication
            private set

        val context : Context get() = instance.applicationContext

        /**
         * Adjusts the opacity of a color by applying an alpha factor.
         *
         * @param color The original color (including alpha).
         * @param alphaFactor A value between 0.0 (fully transparent) and 1.0 (no change in opacity).
         * @return A new color with the adjusted opacity.
        */
        @ColorInt
        fun applyAlphaToColor(@ColorInt color: Int, alphaFactor: Float): Int {
            val newAlpha = (Color.alpha(color) * alphaFactor).coerceIn(0f, 255f).roundToInt()
            return Color.argb(
                newAlpha,
                Color.red(color),
                Color.green(color),
                Color.blue(color)
            )
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        System.loadLibrary("skyline")

        val dynamicColorsOptions = DynamicColorsOptions.Builder().setPrecondition { _, _ -> getSettings().useMaterialYou }.build()
        DynamicColors.applyToActivitiesIfAvailable(this, dynamicColorsOptions)
    }
}
