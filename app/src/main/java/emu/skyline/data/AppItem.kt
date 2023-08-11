/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline.data

import android.content.Context
import android.graphics.Bitmap
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.toBitmap
import emu.skyline.BuildConfig
import emu.skyline.R
import emu.skyline.SkylineApplication
import emu.skyline.loader.AppEntry
import emu.skyline.loader.LoaderResult
import java.io.Serializable

/**
 * The tag used to pass [AppItem]s between activities and fragments
 */
const val AppItemTag = BuildConfig.APPLICATION_ID + ".APP_ITEM"

private val missingIcon by lazy { ContextCompat.getDrawable(SkylineApplication.instance, R.drawable.default_icon)!!.toBitmap(256, 256) }

/**
 * This class is a wrapper around [AppEntry], it is used for passing around game metadata
 */
@Suppress("SERIAL")
class AppItem(meta : AppEntry, private val updates : List<BaseAppItem>, private val dlcs : List<BaseAppItem>) : BaseAppItem(meta), Serializable {

    fun getEnabledDlcs() : List<BaseAppItem> {
        return dlcs.filter { it.enabled }
    }

    fun getEnabledUpdate() : BaseAppItem? {
        return updates.firstOrNull { it.enabled }
    }
}
