/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline

import android.app.Application
import android.content.Context
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import emu.skyline.di.getSettings
import java.io.File

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

        fun setTheme(newValue: Boolean) {
            val dynamicColorsOptions = DynamicColorsOptions.Builder().setPrecondition { _, _ -> newValue }.build()
            DynamicColors.applyToActivitiesIfAvailable(instance, dynamicColorsOptions)
            if (newValue == false) { instance.setTheme(R.style.AppTheme) }
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
