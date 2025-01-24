
package emu.skyline.utils

import android.content.Context
import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns
import emu.skyline.SkylineApplication
import emu.skyline.getPublicFilesDir
import java.io.*

class ContentsHelper(private val context: Context) {

    private val fileName = "contents.dat"

    fun saveContents(contents: List<Serializable>) {
        try {
            val fileOutputStream = context.openFileOutput(fileName, Context.MODE_PRIVATE)
            val objectOutputStream = ObjectOutputStream(fileOutputStream)
            objectOutputStream.writeObject(contents)
            objectOutputStream.close()
            fileOutputStream.close()
        } catch (e: IOException) {
            e.printStackTrace()
        }
    }

    fun loadContents(): List<Serializable> {
        return try {
            val fileInputStream = context.openFileInput(fileName)
            val objectInputStream = ObjectInputStream(fileInputStream)
            val contents = objectInputStream.readObject() as List<Serializable>
            objectInputStream.close()
            fileInputStream.close()
            contents
        } catch (e: FileNotFoundException) {
            emptyList()
        } catch (e: IOException) {
            e.printStackTrace()
            emptyList()
        } catch (e: ClassNotFoundException) {
            e.printStackTrace()
            emptyList()
        }
    }

    fun save(uri: Uri, cr: ContentResolver): Uri? {
        val destinationDir = File("${SkylineApplication.instance.getPublicFilesDir().canonicalPath}/contents/")
        if (!destinationDir.exists()) destinationDir.mkdirs()

        val fileName = getFileName(uri, cr) ?: return null
        val destinationFile = File(destinationDir, fileName)

        try {
            cr.openInputStream(uri)?.use { inputStream ->
                FileOutputStream(destinationFile).use { outputStream ->
                    inputStream.copyTo(outputStream)
                }
            }
            return Uri.fromFile(destinationFile)
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return null
    }

    fun getFileName(uri: Uri, cr: ContentResolver): String? {
        if (uri.scheme == "content") {
            val cursor = cr.query(uri, null, null, null, null)
            cursor?.use {
                if (it.moveToFirst()) {
                    val nameIndex = it.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME)
                    return it.getString(nameIndex)
                }
            }
        } else if (uri.scheme == "file") {
            return File(uri.path!!).name
        }
        return null
    }

    fun getUriSize(context: Context, uri: Uri): Long? {
        var size: Long? = null
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
            if (sizeIndex != -1) {
                cursor.moveToFirst()
                size = cursor.getLong(sizeIndex)
            }
        }
        return size
    }
}
