// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.data

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Copy to a temporary file so a cancelled or invalid import preserves an installed model. */
internal fun copyGgufModel(
  source: InputStream,
  destination: File,
  expectedSize: Long,
  checkCancellation: () -> Unit,
  onBytesCopied: (Long) -> Unit,
) {
  val temporary = File.createTempFile(".gguf-import-", ".part", destination.parentFile)
  try {
    source.buffered().use { input ->
      val header = ByteArray(4)
      var read = 0
      while (read < header.size) {
        val n = input.read(header, read, header.size - read)
        require(n > 0) { "The selected GGUF file is incomplete" }
        read += n
      }
      require(header.contentEquals(byteArrayOf(0x47, 0x47, 0x55, 0x46))) {
        "The selected file is not a GGUF model"
      }
      FileOutputStream(temporary).use { output ->
        output.write(header)
        var copied = 4L
        val buffer = ByteArray(64 * 1024)
        while (true) {
          checkCancellation()
          val n = input.read(buffer)
          if (n < 0) break
          output.write(buffer, 0, n)
          copied += n
          onBytesCopied(copied)
        }
        require(expectedSize <= 0L || copied == expectedSize) { "The GGUF file was not copied completely" }
        checkCancellation()
        output.fd.sync()
      }
    }
    checkCancellation()
    Files.move(temporary.toPath(), destination.toPath(), StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
  } finally {
    temporary.delete()
  }
}
