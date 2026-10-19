// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.runtime.gguf

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CodingErrorAction

/** Token boundaries can split a Romanian letter or emoji across several pieces. */
internal class Utf8TokenDecoder {
  private val decoder = Charsets.UTF_8.newDecoder()
    .onMalformedInput(CodingErrorAction.REPLACE)
    .onUnmappableCharacter(CodingErrorAction.REPLACE)
  private var pending = ByteArray(0)

  fun decode(bytes: ByteArray, end: Boolean = false): String {
    val input = ByteBuffer.wrap(pending + bytes)
    val output = CharBuffer.allocate(input.remaining() + 4)
    decoder.decode(input, output, end)
    pending = ByteArray(input.remaining()).also { input.get(it) }
    if (end) decoder.flush(output)
    output.flip()
    return output.toString()
  }
}
