// SPDX-License-Identifier: Apache-2.0
#include "gguf_engine.h"
#include "json.h"
#include <jni.h>
#include <map>
#include <mutex>
#include <stdexcept>

namespace {
std::mutex handles_mutex;
std::map<jlong, std::shared_ptr<gallery::GgufEngine>> handles;
jlong next_handle = 1;

std::shared_ptr<gallery::GgufEngine> get(jlong handle) {
    std::lock_guard<std::mutex> lock(handles_mutex);
    auto it = handles.find(handle);
    if (it == handles.end()) throw std::runtime_error("GGUF model is closed");
    return it->second;
}

std::string bytes(JNIEnv * env, jbyteArray value) {
    if (!value) throw std::runtime_error("Missing GGUF argument");
    std::string result(env->GetArrayLength(value), '\0');
    env->GetByteArrayRegion(value, 0, result.size(), reinterpret_cast<jbyte *>(result.data()));
    return result;
}

std::vector<gallery::ChatMessage> messages(JNIEnv * env, jbyteArray value) {
    const auto json = common_json::parse(bytes(env, value));
    std::vector<gallery::ChatMessage> result;
    for (const auto & item : json) {
        result.push_back({item.at("role").get<std::string>(), item.at("content").get<std::string>()});
    }
    return result;
}

void fail(JNIEnv * env, const std::exception & e) {
    if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), e.what());
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_open(JNIEnv * env, jobject, jbyteArray path, jint context, jint threads) {
    try {
        auto engine = std::make_shared<gallery::GgufEngine>(bytes(env, path), context, threads);
        std::lock_guard<std::mutex> lock(handles_mutex);
        const auto handle = next_handle++;
        handles.emplace(handle, std::move(engine));
        return handle;
    } catch (const std::exception & e) { fail(env, e); return 0; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_close(JNIEnv *, jobject, jlong handle) {
    std::shared_ptr<gallery::GgufEngine> engine;
    {
        std::lock_guard<std::mutex> lock(handles_mutex);
        auto it = handles.find(handle);
        if (it == handles.end()) return;
        engine = std::move(it->second);
        handles.erase(it);
    }
    engine->cancel();
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_prepare(JNIEnv * env, jobject, jlong handle) {
    try { get(handle)->prepare(); } catch (const std::exception & e) { fail(env, e); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_cancel(JNIEnv * env, jobject, jlong handle) {
    try { get(handle)->cancel(); } catch (const std::exception & e) { fail(env, e); }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_count(JNIEnv * env, jobject, jlong handle, jbyteArray text, jboolean formatted) {
    try {
        auto engine = get(handle);
        return formatted ? engine->count_input(messages(env, text)) : engine->count(bytes(env, text));
    } catch (const std::exception & e) { fail(env, e); return 0; }
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_ai_edge_gallery_runtime_gguf_GgufNative_generate(JNIEnv * env, jobject, jlong handle, jbyteArray history,
                                                            jint max_tokens, jint top_k, jfloat top_p, jfloat temperature,
                                                            jobject callback) {
    try {
        auto engine = get(handle);
        auto callback_class = env->GetObjectClass(callback);
        auto method = env->GetMethodID(callback_class, "onToken", "([B)V");
        env->DeleteLocalRef(callback_class);
        if (!method) return;
        engine->generate(messages(env, history), max_tokens, top_k, top_p, temperature,
            [&](const std::string & piece) {
                if (env->ExceptionCheck()) { engine->cancel(); return; }
                auto array = env->NewByteArray(piece.size());
                if (!array) { engine->cancel(); return; }
                env->SetByteArrayRegion(array, 0, piece.size(), reinterpret_cast<const jbyte *>(piece.data()));
                env->CallVoidMethod(callback, method, array);
                env->DeleteLocalRef(array);
                if (env->ExceptionCheck()) engine->cancel();
            });
    } catch (const std::exception & e) { fail(env, e); }
}
