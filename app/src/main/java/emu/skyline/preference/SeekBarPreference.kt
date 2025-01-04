
package emu.skyline.preference

import android.content.Context
import android.util.AttributeSet
import android.view.LayoutInflater
import android.view.View
import androidx.preference.DialogPreference
import androidx.preference.Preference
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import com.google.android.material.textview.MaterialTextView

class SeekBarPreference(context: Context, attrs: AttributeSet) : DialogPreference(context, attrs),
    Preference.OnPreferenceClickListener {

    private var currentValue: Float = 0f
    private var minValue: Float = 0f
    private var maxValue: Float = 100f
    private var step: Float = 1f
    private var isPercentage: Boolean = false

    init {
        // Read custom attributes
        context.theme.obtainStyledAttributes(attrs, R.styleable.MaterialSeekBarPreference, 0, 0).apply {
            try {
                isPercentage = getBoolean(R.styleable.MaterialSeekBarPreference_isPercentage, false)
            } finally {
                recycle()
            }
        }

        onPreferenceClickListener = this
    }

    override fun onPreferenceClick(preference: Preference?): Boolean {
        showMaterialDialog()
        return true
    }

    private fun showMaterialDialog() {
        val dialogView = LayoutInflater.from(context).inflate(R.layout.preference_dialog_seekbar, null)
        val slider = dialogView.findViewById<Slider>(R.id.seekBar)
        val valueText = dialogView.findViewById<MaterialTextView>(R.id.value)

        // Configure slider
        slider.valueFrom = minValue
        slider.valueTo = maxValue
        slider.stepSize = step
        slider.value = currentValue

        // Display initial value
        updateValueText(valueText, currentValue)

        slider.addOnChangeListener { _, value, _ ->
            currentValue = value
            updateValueText(valueText, value)
        }

        // Build and show the MaterialAlertDialog
        MaterialAlertDialogBuilder(context)
            .setTitle(title)
            .setView(dialogView)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                persistFloat(currentValue)
                updateSummary()
                callChangeListener(currentValue)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun updateValueText(valueText: MaterialTextView, value: Float) {
        valueText.text = if (isPercentage) {
            "${value.toInt()}%"
        } else {
            value.toInt().toString()
        }
    }

    private fun updateSummary() {
        summary = if (isPercentage) {
            "${currentValue.toInt()}%"
        } else {
            currentValue.toInt().toString()
        }
    }

    override fun onSetInitialValue(defaultValue: Any?) {
        currentValue = getPersistedFloat((defaultValue as? Float) ?: minValue)
        updateSummary()
    }
}
    
