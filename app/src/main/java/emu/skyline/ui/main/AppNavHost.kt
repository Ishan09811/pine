
package emu.skyline.ui.main

import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.Composable
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import emu.skyline.MainViewModel

@Composable
fun AppNavHost(viewModel: MainViewModel) {
    //val context = LocalContext.current
    val navController = rememberNavController()

    NavHost(
        navController = navController,
        startDestination = "main"
    ) {
        composable(
            route = "main"
        ) {
            MainScreen(
                navigateBack = navController::navigateUp,
                viewModel = viewModel
            )
        }
    }
}
