
package emu.skyline.ui.main

import android.content.Intent
import android.content.Context
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
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
import androidx.hilt.navigation.compose.hiltViewModel
import kotlinx.coroutines.launch
import emu.skyline.MainViewModel
import emu.skyline.MainState
import emu.skyline.EmulationActivity
import emu.skyline.data.BaseAppItem
import emu.skyline.data.AppItem
import emu.skyline.data.AppItemTag
import emu.skyline.loader.LoaderResult
import emu.skyline.loader.AppEntry
import emu.skyline.loader.RomType
import emu.skyline.settings.AppSettings
import emu.skyline.di.getSettings
import emu.skyline.settings.EmulationSettings
import emu.skyline.settings.SettingsActivity
import emu.skyline.utils.SearchLocationHelper 

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(navigateBack: () -> Unit) {
    val viewModel: MainViewModel = hiltViewModel()
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
                    OutlinedTextField(
                        value = searchQuery,
                        onValueChange = { searchQuery = it },
                        placeholder = { Text("Search") },
                        leadingIcon = {
                            Icon(Icons.Default.Search, contentDescription = "Search Icon")
                        },
                        singleLine = true,
                        shape = MaterialTheme.shapes.large,
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 8.dp),
                        colors = OutlinedTextFieldDefaults.colors(
                            unfocusedBorderColor = Color.Transparent,
                            focusedBorderColor = Color.Transparent,
                            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant,
                            focusedContainerColor = MaterialTheme.colorScheme.surface,
                            unfocusedLeadingIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            focusedLeadingIconColor = MaterialTheme.colorScheme.onSurface,
                            unfocusedPlaceholderColor = MaterialTheme.colorScheme.onSurfaceVariant,
                            focusedPlaceholderColor = MaterialTheme.colorScheme.onSurface,
                            cursorColor = MaterialTheme.colorScheme.primary
                        )
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
            val items = if (state is MainState.Loaded) {
                getAppItems((state as MainState.Loaded).items, appSettings)
            } else {
                emptyList()
            }

            val filteredItems = if (searchQuery.isBlank()) {
                items
            } else {
                items.filter {
                    it.title?.contains(searchQuery, ignoreCase = true) == true ||
                    it.author?.contains(searchQuery, ignoreCase = true) == true ||
                    it.version?.contains(searchQuery, ignoreCase = true) == true
                }
            }

            if (state is MainState.Loading) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }

            LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(filteredItems) { item ->
                    AppItemRow(
                        item = item,
                        modifier = Modifier.fillMaxWidth(),
                        onClick = { startGame(context, item) }
                    )
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
            elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Image(
                painter = BitmapPainter(item.bitmapIcon.asImageBitmap()),
                contentDescription = "App Icon",
                modifier = Modifier
                    .fillMaxWidth()
                    .wrapContentHeight()
                    .padding(0.dp),
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

fun getAppItems(appEntries: List<AppEntry>, appSettings: AppSettings) = mutableListOf<BaseAppItem>().apply {
    appEntries?.let { entries ->
        sortGameList(entries.toList(), appSettings).forEach { entry ->
            val updates : List<BaseAppItem> = entries.filter { it.romType == RomType.Update && it.parentTitleId == entry.titleId }.map { BaseAppItem(it, true) }
            val dlcs : List<BaseAppItem> = entries.filter { it.romType == RomType.DLC && it.parentTitleId == entry.titleId }.map { BaseAppItem(it, true) }
            add(AppItem(entry, updates, dlcs))
        }
    }
}

private fun sortGameList(gameList : List<AppEntry>, appSettings: AppSettings) : List<AppEntry> {
    val sortedApps : MutableList<AppEntry> = mutableListOf()
    gameList.forEach { entry ->
        if (validateAppEntry(entry, appSettings))
            sortedApps.add(entry)
    }
    when (appSettings.sortAppsBy) {
        SortingOrder.AlphabeticalAsc.ordinal -> sortedApps.sortBy { it.name }
        SortingOrder.AlphabeticalDesc.ordinal -> sortedApps.sortByDescending { it.name }
    }
    return sortedApps
}

fun validateAppEntry(entry : AppEntry, appSettings: AppSettings) : Boolean {
    // Unknown ROMs are shown because NROs have this type
    return !appSettings.filterInvalidFiles || entry.loaderResult != LoaderResult.ParsingError && (entry.romType == RomType.Base || entry.romType == RomType.Unknown)
}

enum class SortingOrder {
    AlphabeticalAsc,
    AlphabeticalDesc
}
