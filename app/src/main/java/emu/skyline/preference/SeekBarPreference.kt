
package emu.skyline.preference

import android.content.Context
import android.util.AttributeSet
import android.view.View
import androidx.preference.DialogPreference
import com.google.android.material.slider.Slider
import com.google.android.material.textview.MaterialTextView

class SeekBarPreference(context: Context, attrs: AttributeSet) : DialogPreference(context, attrs) {
    private var currentValue: Float = 0f
    private var minValue: Float = 0f
    private var maxValue: Float = 100f
    private var step: Float = 1f
    private var isPercentage: Boolean = false

    init {
        dialogLayoutResource = R.layout.preference_dialog_seekbar

        // Read the custom attribute
        context.theme.obtainStyledAttributes(attrs, R.styleable.CustomSeekBarPreference, 0, 0).apply {
            try {
                isPercentage = getBoolean(R.styleable.CustomSeekBarPreference_isPercentage, false)
            } finally {
                recycle()
            }
        }
    }

    override fun onBindDialogView(view: View) {
        super.onBindDialogView(view)

        val slider = view.findViewById<Slider>(R.id.seekBar)
        val valueText = view.findViewById<MaterialTextView>(R.id.value)

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
    }

    private fun updateValueText(valueText: MaterialTextView, value: Float) {
        valueText.text = if (isPercentage) {
            "${value.toInt()}%"
        } else {
            value.toInt().toString()
        }
    }

    override fun onDialogClosed(positiveResult: Boolean) {
        if (positiveResult) {
            persistFloat(currentValue)
            callChangeListener(currentValue)
        }
    }

    override fun onSetInitialValue(defaultValue: Any?) {
        currentValue = getPersistedFloat((defaultValue as? Float) ?: minValue)
    }
}
