#include <mono/mini/mini.h>
#include <mono/metadata/assembly.h>
#include <locale.h>

int
main(int argc, char **argv)
{
    printf("Starting mono runtime\n");

    setlocale(LC_ALL, "");

    g_setenv("LANG", "en_US", 1);
    g_setenv("MONO_PATH", ".", 1);
    g_setenv("MONO_LOG_LEVEL", "debug", 1);

    g_log_set_always_fatal(G_LOG_LEVEL_ERROR);
    g_log_set_fatal_mask(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR);

    g_set_prgname("hello");

    extern void *mono_aot_module_mscorlib_info;
    extern void *mono_aot_module_hello_info;
    mono_aot_register_module(mono_aot_module_mscorlib_info);
    mono_aot_register_module(mono_aot_module_hello_info);

    mono_jit_set_aot_mode(MONO_AOT_MODE_LLVMONLY);
    MonoDomain *domain = mono_jit_init_version("hello", "v4.0.30319");

    printf("Opening hello.dll\n");
    MonoAssembly *assembly = mono_assembly_open("hello.dll", NULL);
    g_assert(assembly != NULL);

    printf("Running main()\n");
    int mono_argc = 1;
    char *mono_argv[] = { "/hello", NULL };
    mono_jit_exec(domain, assembly, mono_argc, mono_argv);

    printf("Terminating mono runtime\n");
    mono_jit_cleanup(domain);

    return 0;
}
