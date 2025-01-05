
package emu.skyline.preference

import android.content.Context
import android.util.AttributeSet
import android.util.Log
import android.view.LayoutInflater
import android.view.View
import androidx.preference.DialogPreference
import androidx.preference.Preference
import emu.skyline.R
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import com.google.android.material.textview.MaterialTextView

class SeekBarPreference(context: Context, attrs: AttributeSet) : DialogPreference(context, attrs) {

    private var currentValue: Number = 0 // Use Number to hold either Int or Float
    private var fallbackValue: Number = 0
    private var minValue: Number = 0
    private var maxValue: Number = 100
    private var step: Float = 1f
    private var isPercentage: Boolean = false

    init {
        // Read custom attributes
        context.theme.obtainStyledAttributes(attrs, R.styleable.MaterialSeekBarPreference, 0, 0).apply {
            try {
                isPercentage = getBoolean(R.styleable.MaterialSeekBarPreference_isPercentage, false)
                val attrMinValue = getString(R.styleable.MaterialSeekBarPreference_minValue)?.toIntOrNull() ?: 0
                val attrMaxValue = getString(R.styleable.MaterialSeekBarPreference_maxValue)?.toIntOrNull() ?: 100
                minValue = attrMinValue
                maxValue = attrMaxValue
                val attrDefaultValue = getString(R.styleable.MaterialSeekBarPreference_defValue)?.toIntOrNull() ?: 0
                fallbackValue = attrDefaultValue
            } finally {
                recycle()
            }
        }
        if (!isPersistent || currentValue.toInt() < minValue.toInt()) { 
            currentValue = fallbackValue 
            if (isPercentage) {
                persistFloat(currentValue.toFloat())
            } else {
                persistInt(currentValue.toInt())
            }
        }
    }

    override fun onClick() { showMaterialDialog() }

    private fun showMaterialDialog() {
        val dialogView = LayoutInflater.from(context).inflate(R.layout.preference_dialog_seekbar, null)
        val slider = dialogView.findViewById<Slider>(R.id.seekBar)
        val valueText = dialogView.findViewById<MaterialTextView>(R.id.value)
        
        // Configure slider
        slider.valueFrom = if (isPercentage) minValue.toFloat() else minValue.toInt().toFloat()
        slider.valueTo = if (isPercentage) maxValue.toFloat() else maxValue.toInt().toFloat()
        slider.stepSize = step
        slider.value = if (isPercentage) currentValue.toFloat() else currentValue.toInt().toFloat()

        // Display initial value
        updateValueText(valueText, slider.value)

        slider.addOnChangeListener { _, value, _ ->
            updateValueText(valueText, value)
            currentValue = if (isPercentage) value else value.toInt()
        }

        var dismissTrigger: String? = null

        // Build and show the MaterialAlertDialog
        MaterialAlertDialogBuilder(context)
            .setTitle(title)
            .setView(dialogView)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                dismissTrigger = "positive_button"
                if (isPercentage) {
                    persistFloat(currentValue.toFloat())
                } else {
                    persistInt(currentValue.toInt())
                }
                updateSummary()
                callChangeListener(currentValue)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .setOnDismissListener {
                if (dismissTrigger != "positive_button")
                    slider.value = summary?.toString()?.replace("%", "")?.toIntOrNull()?.toFloat() ?: 0f
            }
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
            "${currentValue.toFloat().toInt()}%"
        } else {
            currentValue.toInt().toString()
        }
    }

    override fun onSetInitialValue(defaultValue: Any?) {
        val actualDefaultValue = when (defaultValue) {
            is String -> defaultValue.toIntOrNull() ?: minValue.toInt()
            is Int -> defaultValue ?: minValue.toInt()
            is Float -> defaultValue.toInt()
            else -> minValue.toInt() // fallback to minValue if default is invalid
        }
        currentValue = if (!isPercentage) getPersistedInt(actualDefaultValue!!)!! else getPersistedFloat(actualDefaultValue.toFloat()!!).toInt()!!
        updateSummary()
    }

    fun setMaxValue(max: Any) { 
        if (isPercentage) maxValue = max as Float else maxValue = max as Int
    }

    fun setMinValue(min: Any) {
        if (isPercentage) minValue = min as Float else minValue = min as Int
    }
}
