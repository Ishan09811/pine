/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline.settings

import android.content.Intent
import android.os.Bundle
import android.os.Build
import android.view.View
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import androidx.preference.TwoStatePreference
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import emu.skyline.BuildConfig
import emu.skyline.MainActivity
import emu.skyline.R
import emu.skyline.utils.GpuDriverHelper
import emu.skyline.utils.WindowInsetsHelper
import emu.skyline.SkylineApplication
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * This fragment is used to display the global preferences
 */
class GlobalSettingsFragment : PreferenceFragmentCompat() {
    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val recyclerView = view.findViewById<View>(androidx.preference.R.id.recycler_view)
        WindowInsetsHelper.setPadding(recyclerView, bottom = true)
    }

    /**
     * This constructs the preferences from XML preference resources
     */
    override fun onCreatePreferences(savedInstanceState : Bundle?, rootKey : String?) {
        addPreferencesFromResource(R.xml.app_preferences)
        addPreferencesFromResource(R.xml.emulation_preferences)
        addPreferencesFromResource(R.xml.input_preferences)
        addPreferencesFromResource(R.xml.credits_preferences)

        // Re-launch the app if Material You is toggled
        findPreference<Preference>("use_material_you")?.setOnPreferenceChangeListener { _, newValue ->
            val isMaterialYouEnabled = newValue as Boolean
            SkylineApplication.setTheme(isMaterialYouEnabled)
            true
        }

        CoroutineScope(Dispatchers.IO).launch {
            WindowInfoTracker.getOrCreate(requireContext()).windowLayoutInfo(requireActivity()).collect { newLayoutInfo ->
                withContext(Dispatchers.Main) {
                    findPreference<SwitchPreferenceCompat>("enable_foldable_layout")?.isVisible = newLayoutInfo.displayFeatures.find { it is FoldingFeature } != null
                }
            }
        }

        // Uncheck `disable_frame_throttling` if `force_triple_buffering` gets disabled
        val disableFrameThrottlingPref = findPreference<TwoStatePreference>("disable_frame_throttling")!!
        findPreference<TwoStatePreference>("force_triple_buffering")?.setOnPreferenceChangeListener { _, newValue ->
            if (newValue == false)
                disableFrameThrottlingPref.isChecked = false
            true
        }

        // Only show validation layer setting in debug builds
        @Suppress("SENSELESS_COMPARISON")
        if (BuildConfig.BUILD_TYPE != "release")
            findPreference<Preference>("validation_layer")?.isVisible = true

        disablePreference("use_material_you", Build.VERSION.SDK_INT < Build.VERSION_CODES.S, null)
        disablePreference("force_max_gpu_clocks", !GpuDriverHelper.supportsForceMaxGpuClocks(), context!!.getString(R.string.force_max_gpu_clocks_desc_unsupported))
        resources.getStringArray(R.array.credits_entries).asIterable().shuffled().forEach {
            findPreference<PreferenceCategory>("category_credits")?.addPreference(Preference(context!!).apply {
                title = it
            })
        }
    }

    fun disablePreference(
        preferenceId: String, 
        isEnabled: Boolean, 
        disabledSummary: String? = null
    ) {
        val preference = findPreference<Preference>(preferenceId)!!
        preference.isSelectable = !isEnabled
        preference.isEnabled = !isEnabled
        if (preference is TwoStatePreference) {
            if (!isEnabled) {
                preference.isChecked = false
            }
        }
        if (!isEnabled && disabledSummary != null) {
            preference.summary = disabledSummary
        }
    }
}
