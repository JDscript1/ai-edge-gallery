// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.data

import java.io.IOException
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class GgufImportTest {
  @get:Rule val directory = TemporaryFolder()

  @Test fun validCopyIsCommittedAndProgressReportsRealBytes() {
    val destination = directory.newFile("model.gguf")
    val bytes = "GGUF".toByteArray() + ByteArray(100000) { 7 }
    var copied = 0L
    copyGgufModel(bytes.inputStream(), destination, bytes.size.toLong(), {}) { copied = it }
    assertArrayEquals(bytes, destination.readBytes())
    assertEquals(bytes.size.toLong(), copied)
    assertEquals(1, directory.root.listFiles()!!.size)
  }

  @Test fun invalidHeaderPreservesPreviousModel() {
    val destination = directory.newFile("model.gguf").apply { writeText("previous") }
    assertThrows(IllegalArgumentException::class.java) {
      copyGgufModel("not a model".byteInputStream(), destination, 0, {}, {})
    }
    assertEquals("previous", destination.readText())
    assertEquals(1, directory.root.listFiles()!!.size)
  }

  @Test fun interruptedCopyPreservesPreviousModelAndRemovesTemporaryFile() {
    val destination = directory.newFile("model.gguf").apply { writeText("previous") }
    val bytes = "GGUF".toByteArray() + ByteArray(100000)
    assertThrows(IOException::class.java) {
      copyGgufModel(bytes.inputStream(), destination, bytes.size.toLong(), { throw IOException("cancelled") }, {})
    }
    assertEquals("previous", destination.readText())
    assertEquals(1, directory.root.listFiles()!!.size)
  }

  @Test fun truncatedSourceCannotReplacePreviousModel() {
    val destination = directory.newFile("model.gguf").apply { writeText("previous") }
    assertThrows(IllegalArgumentException::class.java) {
      copyGgufModel("GGUFshort".byteInputStream(), destination, 100000, {}, {})
    }
    assertEquals("previous", destination.readText())
    assertEquals(1, directory.root.listFiles()!!.size)
  }
}
