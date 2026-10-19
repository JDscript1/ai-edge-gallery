// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.runtime.gguf

import org.junit.Assert.assertEquals
import org.junit.Test

class Utf8TokenDecoderTest {
  @Test fun preservesRomanianAndEmojiAtEveryByteBoundary() {
    val text = "Bună! Știință, țară, 🦊 și 🌳."
    val decoder = Utf8TokenDecoder()
    val result = StringBuilder()
    text.toByteArray(Charsets.UTF_8).forEach { result.append(decoder.decode(byteArrayOf(it))) }
    result.append(decoder.decode(ByteArray(0), end = true))
    assertEquals(text, result.toString())
  }

  @Test fun incompleteTokenDoesNotProduceReplacementCharacterWhileStreaming() {
    val decoder = Utf8TokenDecoder()
    val bytes = "🌳".toByteArray(Charsets.UTF_8)
    assertEquals("", decoder.decode(bytes.copyOfRange(0, 3)))
    assertEquals("🌳", decoder.decode(bytes.copyOfRange(3, 4)))
    assertEquals("", decoder.decode(ByteArray(0), end = true))
  }

  @Test fun cancelledStreamFlushesIncompleteUtf8Safely() {
    val decoder = Utf8TokenDecoder()
    assertEquals("", decoder.decode(byteArrayOf(0xC8.toByte())))
    assertEquals("\uFFFD", decoder.decode(ByteArray(0), end = true))
  }
}
