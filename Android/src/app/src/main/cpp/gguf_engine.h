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
    GgufEngine(const std::string & path, int context_size, int threads);
    ~GgufEngine();
    GgufEngine(const GgufEngine &) = delete;
    GgufEngine & operator=(const GgufEngine &) = delete;
    void prepare() { cancelled_.store(false); }
    void cancel() { cancelled_.store(true); }
    bool cancelled() const { return cancelled_.load(); }
    int count(const std::string & text) const;
    int count_input(const std::vector<ChatMessage> & messages) const;
    std::string generate(const std::vector<ChatMessage> & messages, int max_tokens,
                         int top_k, float top_p, float temperature,
                         const std::function<void(const std::string &)> & on_piece);

private:
    std::vector<llama_token> tokenize(const std::string & text, bool special) const;
    std::string prompt(const std::vector<ChatMessage> & messages) const;
    std::atomic<bool> cancelled_{false};
    llama_model * model_ = nullptr;
    llama_context * context_ = nullptr;
    const llama_vocab * vocab_ = nullptr;
    common_chat_templates_ptr templates_;
};
}
