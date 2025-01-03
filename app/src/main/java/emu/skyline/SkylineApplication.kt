/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline

import android.app.Application
import android.content.Context
import android.graphics.Color
import android.content.res.Resources.Theme
import androidx.annotation.ColorInt
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import emu.skyline.di.getSettings
import java.io.File
import kotlin.math.roundToInt
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

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

        private val _themeChangeFlow = MutableSharedFlow<Int>()
        val themeChangeFlow = _themeChangeFlow.asSharedFlow()

        const val NAV_TYPE_THREE_BUTTON = 0
        const val NAV_TYPE_TWO_BUTTON = 1
        const val NAV_TYPE_GESTURE = 2

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

        /**
         * Determines the system navigation type.
         *
         * @param context The context used to access resources.
         * @return An integer representing the navigation type:
         * - 0: Three-button navigation
         * - 1: Two-button navigation
         * - 2: Gesture navigation
        */
        fun detectNavigationType(context: Context): Int {
            val navBarModeResource = context.resources.getIdentifier(
                "config_navBarInteractionMode",
                "integer",
                "android"
            )
            return if (navBarModeResource != 0) {
                try {
                    context.resources.getInteger(navBarModeResource)
                } catch (e: Exception) {
                    NAV_TYPE_THREE_BUTTON // Fallback to default
                }
            } else {
                NAV_TYPE_THREE_BUTTON // Fallback to default
            }
        }

        private var currentTheme: Int? = null

        fun setTheme(newValue: Boolean) {
            val newTheme = if (newValue) R.style.AppTheme_MaterialYou else R.style.AppTheme
            if (currentTheme != newTheme) {
                CoroutineScope(Dispatchers.Main).launch { _themeChangeFlow.emit(newTheme) }
                currentTheme = newTheme
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        System.loadLibrary("skyline")
    }
}
