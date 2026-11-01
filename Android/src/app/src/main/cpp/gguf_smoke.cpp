// SPDX-License-Identifier: Apache-2.0
#include "gguf_engine.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

static long elapsed_ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - since).count();
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        std::cerr << "Usage: gallery-gguf-smoke model.gguf [prompt|@file] [gpu_layers]"
                     " [context] [max_out] [verify]\n";
        return 2;
    }
    try {
        std::string prompt = argc > 2 ? argv[2] : "Reply with just the number: what is 2 + 2?";
        if (!prompt.empty() && prompt[0] == '@') {
            std::ifstream file(prompt.substr(1), std::ios::binary);
            if (!file) throw std::runtime_error("Cannot open the prompt file");
            std::ostringstream text;
            text << file.rdbuf();
            prompt = text.str();
        }
        const int gpu_layers = argc > 3 ? std::atoi(argv[3]) : 0;
        const int context = argc > 4 ? std::atoi(argv[4]) : 1024;
        const int max_out = argc > 5 ? std::atoi(argv[5]) : 64;
        const bool verify = argc > 6 ? std::atoi(argv[6]) != 0 : true;
        auto phase = std::chrono::steady_clock::now();
        gallery::GgufEngine engine(argv[1], context, 4, gpu_layers);
        std::cerr << "timing: load_ms=" << elapsed_ms(phase) << "\n";
        std::vector<gallery::ChatMessage> history = {{"user", prompt}};
        const int prompt_tokens = engine.count_input(history);
        if (prompt_tokens <= 0) throw std::runtime_error("No prompt tokens");
        std::cerr << "prompt_tokens=" << prompt_tokens << "\n";
        if (max_out < 1) {
            std::cout << "GGUF load with " << context << " tokens of context passed.\n";
            return 0;
        }
        phase = std::chrono::steady_clock::now();
        int streamed1 = 0;
        const auto response = engine.generate(history, max_out, 40, 0.95f, 0, [&](const std::string & p) {
            ++streamed1;
            std::cout << p << std::flush;
        });
        std::cerr << "\ntiming: turn1_ms=" << elapsed_ms(phase) << " pieces=" << streamed1 << "\n";
        if (response.empty()) throw std::runtime_error("No response");
        history.push_back({"assistant", response});
        history.push_back({"user", "What number did you just give?"});
        std::cout << "\n--- second turn ---\n";
        phase = std::chrono::steady_clock::now();
        const auto second = engine.generate(history, max_out / 2 > 0 ? max_out / 2 : 1, 40, 0.95f, 0, [](const std::string & p) { std::cout << p << std::flush; });
        std::cerr << "\ntiming: turn2_ms=" << elapsed_ms(phase) << "\n";
        if (second.empty()) throw std::runtime_error("No second response");
        if (!verify) {
            std::cout << "\nGGUF load and two turns passed.\n";
            return 0;
        }
        engine.cancel();
        bool emitted_after_cancel = false;
        engine.generate(history, 32, 40, 0.95f, 0, [&](const std::string &) { emitted_after_cancel = true; });
        if (emitted_after_cancel) throw std::runtime_error("Cancellation failed");
        engine.prepare();
        int streamed = 0;
        engine.generate(history, 32, 40, 0.95f, 0, [&](const std::string &) {
            ++streamed;
            engine.cancel();
        });
        if (streamed != 1) throw std::runtime_error("Streaming cancellation failed");
        std::cout << "\nGGUF load, template, tokenizer, two turns and cancellation passed.\n";
        return 0;
    } catch (const std::exception & e) {
        std::cerr << "\n" << e.what() << "\n";
        return 1;
    }
}
