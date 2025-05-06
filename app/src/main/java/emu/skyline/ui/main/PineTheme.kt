
package emu.skyline.ui.main

import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.foundation.isSystemInDarkTheme
import android.app.Activity
import androidx.compose.runtime.*
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsControllerCompat

object colors {
    val primaryLight = Color(0xFF4C662B)
    val onPrimaryLight = Color(0xFFFFFFFF)
    val primaryContainerLight = Color(0xFFCDEDA3)
    val onPrimaryContainerLight = Color(0xFF354E16)
    val secondaryLight = Color(0xFF586249)
    val onSecondaryLight = Color(0xFFFFFFFF)
    val secondaryContainerLight = Color(0xFFDCE7C8)
    val onSecondaryContainerLight = Color(0xFF404A33)
    val tertiaryLight = Color(0xFF386663)
    val onTertiaryLight = Color(0xFFFFFFFF)
    val tertiaryContainerLight = Color(0xFFBCECE7)
    val onTertiaryContainerLight = Color(0xFF1F4E4B)
    val errorLight = Color(0xFFBA1A1A)
    val onErrorLight = Color(0xFFFFFFFF)
    val errorContainerLight = Color(0xFFFFDAD6)
    val onErrorContainerLight = Color(0xFF93000A)
    val backgroundLight = Color(0xFFF9FAEF)
    val onBackgroundLight = Color(0xFF1A1C16)
    val surfaceLight = Color(0xFFF9FAEF)
    val onSurfaceLight = Color(0xFF1A1C16)
    val surfaceVariantLight = Color(0xFFE1E4D5)
    val onSurfaceVariantLight = Color(0xFF44483D)
    val outlineLight = Color(0xFF75796C)
    val outlineVariantLight = Color(0xFFC5C8BA)
    val scrimLight = Color(0xFF000000)
    val inverseSurfaceLight = Color(0xFF2F312A)
    val inverseOnSurfaceLight = Color(0xFFF1F2E6)
    val inversePrimaryLight = Color(0xFFB1D18A)
    val surfaceDimLight = Color(0xFFDADBD0)
    val surfaceBrightLight = Color(0xFFF9FAEF)
    val surfaceContainerLowestLight = Color(0xFFFFFFFF)
    val surfaceContainerLowLight = Color(0xFFF3F4E9)
    val surfaceContainerLight = Color(0xFFEEEFE3)
    val surfaceContainerHighLight = Color(0xFFE8E9DE)
    val surfaceContainerHighestLight = Color(0xFFE2E3D8)

    val primaryDark = Color(0xFFB1D18A)
    val onPrimaryDark = Color(0xFF1F3701)
    val primaryContainerDark = Color(0xFF354E16)
    val onPrimaryContainerDark = Color(0xFFCDEDA3)
    val secondaryDark = Color(0xFFBFCBAD)
    val onSecondaryDark = Color(0xFF2A331E)
    val secondaryContainerDark = Color(0xFF404A33)
    val onSecondaryContainerDark = Color(0xFFDCE7C8)
    val tertiaryDark = Color(0xFFA0D0CB)
    val onTertiaryDark = Color(0xFF003735)
    val tertiaryContainerDark = Color(0xFF1F4E4B)
    val onTertiaryContainerDark = Color(0xFFBCECE7)
    val errorDark = Color(0xFFFFB4AB)
    val onErrorDark = Color(0xFF690005)
    val errorContainerDark = Color(0xFF93000A)
    val onErrorContainerDark = Color(0xFFFFDAD6)
    val backgroundDark = Color(0xFF12140E)
    val onBackgroundDark = Color(0xFFE2E3D8)
    val surfaceDark = Color(0xFF12140E)
    val onSurfaceDark = Color(0xFFE2E3D8)
    val surfaceVariantDark = Color(0xFF44483D)
    val onSurfaceVariantDark = Color(0xFFC5C8BA)
    val outlineDark = Color(0xFF8F9285)
    val outlineVariantDark = Color(0xFF44483D)
    val scrimDark = Color(0xFF000000)
    val inverseSurfaceDark = Color(0xFFE2E3D8)
    val inverseOnSurfaceDark = Color(0xFF2F312A)
    val inversePrimaryDark = Color(0xFF4C662B)
    val surfaceDimDark = Color(0xFF12140E)
    val surfaceBrightDark = Color(0xFF383A32)
    val surfaceContainerLowestDark = Color(0xFF0C0F09)
    val surfaceContainerLowDark = Color(0xFF1A1C16)
    val surfaceContainerDark = Color(0xFF1E201A)
    val surfaceContainerHighDark = Color(0xFF282B24)
    val surfaceContainerHighestDark = Color(0xFF33362E)
}

private val lightScheme = lightColorScheme(
    primary = colors.primaryLight,
    onPrimary = colors.onPrimaryLight,
    primaryContainer = colors.primaryContainerLight,
    onPrimaryContainer = colors.onPrimaryContainerLight,
    secondary = colors.secondaryLight,
    onSecondary = colors.onSecondaryLight,
    secondaryContainer = colors.secondaryContainerLight,
    onSecondaryContainer = colors.onSecondaryContainerLight,
    tertiary = colors.tertiaryLight,
    onTertiary = colors.onTertiaryLight,
    tertiaryContainer = colors.tertiaryContainerLight,
    onTertiaryContainer = colors.onTertiaryContainerLight,
    error = colors.errorLight,
    onError = colors.onErrorLight,
    errorContainer = colors.errorContainerLight,
    onErrorContainer = colors.onErrorContainerLight,
    background = colors.backgroundLight,
    onBackground = colors.onBackgroundLight,
    surface = colors.surfaceLight,
    onSurface = colors.onSurfaceLight,
    surfaceVariant = colors.surfaceVariantLight,
    onSurfaceVariant = colors.onSurfaceVariantLight,
    outline = colors.outlineLight,
    outlineVariant = colors.outlineVariantLight,
    scrim = colors.scrimLight,
    inverseSurface = colors.inverseSurfaceLight,
    inverseOnSurface = colors.inverseOnSurfaceLight,
    inversePrimary = colors.inversePrimaryLight,
    surfaceDim = colors.surfaceDimLight,
    surfaceBright = colors.surfaceBrightLight,
    surfaceContainerLowest = colors.surfaceContainerLowestLight,
    surfaceContainerLow = colors.surfaceContainerLowLight,
    surfaceContainer = colors.surfaceContainerLight,
    surfaceContainerHigh = colors.surfaceContainerHighLight,
    surfaceContainerHighest = colors.surfaceContainerHighestLight,
)

private val darkScheme = darkColorScheme(
    primary = colors.primaryDark,
    onPrimary = colors.onPrimaryDark,
    primaryContainer = colors.primaryContainerDark,
    onPrimaryContainer = colors.onPrimaryContainerDark,
    secondary = colors.secondaryDark,
    onSecondary = colors.onSecondaryDark,
    secondaryContainer = colors.secondaryContainerDark,
    onSecondaryContainer = colors.onSecondaryContainerDark,
    tertiary = colors.tertiaryDark,
    onTertiary = colors.onTertiaryDark,
    tertiaryContainer = colors.tertiaryContainerDark,
    onTertiaryContainer = colors.onTertiaryContainerDark,
    error = colors.errorDark,
    onError = colors.onErrorDark,
    errorContainer = colors.errorContainerDark,
    onErrorContainer = colors.onErrorContainerDark,
    background = colors.backgroundDark,
    onBackground = colors.onBackgroundDark,
    surface = colors.surfaceDark,
    onSurface = colors.onSurfaceDark,
    surfaceVariant = colors.surfaceVariantDark,
    onSurfaceVariant = colors.onSurfaceVariantDark,
    outline = colors.outlineDark,
    outlineVariant = colors.outlineVariantDark,
    scrim = colors.scrimDark,
    inverseSurface = colors.inverseSurfaceDark,
    inverseOnSurface = colors.inverseOnSurfaceDark,
    inversePrimary = colors.inversePrimaryDark,
    surfaceDim = colors.surfaceDimDark,
    surfaceBright = colors.surfaceBrightDark,
    surfaceContainerLowest = colors.surfaceContainerLowestDark,
    surfaceContainerLow = colors.surfaceContainerLowDark,
    surfaceContainer = colors.surfaceContainerDark,
    surfaceContainerHigh = colors.surfaceContainerHighDark,
    surfaceContainerHighest = colors.surfaceContainerHighestDark,
)

@Composable
fun PineTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit
) {
    val colors = if (darkTheme) darkScheme else lightScheme

    val view = LocalView.current
    val activity = view.context as? Activity

    SideEffect {
        activity?.window?.apply {
            statusBarColor = android.graphics.Color.TRANSPARENT
            navigationBarColor = android.graphics.Color.TRANSPARENT
            isNavigationBarContrastEnforced = false
            val insetsController = WindowInsetsControllerCompat(this, decorView)
            insetsController.isAppearanceLightNavigationBars = !darkTheme
            insetsController.isAppearanceLightStatusBars = !darkTheme
        }
    }

    MaterialTheme(
        colorScheme = colors,
        typography = Typography(),
        content = content
    )
}
