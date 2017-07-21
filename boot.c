#include <glib.h>
#include <mono/mini/jit.h>
//#include <mono/metadata/assembly.h>

int
main(int argc, char **argv)
{
    printf("Starting mono runtime\n");

    g_log_set_always_fatal(G_LOG_LEVEL_ERROR);
    g_log_set_fatal_mask(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR);

    //extern void *mono_aot_module_hello_info;
    //extern void *mono_aot_module_mscorlib_info;
    //mono_aot_register_module(mono_aot_module_hello_info);
    //mono_aot_register_module(mono_aot_module_mscorlib_info);

    mono_jit_set_aot_mode(MONO_AOT_MODE_LLVMONLY);
    mono_jit_init_version("hello", "v4.0.30319");

    //MonoAssembly *assembly = mono_assembly_open("hello.exe", NULL);
    //g_assert(assembly != NULL);
    //mono_jit_exec(mono_domain_get(), assembly, argc, argv);

    printf("Terminating mono runtime\n");
    return 0;
}
