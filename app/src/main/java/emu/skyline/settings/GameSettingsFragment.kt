/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline.settings

import android.os.Bundle
import android.view.View
import androidx.appcompat.app.AppCompatActivity
import androidx.preference.*
import emu.skyline.BuildConfig
import emu.skyline.R
import emu.skyline.data.BaseAppItem
import emu.skyline.data.AppItemTag
import emu.skyline.preference.GpuDriverPreference
import emu.skyline.preference.SeekBarPreference
import emu.skyline.utils.GpuDriverHelper
import emu.skyline.utils.WindowInsetsHelper
import emu.skyline.utils.serializable

/**
 * This fragment is used to display custom game preferences
 */
class GameSettingsFragment : PreferenceFragmentCompat() {
    private val item by lazy { requireArguments().serializable<BaseAppItem>(AppItemTag)!! }

    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val recyclerView = view.findViewById<View>(androidx.preference.R.id.recycler_view)
        WindowInsetsHelper.setPadding(recyclerView, bottom = true)

        (activity as AppCompatActivity).supportActionBar?.subtitle = item.title
    }

    /**
     * This constructs the preferences from XML preference resources
     */
    override fun onCreatePreferences(savedInstanceState : Bundle?, rootKey : String?) {
        preferenceManager.sharedPreferencesName = EmulationSettings.prefNameForTitle(item.titleId ?: item.key())
        addPreferencesFromResource(R.xml.custom_game_preferences)
        addPreferencesFromResource(R.xml.emulation_preferences)

        // Toggle emulation settings enabled state based on use_custom_settings state
        listOf<Preference?>(
            findPreference("category_system"),
            findPreference("category_presentation"),
            findPreference("category_gpu"),
            findPreference("category_hacks"),
            findPreference("category_audio"),
            findPreference("category_debug")
        ).forEach { it?.dependency = "use_custom_settings" }

        findPreference<Preference>("enable_speed_limit")?.setOnPreferenceChangeListener { _, newValue ->
            disablePreference("speed_limit", !(newValue as Boolean), null)
            true
        }

        // Only show validation layer setting in debug builds
        @Suppress("SENSELESS_COMPARISON")
        if (BuildConfig.BUILD_TYPE != "release")
            findPreference<Preference>("validation_layer")?.isVisible = true

        findPreference<GpuDriverPreference>("gpu_driver")?.item = item

        findPreference<SeekBarPreference>("executor_slot_count_scale")?.setMaxValue(Runtime.getRuntime().availableProcessors().toInt())

        findPreference<SwitchPreferenceCompat>("enable_speed_limit")?.isChecked?.let {
            disablePreference("speed_limit", !it, null)
        }
        disablePreference("force_max_gpu_clocks", !GpuDriverHelper.supportsForceMaxGpuClocks(), context?.getString(R.string.force_max_gpu_clocks_desc_unsupported))

        // Hide settings that don't support per-game configuration
        var prefToRemove = findPreference<Preference>("profile_picture_value")
        prefToRemove?.parent?.removePreference(prefToRemove)
        prefToRemove = findPreference<Preference>("log_level")
        prefToRemove?.parent?.removePreference(prefToRemove)

        // TODO: remove this once we have more settings under the debug category
        // Avoid showing the debug category if no settings under it are visible
        @Suppress("SENSELESS_COMPARISON")
        if (BuildConfig.BUILD_TYPE == "release")
            findPreference<PreferenceCategory>("category_debug")?.isVisible = false
    }
    
    private fun disablePreference(
        preferenceId: String, 
        isDisabled: Boolean, 
        disabledSummary: String? = null
    ) {
        val preference = findPreference<Preference>(preferenceId)!!
        preference.isSelectable = !isDisabled
        preference.isEnabled = !isDisabled
        if (preference is TwoStatePreference && isDisabled) {
            preference.isChecked = false
        }
        if (isDisabled && disabledSummary != null) {
            preference.summary = disabledSummary
        }
    }
}
