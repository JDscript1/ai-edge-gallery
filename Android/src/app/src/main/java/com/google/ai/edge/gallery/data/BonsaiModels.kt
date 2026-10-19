// SPDX-License-Identifier: Apache-2.0
package com.google.ai.edge.gallery.data

/** Bundled starter models remain available when the upstream catalog cannot be fetched. */
object BonsaiModels {
  val models: List<AllowedModel> = listOf(
    model(
      size = "1.7B", bytes = 248302272L,
      revision = "210a9e99f79cb184909d49595906526eb2b3dd9a", minRamGb = 3,
      description = "Small 1-bit Bonsai text model. Start here on phones with 4–5 GB RAM. " +
        "Runs locally on the CPU with a 2048-token context; actual memory and speed depend on the device.",
    ),
    model(
      size = "4B", bytes = 572270624L,
      revision = "78f2c2bacd0904ffaba24b4873ed975e5818354a", minRamGb = 4,
      description = "Larger 1-bit Bonsai text model for local CPU chat. " +
        "Use the 1.7B model if memory is limited or generation is too slow.",
    ),
  )

  private fun model(size: String, bytes: Long, revision: String, minRamGb: Int, description: String) = AllowedModel(
    name = "Bonsai-$size-Q1_0",
    modelId = "prism-ml/Bonsai-$size-gguf",
    modelFile = "Bonsai-$size-Q1_0.gguf",
    commitHash = revision,
    description = description,
    sizeInBytes = bytes,
    runtimeType = RuntimeType.LLAMA_CPP,
    minDeviceMemoryInGb = minRamGb,
    taskTypes = listOf(BuiltInTaskId.LLM_CHAT, BuiltInTaskId.LLM_PROMPT_LAB),
    defaultConfig = DefaultConfig(
      topK = 20, topP = 0.95f, temperature = 0.6f, accelerators = "cpu",
      visionAccelerator = null, maxContextLength = 4096, maxTokens = 2048,
    ),
  )
}
