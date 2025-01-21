
package emu.skyline.utils

import android.content.Context
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
}
