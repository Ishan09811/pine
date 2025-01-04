
package emu.skyline.preference

import android.content.Context
import android.util.AttributeSet
import androidx.preference.DialogPreference
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import android.widget.SeekBar
import android.widget.TextView
import android.view.View
import android.widget.LinearLayout

class SeekBarPreference(context: Context, attrs: AttributeSet) : DialogPreference(context, attrs) {
    private var currentValue: Int = 0
    private var minValue: Int = 0
    private var maxValue: Int = 100
    private var step: Int = 1

    init {
        dialogLayoutResource = R.layout.preference_dialog_seekbar
    }

    override fun onBindDialogView(view: View) {
        super.onBindDialogView(view)
        val seekBar = view.findViewById<SeekBar>(R.id.seekBar)
        val valueText = view.findViewById<TextView>(R.id.valueText)

        seekBar.max = (maxValue - minValue) / step
        seekBar.progress = (currentValue - minValue) / step
        valueText.text = currentValue.toString()

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                currentValue = minValue + progress * step
                valueText.text = currentValue.toString()
            }

            override fun onStartTrackingTouch(seekBar: SeekBar) {}
            override fun onStopTrackingTouch(seekBar: SeekBar) {}
        })
    }

    override fun onDialogClosed(positiveResult: Boolean) {
        if (positiveResult) {
            persistInt(currentValue)
            callChangeListener(currentValue)
        }
    }

    override fun onSetInitialValue(defaultValue: Any?) {
        currentValue = getPersistedInt(defaultValue as? Int ?: 0)
    }
}
