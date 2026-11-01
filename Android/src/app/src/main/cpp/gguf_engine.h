// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llama.h"
#include "chat.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gallery {
struct ChatMessage {
    std::string role;
    std::string content;
};

// Operations other than cancel() are serialized by the caller.
class GgufEngine {
public:
    // gpu_layers: 0 keeps everything on the CPU, -1 offloads all layers.
    GgufEngine(const std::string & path, int context_size, int threads, int gpu_layers);
    ~GgufEngine();
    GgufEngine(const GgufEngine &) = delete;
    GgufEngine & operator=(const GgufEngine &) = delete;
    void prepare() { cancelled_.store(false); }
    void cancel() { cancelled_.store(true); }
    bool cancelled() const { return cancelled_.load(); }
    int count(const std::string & text) const;
    bool supports_thinking() const { return supports_thinking_; }
    int count_input(const std::vector<ChatMessage> & messages, bool thinking = false) const;
    std::string generate(const std::vector<ChatMessage> & messages, int max_tokens,
                         int top_k, float top_p, float temperature,
                         const std::function<void(const std::string &)> & on_piece,
                         bool thinking = false,
                         const std::function<void(const std::string &)> & on_thinking = {});

private:
    std::vector<llama_token> tokenize(const std::string & text, bool special) const;
    common_chat_params prompt(const std::vector<ChatMessage> & messages, bool thinking) const;
    bool supports_thinking_ = false;
    std::atomic<bool> cancelled_{false};
    llama_model * model_ = nullptr;
    llama_context * context_ = nullptr;
    const llama_vocab * vocab_ = nullptr;
    common_chat_templates_ptr templates_;
};
}
