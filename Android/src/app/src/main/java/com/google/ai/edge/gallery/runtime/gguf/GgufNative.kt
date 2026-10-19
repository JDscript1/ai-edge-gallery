// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.runtime.gguf

/** Byte arrays preserve standard UTF-8, including emoji, across the JNI boundary. */
internal object GgufNative {
  init { System.loadLibrary("gallery_gguf") }

  external fun open(path: ByteArray, contextSize: Int, threads: Int): Long
  external fun close(handle: Long)
  external fun prepare(handle: Long)
  external fun cancel(handle: Long)
  external fun count(handle: Long, text: ByteArray, formatted: Boolean): Int
  external fun generate(
    handle: Long,
    history: ByteArray,
    maxTokens: Int,
    topK: Int,
    topP: Float,
    temperature: Float,
    callback: TokenCallback,
  )

  fun interface TokenCallback { fun onToken(bytes: ByteArray) }
}
