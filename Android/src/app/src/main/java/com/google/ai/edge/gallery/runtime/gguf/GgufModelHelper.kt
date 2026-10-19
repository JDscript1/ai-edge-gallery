// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.runtime.gguf

import android.content.Context
import android.graphics.Bitmap
import com.google.ai.edge.gallery.data.ConfigKeys
import com.google.ai.edge.gallery.data.Model
import com.google.ai.edge.gallery.data.markInitializationFailed
import com.google.ai.edge.gallery.data.markInitialized
import com.google.ai.edge.gallery.data.resetInitialization
import com.google.ai.edge.gallery.runtime.CleanUpListener
import com.google.ai.edge.gallery.runtime.EXTRA_CONTEXT_MAX_OUTPUT_TOKENS
import com.google.ai.edge.gallery.runtime.LlmModelHelper
import com.google.ai.edge.gallery.runtime.ResultListener
import com.google.ai.edge.litertlm.Contents
import com.google.ai.edge.litertlm.Message
import com.google.ai.edge.litertlm.Role
import com.google.ai.edge.litertlm.ToolProvider
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

private data class GgufMessage(val role: String, val content: String)

private class GgufInstance(val handle: Long) {
  val mutex = Mutex()
  val busy = AtomicBoolean(false)
  val closing = AtomicBoolean(false)
  val history = mutableListOf<GgufMessage>()
  val revision = AtomicLong(0)
  var cleanUpListener: CleanUpListener? = null
}

/** Local ARM64 CPU chat using the pinned PrismML llama.cpp runtime. */
object GgufModelHelper : LlmModelHelper {
  private val background = CoroutineScope(SupervisorJob() + Dispatchers.IO)

  private fun messages(system: Contents?, initial: List<Message>): List<GgufMessage> = buildList {
    system?.toString()?.takeIf { it.isNotBlank() }?.let { add(GgufMessage("system", it)) }
    initial.forEach {
      val role = when (it.role) {
        Role.USER -> "user"
        Role.SYSTEM -> "system"
        else -> "assistant"
      }
      add(GgufMessage(role, it.contents.toString()))
    }
  }

  private fun encode(messages: List<GgufMessage>): ByteArray {
    val array = JSONArray()
    messages.forEach {
      array.put(JSONObject().put("role", it.role).put("content", it.content))
    }
    return array.toString().toByteArray(Charsets.UTF_8)
  }

  override fun initialize(
    context: Context, model: Model, taskId: String, supportImage: Boolean, supportAudio: Boolean,
    onDone: (String) -> Unit, systemInstruction: Contents?, tools: List<ToolProvider>,
    enableConversationConstrainedDecoding: Boolean, initialMessages: List<Message>,
    coroutineScope: CoroutineScope?, enableResponseFormat: Boolean,
  ) {
    background.launch {
      var openedHandle = 0L
      try {
        require(!supportImage && !supportAudio && tools.isEmpty() && !enableResponseFormat) {
          "GGUF currently supports text chat and Prompt Lab. Image, audio, tools and JSON schemas need separate integrations."
        }
        if (model.instance is GgufInstance) {
          withContext(Dispatchers.Main) { onDone("") }
          return@launch
        }
        val contextSize = model.getIntConfigValue(ConfigKeys.MAX_TOKENS, 2048).coerceIn(256, 8192)
        openedHandle = GgufNative.open(
          model.getPath(context).toByteArray(Charsets.UTF_8), contextSize,
          Runtime.getRuntime().availableProcessors().coerceIn(1, 4),
        )
        check(openedHandle != 0L) { "Cannot initialize the GGUF engine" }
        check(!model.cleanUpAfterInit) { "Model loading was cancelled" }
        val instance = GgufInstance(openedHandle)
        instance.history.addAll(messages(systemInstruction, initialMessages))
        model.markInitialized(instance)
        openedHandle = 0L
        withContext(Dispatchers.Main) { onDone("") }
      } catch (error: Exception) {
        if (openedHandle != 0L) GgufNative.close(openedHandle)
        model.markInitializationFailed(error)
        withContext(Dispatchers.Main) { onDone(error.message ?: "GGUF initialization failed") }
      } catch (error: LinkageError) {
        model.markInitializationFailed(error)
        withContext(Dispatchers.Main) { onDone("GGUF native runtime is unavailable in this APK: ${error.message}") }
      }
    }
  }

  override fun resetConversation(
    model: Model, supportImage: Boolean, supportAudio: Boolean, systemInstruction: Contents?,
    tools: List<ToolProvider>, enableConversationConstrainedDecoding: Boolean,
    initialMessages: List<Message>, enableResponseFormat: Boolean,
  ) {
    val instance = model.instance as? GgufInstance ?: return
    GgufNative.cancel(instance.handle)
    synchronized(instance.history) {
      instance.revision.incrementAndGet()
      instance.history.clear()
      instance.history.addAll(messages(systemInstruction, initialMessages))
    }
  }

  override fun cleanUp(model: Model, onDone: () -> Unit) {
    val instance = model.instance as? GgufInstance
    if (instance == null) { onDone(); return }
    instance.closing.set(true)
    GgufNative.cancel(instance.handle)
    background.launch {
      instance.mutex.withLock {
        GgufNative.close(instance.handle)
        if (model.instance === instance) model.resetInitialization()
      }
      withContext(Dispatchers.Main) {
        instance.cleanUpListener?.invoke()
        instance.cleanUpListener = null
        onDone()
      }
    }
  }

  override fun runInference(
    model: Model, input: String, resultListener: ResultListener, cleanUpListener: CleanUpListener,
    onError: (String) -> Unit, images: List<Bitmap>, audioClips: List<ByteArray>,
    coroutineScope: CoroutineScope?, extraContext: Map<String, String>, responseJsonSchema: String?,
  ) {
    val instance = model.instance as? GgufInstance
    if (instance == null || instance.closing.get()) { onError("GGUF model is not initialized"); return }
    if (images.isNotEmpty() || audioClips.isNotEmpty() || responseJsonSchema != null) {
      onError("This GGUF integration accepts text prompts only")
      return
    }
    if (!instance.busy.compareAndSet(false, true)) { onError("The model is already generating"); return }
    instance.cleanUpListener = cleanUpListener
    // Reset before scheduling, so a Stop tap cannot be lost before the worker starts.
    GgufNative.prepare(instance.handle)
    val parent = coroutineScope?.coroutineContext?.get(Job)
    val completion = parent?.invokeOnCompletion { cause ->
      if (cause != null && !instance.closing.get()) runCatching { GgufNative.cancel(instance.handle) }
    }
    background.launch {
      try {
        instance.mutex.withLock {
          check(!instance.closing.get()) { "GGUF model is closing" }
          parent?.ensureActive()
          val user = GgufMessage("user", input)
          val (revision, history) = synchronized(instance.history) {
            instance.revision.get() to (instance.history.toList() + user)
          }
          val response = StringBuilder()
          val decoder = Utf8TokenDecoder()
          val maxOutput = extraContext[EXTRA_CONTEXT_MAX_OUTPUT_TOKENS]?.toIntOrNull()
            ?: model.getIntConfigValue(ConfigKeys.MAX_OUTPUT_TOKENS, 512)
          GgufNative.generate(
            instance.handle, encode(history), maxOutput.coerceIn(1, 4096),
            model.getIntConfigValue(ConfigKeys.TOPK, 40),
            model.getFloatConfigValue(ConfigKeys.TOPP, 0.95f),
            model.getFloatConfigValue(ConfigKeys.TEMPERATURE, 0.7f),
          ) { bytes ->
            val piece = decoder.decode(bytes)
            response.append(piece)
            if (piece.isNotEmpty()) resultListener(piece, false, null)
          }
          val tail = decoder.decode(ByteArray(0), end = true)
          if (tail.isNotEmpty()) { response.append(tail); resultListener(tail, false, null) }
          synchronized(instance.history) {
            if (response.isNotEmpty() && revision == instance.revision.get()) {
              instance.history.add(user)
              instance.history.add(GgufMessage("assistant", response.toString()))
            }
          }
        }
        resultListener("", true, null)
      } catch (error: CancellationException) {
        resultListener("", true, null)
      } catch (error: Exception) {
        onError(error.message ?: "GGUF inference failed")
      } finally {
        completion?.dispose()
        instance.busy.set(false)
      }
    }
  }

  override fun stopResponse(model: Model) {
    (model.instance as? GgufInstance)?.let { GgufNative.cancel(it.handle) }
  }

  override suspend fun countTokens(model: Model, text: String): Int? = withContext(Dispatchers.IO) {
    val instance = model.instance as? GgufInstance ?: return@withContext null
    instance.mutex.withLock {
      if (instance.closing.get()) null
      else GgufNative.count(instance.handle, text.toByteArray(Charsets.UTF_8), false)
    }
  }

  override suspend fun countInputTokens(model: Model, input: String): Int? = withContext(Dispatchers.IO) {
    val instance = model.instance as? GgufInstance ?: return@withContext null
    instance.mutex.withLock {
      if (instance.closing.get()) null
      else {
        val history = synchronized(instance.history) { instance.history.toList() }
        GgufNative.count(instance.handle, encode(history + GgufMessage("user", input)), true)
      }
    }
  }
}
