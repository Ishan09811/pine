
package emu.skyline.ui.main

import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import emu.skyline.MainViewModel
import com.skyline.MainState
import emu.skyline.settings.AppSettings
import emu.skyline.di.getSettings
import com.skyline.utils.SearchLocationHelper 

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(viewModel: MainViewModel, navigateBack: () -> Unit) {
    val state by viewModel.stateData.collectAsState()
    val context = LocalContext.current
    val appSettings = remember { getSettings() }

    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()

    var searchQuery by rememberSaveable { mutableStateOf("") }

    val refreshLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        uri?.let {
            context.contentResolver.takePersistableUriPermission(it, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            SearchLocationHelper.addLocation(context, it)
            viewModel.loadRoms(context, false, SearchLocationHelper.getSearchLocations(context), EmulationSettings.global.systemLanguage)
        }
    }

    val settingsLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        if (appSettings.refreshRequired) viewModel.loadRoms(context, false, SearchLocationHelper.getSearchLocations(context), EmulationSettings.global.systemLanguage)
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    TextField(
                        value = searchQuery,
                        onValueChange = {
                            searchQuery = it
                        },
                        placeholder = { Text("Search") }
                    )
                },
                actions = {
                    IconButton(onClick = {
                        settingsLauncher.launch(Intent(context, SettingsActivity::class.java))
                    }) {
                        Icon(Icons.Default.Settings, contentDescription = "Settings")
                    }
                    IconButton(onClick = {
                        viewModel.loadRoms(context, false, SearchLocationHelper.getSearchLocations(context), EmulationSettings.global.systemLanguage)
                    }) {
                        Icon(Icons.Default.Refresh, contentDescription = "Refresh")
                    }
                }
            )
        },
        snackbarHost = { SnackbarHost(snackbarHostState) }
    ) { paddingValues ->
        Column(Modifier.padding(paddingValues)) {
            when (state) {
                is MainState.Loading -> {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                }
                is MainState.Loaded -> {
                    val items = (state as MainState.Loaded).items

                    LazyColumn {
                        items(items) { item ->
                            AppItemRow(item)
                        }
                    }
                }
                is MainState.Error -> {
                    scope.launch {
                        snackbarHostState.showSnackbar("Error: ${(state as MainState.Error).ex.localizedMessage}")
                    }
                }
            }
        }
    }
}
