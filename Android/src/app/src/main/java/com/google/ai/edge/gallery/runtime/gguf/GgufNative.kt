// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.runtime.gguf

/** Byte arrays preserve standard UTF-8, including emoji, across the JNI boundary. */
internal object GgufNative {
  // Keep the original loader error available on every attempt. Throwing from
  // the object initializer would replace it with NoClassDefFoundError later.
  private val loadFailure = runCatching { System.loadLibrary("gallery_gguf") }.exceptionOrNull()

  fun checkAvailable() {
    loadFailure?.let { cause ->
      throw UnsatisfiedLinkError(cause.message ?: "Cannot load gallery_gguf").apply {
        initCause(cause)
      }
    }
  }

  /** Reports whether this build has a GPU backend; per-device availability is only known at load. */
  external fun supportsGpuOffload(): Boolean
  external fun open(path: ByteArray, contextSize: Int, threads: Int, gpuLayers: Int): Long
  external fun close(handle: Long)
  external fun prepare(handle: Long)
  external fun cancel(handle: Long)
  external fun supportsThinking(handle: Long): Boolean
  external fun count(handle: Long, text: ByteArray, formatted: Boolean, thinking: Boolean = false): Int
  external fun generate(
    handle: Long,
    history: ByteArray,
    maxTokens: Int,
    topK: Int,
    topP: Float,
    temperature: Float,
    thinking: Boolean,
    callback: TokenCallback,
  )

  fun interface TokenCallback { fun onToken(bytes: ByteArray, thinking: Boolean) }
}
