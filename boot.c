// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See the LICENSE.txt file in the project root
// for the license information.

#include <mono/mini/mini.h>
#include <mono/metadata/assembly.h>
#include <locale.h>

void mono_wasm_aot_init(void);

int
mono_wasm_main(char *main_assembly_name, int debug)
{
    g_log("mono-wasm", G_LOG_LEVEL_INFO, "booting main()");

    setlocale(LC_ALL, "");

    g_setenv("LANG", "en_US", 1);
    g_setenv("MONO_PATH", ".", 1);
    g_setenv("MONO_LOG_LEVEL", debug ? "debug" : "error", 1);

    g_log_set_always_fatal(G_LOG_LEVEL_ERROR);
    g_log_set_fatal_mask(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR);

    g_set_prgname("hello");

    mono_wasm_aot_init();

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "initializing mono runtime");
    mono_jit_set_aot_mode(MONO_AOT_MODE_LLVMONLY);
    MonoDomain *domain = mono_jit_init_version("hello", "v4.0.30319");

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "opening main assembly `%s'",
            main_assembly_name);
    MonoAssembly *assembly = mono_assembly_open(main_assembly_name, NULL);
    g_assert(assembly != NULL);

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "running Main()");
    int mono_argc = 1;
    char *mono_argv[] = { main_assembly_name, NULL };
    int ret = mono_jit_exec(domain, assembly, mono_argc, mono_argv);

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "terminating mono runtime");
    //mono_jit_cleanup(domain);

    return ret;
}
