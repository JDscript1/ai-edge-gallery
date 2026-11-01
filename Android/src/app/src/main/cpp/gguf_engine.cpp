// SPDX-License-Identifier: Apache-2.0
#include "gguf_engine.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace gallery {
GgufEngine::GgufEngine(const std::string & path, int context_size, int threads, int gpu_layers) {
    static std::once_flag initialized;
    std::call_once(initialized, [] { llama_backend_init(); });
    if (context_size < 256 || context_size > 16384) {
        throw std::runtime_error("GGUF context must be between 256 and 16384 tokens");
    }
    auto mp = llama_model_default_params();
    mp.n_gpu_layers = gpu_layers;
    mp.load_mode = LLAMA_LOAD_MODE_MMAP;
    model_ = llama_model_load_from_file(path.c_str(), mp);
    if (!model_) throw std::runtime_error("Cannot load this GGUF. Check the model architecture, quantization and available RAM.");
    try {
        vocab_ = llama_model_get_vocab(model_);
        auto cp = llama_context_default_params();
        cp.n_ctx = context_size;
        cp.n_batch = 128;
        cp.n_ubatch = 64;
        cp.n_threads = std::clamp(threads, 1, 8);
        cp.n_threads_batch = cp.n_threads;
        cp.offload_kqv = gpu_layers != 0;
        cp.op_offload = gpu_layers != 0;
        if (gpu_layers == 0) {
            // Q8_0 keeps the KV cache near half its f16 size. llama.cpp switches CPU
            // flash attention on by itself, as a quantized V cache requires it.
            cp.type_k = GGML_TYPE_Q8_0;
            cp.type_v = GGML_TYPE_Q8_0;
        } else {
            // Adreno's Vulkan driver cannot compile the scalar flash-attention pipeline.
            cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_DISABLED;
        }
        cp.abort_callback = [](void * data) { return static_cast<GgufEngine *>(data)->cancelled(); };
        cp.abort_callback_data = this;
        context_ = llama_init_from_model(model_, cp);
        if (!context_) throw std::runtime_error("Not enough memory to create the GGUF context. Try a smaller model or context.");
        if (!llama_model_chat_template(model_, nullptr)) {
            throw std::runtime_error("This GGUF has no chat template. Import an instruction/chat model with its tokenizer.chat_template metadata.");
        }
        templates_ = common_chat_templates_init(model_, "");
        supports_thinking_ = common_chat_templates_support_enable_thinking(templates_.get());
    } catch (...) {
        if (context_) llama_free(context_);
        llama_model_free(model_);
        throw;
    }
}

GgufEngine::~GgufEngine() {
    if (context_) llama_free(context_);
    if (model_) llama_model_free(model_);
}

std::vector<llama_token> GgufEngine::tokenize(const std::string & text, bool special) const {
    if (text.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error("Prompt is too large");
    }
    int n = llama_tokenize(vocab_, text.data(), text.size(), nullptr, 0, special, true);
    if (n == INT32_MIN) throw std::runtime_error("Tokenization overflow");
    std::vector<llama_token> tokens(static_cast<size_t>(n < 0 ? -n : n));
    n = llama_tokenize(vocab_, text.data(), text.size(), tokens.data(), tokens.size(), special, true);
    if (n < 0) throw std::runtime_error("Cannot tokenize the prompt");
    tokens.resize(n);
    return tokens;
}

common_chat_params GgufEngine::prompt(const std::vector<ChatMessage> & messages, bool thinking) const {
    common_chat_templates_inputs inputs;
    inputs.enable_thinking = thinking && supports_thinking_;
    inputs.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
    inputs.add_generation_prompt = true;
    for (const auto & message : messages) {
        common_chat_msg msg;
        msg.role = message.role;
        msg.content = message.content;
        inputs.messages.push_back(std::move(msg));
    }
    return common_chat_templates_apply(templates_.get(), inputs);
}

int GgufEngine::count(const std::string & text) const { return tokenize(text, false).size(); }
int GgufEngine::count_input(const std::vector<ChatMessage> & messages, bool thinking) const {
    return tokenize(prompt(messages, thinking).prompt, true).size();
}

std::string GgufEngine::generate(const std::vector<ChatMessage> & messages, int max_tokens,
                               int top_k, float top_p, float temperature,
                               const std::function<void(const std::string &)> & on_piece,
                               bool thinking,
                               const std::function<void(const std::string &)> & on_thinking) {
    const auto chat = prompt(messages, thinking);
    const auto tokens = tokenize(chat.prompt, true);
    common_chat_parser_params parser(chat);
    parser.reasoning_format = COMMON_REASONING_FORMAT_DEEPSEEK;
    parser.parse_tool_calls = false;
    const bool split_channels = on_thinking && !chat.parser.empty();
    if (split_channels) parser.parser.load(chat.parser);
    common_chat_msg previous;
    if (tokens.empty()) throw std::runtime_error("The chat template produced an empty prompt");
    const int capacity = llama_n_ctx(context_);
    if (tokens.size() >= static_cast<size_t>(capacity)) {
        throw std::runtime_error("Conversation exceeds the GGUF context. Start a new chat or reduce the prompt.");
    }
    const int limit = std::min(std::max(1, max_tokens), capacity - static_cast<int>(tokens.size()));
    llama_memory_clear(llama_get_memory(context_), true);
    for (size_t offset = 0; offset < tokens.size() && !cancelled(); offset += 128) {
        const int n = std::min<size_t>(128, tokens.size() - offset);
        auto batch = llama_batch_get_one(const_cast<llama_token *>(tokens.data() + offset), n);
        if (llama_decode(context_, batch) != 0 && !cancelled()) {
            throw std::runtime_error("GGUF prompt processing failed");
        }
    }
    using Sampler = std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)>;
    Sampler sampler(llama_sampler_chain_init(llama_sampler_chain_default_params()), llama_sampler_free);
    if (temperature <= 0) {
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(std::max(1, top_k)));
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(std::clamp(top_p, 0.0f, 1.0f), 1));
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(temperature));
        llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }
    std::string result;
    const auto emit = [&](bool partial) {
        const auto message = common_chat_parse(result, partial, parser);
        if (message.empty()) return;
        for (const auto & delta : common_chat_msg_diff::compute_diffs(previous, message)) {
            if (!delta.reasoning_content_delta.empty()) on_thinking(delta.reasoning_content_delta);
            if (!delta.content_delta.empty()) on_piece(delta.content_delta);
        }
        previous = message;
    };
    for (int i = 0; i < limit && !cancelled(); ++i) {
        llama_token token = llama_sampler_sample(sampler.get(), context_, -1);
        if (llama_vocab_is_eog(vocab_, token)) break;
        std::string piece(256, '\0');
        int n = llama_token_to_piece(vocab_, token, piece.data(), piece.size(), 0, false);
        if (n < 0) {
            piece.resize(-n);
            n = llama_token_to_piece(vocab_, token, piece.data(), piece.size(), 0, false);
        }
        if (n < 0) throw std::runtime_error("Cannot decode a generated token");
        piece.resize(n);
        result += piece;
        if (split_channels) emit(true);
        else on_piece(piece);
        if (cancelled() || i + 1 == limit) break;
        auto batch = llama_batch_get_one(&token, 1);
        if (llama_decode(context_, batch) != 0 && !cancelled()) {
            throw std::runtime_error("GGUF token generation failed");
        }
    }
    if (split_channels) emit(false);
    return result;
}
}
