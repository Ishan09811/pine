
package emu.skyline.ui.main

import android.content.Intent
import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.Image
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.text.style.TextOverflow
import kotlinx.coroutines.launch
import emu.skyline.MainViewModel
import emu.skyline.MainState
import emu.skyline.EmulationActivity
import emu.skyline.data.BaseAppItem
import emu.skyline.loader.LoaderResult
import emu.skyline.settings.AppSettings
import emu.skyline.di.getSettings
import emu.skyline.settings.EmulationSettings
import emu.skyline.settings.SettingsActivity
import emu.skyline.utils.SearchLocationHelper 

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(viewModel: MainViewModel, navigateBack: () -> Unit) {
    val state by viewModel.stateData.observeAsState()
    val context = LocalContext.current
    val appSettings = remember { context.getSettings() }

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
                            AppItemRow(
                                item, 
                                onClick = {
                                    startGame(context, item)
                                }
                            )
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

@Composable
fun AppItemRow(
    item: BaseAppItem,
    modifier: Modifier = Modifier,
    onClick: () -> Unit = {}
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(10.dp)
    ) {
        Card(
            shape = MaterialTheme.shapes.large,
            elevation = CardDefaults.cardElevation(defaultElevation = 6.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Image(
                painter = BitmapPainter(item.bitmapIcon.asImageBitmap()),
                contentDescription = "App Icon",
                modifier = Modifier
                    .fillMaxWidth()
                    .wrapContentHeight()
                    .padding(8.dp),
                contentScale = ContentScale.Fit
            )
        }

        Text(
            text = item.title ?: "",
            style = MaterialTheme.typography.titleSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .padding(horizontal = 10.dp)
        )

        Text(
            text = item.version ?: "",
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .padding(horizontal = 10.dp)
        )

        Text(
            text = item.author ?: "",
            style = MaterialTheme.typography.bodySmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier
                .padding(horizontal = 10.dp, vertical = 5.dp)
        )
    }
}

fun startGame(ctx: Context, appItem : BaseAppItem) {
    if (appItem.loaderResult == LoaderResult.Success) {
        ctx.startActivity(Intent(ctx, EmulationActivity::class.java).apply {
            putExtra(AppItemTag, appItem)
            putExtra(EmulationActivity.ReturnToMainTag, true)
        })
    }
}
