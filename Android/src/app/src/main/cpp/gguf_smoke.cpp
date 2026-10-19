// SPDX-License-Identifier: Apache-2.0
#include "gguf_engine.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char ** argv) {
    if (argc < 2) {
        std::cerr << "Usage: gallery-gguf-smoke model.gguf [prompt]\n";
        return 2;
    }
    try {
        gallery::GgufEngine engine(argv[1], 1024, 4);
        std::vector<gallery::ChatMessage> history = {{"user", argc > 2 ? argv[2] : "Reply with just the number: what is 2 + 2?"}};
        if (engine.count_input(history) <= 0) throw std::runtime_error("No prompt tokens");
        const auto response = engine.generate(history, 64, 40, 0.95f, 0, [](const std::string & p) { std::cout << p << std::flush; });
        if (response.empty()) throw std::runtime_error("No response");
        history.push_back({"assistant", response});
        history.push_back({"user", "What number did you just give?"});
        std::cout << "\n--- second turn ---\n";
        const auto second = engine.generate(history, 32, 40, 0.95f, 0, [](const std::string & p) { std::cout << p << std::flush; });
        if (second.empty()) throw std::runtime_error("No second response");
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
