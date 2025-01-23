/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package emu.skyline

import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.net.Uri
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.os.Bundle
import android.provider.DocumentsContract
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.activityViewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.ViewModelProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.snackbar.Snackbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import emu.skyline.data.BaseAppItem
import emu.skyline.data.AppItemTag
import emu.skyline.databinding.AppDialogBinding
import emu.skyline.loader.LoaderResult
import emu.skyline.loader.RomFile
import emu.skyline.loader.RomType
import emu.skyline.loader.RomFormat
import emu.skyline.loader.RomFormat.*
import emu.skyline.loader.AppEntry
import emu.skyline.settings.SettingsActivity
import emu.skyline.settings.EmulationSettings
import emu.skyline.utils.CacheManagementUtils
import emu.skyline.utils.SaveManagementUtils
import emu.skyline.utils.serializable
import emu.skyline.utils.ContentsHelper
import emu.skyline.model.TaskViewModel
import emu.skyline.fragments.IndeterminateProgressDialogFragment
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream

/**
 * This dialog is used to show extra game metadata and provide extra options such as pinning the game to the home screen
 */
class AppDialog : BottomSheetDialogFragment() {
    companion object {
        /**
         * @param item This is used to hold the [BaseAppItem] between instances
         */
        fun newInstance(item : BaseAppItem) : AppDialog {
            val args = Bundle()
            args.putSerializable(AppItemTag, item)

            val fragment = AppDialog()
            fragment.arguments = args
            return fragment
        }
    }

    private lateinit var binding : AppDialogBinding

    private val item by lazy { requireArguments().serializable<BaseAppItem>(AppItemTag)!! }

    /**
     * Used to manage save files
     */
    private lateinit var documentPicker : ActivityResultLauncher<Array<String>>
    private lateinit var startForResultExportSave : ActivityResultLauncher<Intent>

    private val contents by lazy { ContentsHelper(requireContext()) }

    private lateinit var expectedContentType: RomType
    
    private lateinit var contentPickerLauncher: ActivityResultLauncher<Intent>

    private val taskViewModel : TaskViewModel by activityViewModels()

    override fun onCreate(savedInstanceState : Bundle?) {
        super.onCreate(savedInstanceState)
        documentPicker = SaveManagementUtils.registerDocumentPicker(requireActivity()) {
            val isSaveFileOfThisGame = SaveManagementUtils.saveFolderGameExists(item.titleId)
            binding.deleteSave.isEnabled = isSaveFileOfThisGame
            binding.exportSave.isEnabled = isSaveFileOfThisGame
        }
        
        startForResultExportSave = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                result.data?.data?.let { uri ->
                    val pickedDir = DocumentFile.fromTreeUri(requireContext(), uri) ?: return@let
                    CoroutineScope(Dispatchers.IO).launch {
                        val zipFilePath = "${SkylineApplication.instance.getPublicFilesDir().canonicalPath}/temp/${SaveManagementUtils.saveZipName}"
                        val zipFile = File(zipFilePath)
                        val inputStream: InputStream = zipFile.inputStream()
                        val outputStream: OutputStream? = requireContext().contentResolver.openOutputStream(pickedDir.createFile("application/zip", zipFile.name)?.uri!!)
                        
                        inputStream.use { input ->
                            outputStream?.use { output ->
                                input.copyTo(output)
                            }
                        }
                    }
                }
            }
        }

        contentPickerLauncher = registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                val uri: Uri? = result.data?.data
                val task: () -> Unit = { 
                    val result = loadContent(uri)
                    val contentType = if (expectedContentType == RomType.DLC) "DLCs" else "Update" 
                    when (result) {
                        LoaderResult.Success -> {
                           Snackbar.make(binding.root, "Imported ${contentType} successfully", Snackbar.LENGTH_SHORT).show()
                        }
                        LoaderResult.ParsingError -> {
                           Snackbar.make(binding.root, "Failed to import ${contentType}", Snackbar.LENGTH_SHORT).show()
                        }
                        else -> {
                           Snackbar.make(binding.root, "Unknown error occurred", Snackbar.LENGTH_SHORT).show()
                        }
                    }
                }

                val uriSize = contents.getUriSize(requireContext(), uri!!) ?: null

                if (uriSize != null && uriSize.toInt() > 100 * 1024 * 1024) {
                    IndeterminateProgressDialogFragment.newInstance(requireActivity() as AppCompatActivity, R.string.importing, task).show(parentFragmentManager, IndeterminateProgressDialogFragment.TAG)
                } else {
                    ViewModelProvider(requireActivity())[TaskViewModel::class.java].task = task
                    taskViewModel.runTask()
                    taskViewModel.isComplete.observe(this) { isComplete ->
                        if (!isComplete)
                            return@observe
                        taskViewModel.clear()
                    }
                }
            }
        }
    }

    /**
     * This inflates the layout of the dialog after initial view creation
     */
    override fun onCreateView(inflater : LayoutInflater, container : ViewGroup?, savedInstanceState : Bundle?) = AppDialogBinding.inflate(inflater).also { binding = it }.root

    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // Set the peek height after the root view has been laid out
        view.apply {
            post {
                val behavior = BottomSheetBehavior.from(parent as View)
                behavior.peekHeight = height
            }
        }

        binding.gameIcon.setImageBitmap(item.bitmapIcon)
        binding.gameTitle.text = item.title
        binding.gameVersion.text = item.version ?: item.loaderResultString(requireContext())
        binding.gameTitleId.text = item.titleId
        binding.gameAuthor.text = item.author

        binding.gamePlay.isEnabled = item.loaderResult == LoaderResult.Success
        binding.gamePlay.setOnClickListener {
            startActivity(Intent(activity, EmulationActivity::class.java).apply {
                putExtras(requireArguments())
            })
        }

        binding.gameSettings.isEnabled = item.loaderResult == LoaderResult.Success
        binding.gameSettings.setOnClickListener {
            startActivity(Intent(activity, SettingsActivity::class.java).apply {
                putExtras(requireArguments())
            })
        }

        val shortcutManager = requireActivity().getSystemService(ShortcutManager::class.java)
        binding.gamePin.isEnabled = shortcutManager.isRequestPinShortcutSupported

        binding.gamePin.setOnClickListener {
            val info = ShortcutInfo.Builder(context, item.title)
            item.title?.let { title -> info.setShortLabel(title) }
            info.setActivity(ComponentName(requireContext(), EmulationActivity::class.java))
            info.setIcon(Icon.createWithAdaptiveBitmap(item.bitmapIcon))

            val intent = Intent(context, EmulationActivity::class.java)
            intent.data = item.uri
            intent.action = Intent.ACTION_VIEW

            info.setIntent(intent)

            shortcutManager.requestPinShortcut(info.build(), null)
        }

        val saveExists = SaveManagementUtils.saveFolderGameExists(item.titleId)

        binding.deleteSave.isEnabled = saveExists
        binding.deleteSave.setOnClickListener {
            AlertDialog.Builder(requireContext())
                .setTitle(getString(R.string.delete_save_confirmation_message))
                .setMessage(getString(R.string.action_irreversible))
                .setNegativeButton(getString(R.string.cancel), null)
                .setPositiveButton(getString(android.R.string.ok)) { _, _ ->
                    SaveManagementUtils.deleteSaveFile(item.titleId)
                    binding.deleteSave.isEnabled = false
                    binding.exportSave.isEnabled = false
                }.show()
        }

        val cacheExists = CacheManagementUtils.pipelineCacheExists(item.titleId!!)

        binding.cacheDelete.isEnabled = cacheExists
        binding.cacheDelete.setOnClickListener {
            AlertDialog.Builder(requireContext())
                .setTitle(getString(R.string.delete_shader_cache_confirmation_message))
                .setMessage(getString(R.string.action_irreversible))
                .setNegativeButton(getString(R.string.cancel), null)
                .setPositiveButton(getString(android.R.string.ok)) { _, _ ->
                    if(CacheManagementUtils.deleteCacheFile(item.titleId!!)) {
                        binding.cacheDelete.isEnabled = false
                    }
                }.show()
        }

        binding.importSave.setOnClickListener {
            SaveManagementUtils.importSave(documentPicker)
        }

        binding.exportSave.isEnabled = saveExists
        binding.exportSave.setOnClickListener {
            SaveManagementUtils.exportSave(requireContext(), startForResultExportSave, item.titleId, "${item.title} (v${binding.gameVersion.text}) [${item.titleId}]")
        }

        binding.importUpdate.setOnClickListener {
            expectedContentType = RomType.Update
            openContentPicker()
        }

        binding.importDlcs.setOnClickListener {
            expectedContentType = RomType.DLC
            openContentPicker()
        }

        binding.deleteContents.isEnabled = !contents.loadContents().isEmpty()

        binding.deleteContents.setOnClickListener {
            var contentList = contents.loadContents().toMutableList()
            val currentItemContentList = contentList.filter { appEntry ->
                (appEntry as AppEntry).parentTitleId == item.titleId
            }
            val contentNames = currentItemContentList.map { contents.getFileName((it as AppEntry).uri!!, requireContext().contentResolver) }.toTypedArray()
            var selectedItemIndex = 0
            MaterialAlertDialogBuilder(requireContext())
                .setTitle("Contents")
                .setSingleChoiceItems(contentNames, selectedItemIndex) { _, which ->
                    selectedItemIndex = which
                }
                .setPositiveButton("Remove") { _, _ ->
                    val selectedContent = currentItemContentList[selectedItemIndex]
                    File((selectedContent as AppEntry).uri.path).delete()
                    contentList.remove(selectedContent)
                    contents.saveContents(contentList)
                }
                .setNegativeButton("Cancel") { dialog, _ ->
                    dialog.dismiss()
                }
                .show()
        }

        binding.gameTitleId.setOnLongClickListener {
            val clipboard = requireActivity().getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
            clipboard.setPrimaryClip(android.content.ClipData.newPlainText("Title ID", item.titleId))
            Snackbar.make(binding.root, getString(R.string.copied_to_clipboard), Snackbar.LENGTH_SHORT).show()
            true
        }

        dialog?.setOnKeyListener { _, keyCode, event ->
            if (keyCode == KeyEvent.KEYCODE_BUTTON_B && event.action == KeyEvent.ACTION_UP) {
                dialog?.onBackPressed()
                true
            } else {
                false
            }
        }
    }

    private fun loadContent(uri: Uri?): LoaderResult {
        if (uri == Uri.EMPTY || uri == null) {
            return LoaderResult.ParsingError
        }
        
        mapOf(
            "nro" to NRO,
            "nso" to NSO,
            "nca" to NCA,
            "nsp" to NSP,
            "xci" to XCI
        )[contents.getFileName(uri!!, requireContext().contentResolver)?.substringAfterLast(".")?.lowercase()]?.let { contentFormat ->

            val newContent = RomFile(
                requireContext(),
                contentFormat,
                contents.save(uri!!, requireContext().contentResolver)!!,
                EmulationSettings.global.systemLanguage
            )

            val currentContents = contents.loadContents().toMutableList()
            val isDuplicate = currentContents.any { (it as AppEntry).uri == newContent.appEntry.uri }
            if (!isDuplicate && newContent.result == LoaderResult.Success && newContent.appEntry.romType == expectedContentType) {
                currentContents.add(newContent.appEntry)
                contents.saveContents(currentContents)
                return LoaderResult.Success
            } else if (!isDuplicate) File(newContent.appEntry.uri.path).delete()
        }

        return LoaderResult.ParsingError
    }

    private fun openContentPicker() {
        val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "*/*"
        }
        contentPickerLauncher.launch(intent)
    }
}
