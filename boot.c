#include <mono/mini/mini.h>
#include <mono/metadata/assembly.h>
#include <locale.h>

int
main(int argc, char **argv)
{
    g_log("mono-wasm", G_LOG_LEVEL_INFO, "booting main()");

    setlocale(LC_ALL, "");

    g_setenv("LANG", "en_US", 1);
    g_setenv("MONO_PATH", ".", 1);
    g_setenv("MONO_LOG_LEVEL", "error"/*"debug"*/, 1);

    g_log_set_always_fatal(G_LOG_LEVEL_ERROR);
    g_log_set_fatal_mask(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR);

    g_set_prgname("hello");

    extern void *mono_aot_module_mscorlib_info;
    extern void *mono_aot_module_hello_info;
    mono_aot_register_module(mono_aot_module_mscorlib_info);
    mono_aot_register_module(mono_aot_module_hello_info);

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "initializing mono runtime");
    mono_jit_set_aot_mode(MONO_AOT_MODE_LLVMONLY);
    MonoDomain *domain = mono_jit_init_version("hello", "v4.0.30319");

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "opening hello.dll");
    MonoAssembly *assembly = mono_assembly_open("hello.dll", NULL);
    g_assert(assembly != NULL);

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "running Hello.Main()");
    int mono_argc = 1;
    char *mono_argv[] = { "/hello", NULL };
    int ret = mono_jit_exec(domain, assembly, mono_argc, mono_argv);

    g_log("mono-wasm", G_LOG_LEVEL_INFO, "terminating mono runtime");
    //mono_jit_cleanup(domain);

    return ret;
}
