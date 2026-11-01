// SPDX-License-Identifier: Apache-2.0
// Build preflight: use Android's actual loader without Termux's C++ libraries.
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("Expected the native library path\n", stderr);
        return 2;
    }
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "Android native loader: %s\n", dlerror());
        return 1;
    }
    dlclose(handle);
    puts("Android native loader: OK");
    return 0;
}
