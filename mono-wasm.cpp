// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See the LICENSE.txt file in the project root
// for the license information.

#include <mach/mach_time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>

#include <string>
#include <vector>
#include <memory>

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include "lld/Common/Driver.h"

#define ERROR(...) \
    do { \
        fprintf(stderr, __VA_ARGS__); \
        exit(1); \
    } \
    while (0)

#define _PATH_CHECK(path, iftype, what, must_exist) \
    ({ \
        struct stat s; \
        bool exists = false; \
        if (stat(path, &s) != 0) { \
            if (must_exist) { \
                ERROR("path `%s' does not exist\n", path); \
            } \
        } \
        else { \
            if ((s.st_mode & S_IFMT) != iftype) { \
                ERROR("path `%s' is not a %s\n", path, what); \
            } \
            exists = true; \
        } \
        exists; \
    })

#define _FILE_CHECK(path, must_exist) \
    _PATH_CHECK(path, S_IFREG, "file", must_exist)
#define FILE_MUST_EXIST(path) _FILE_CHECK(path, true)
#define FILE_MAY_EXIST(path) _FILE_CHECK(path, false)

#define _DIR_CHECK(path, must_exist) \
    _PATH_CHECK(path, S_IFDIR, "directory", must_exist)
#define DIR_MUST_EXIST(path) _DIR_CHECK(path, true)
#define DIR_MAY_EXIST(path) _DIR_CHECK(path, false)

static int
timespec_cmp (struct timespec a, struct timespec b)
{
    return a.tv_sec < b.tv_sec
        ? -1
        : (a.tv_sec > b.tv_sec
                ? 1 : a.tv_nsec - b.tv_nsec);
}

#define FILE_IS_OLDER(source_path, dest_path) \
    ({ \
        struct stat source_s; \
        assert(stat(source_path, &source_s) == 0); \
        struct stat dest_s; \
        stat(dest_path, &dest_s) == 0 \
            ? timespec_cmp(source_s.st_mtimespec, dest_s.st_mtimespec) > 0 \
            : true; \
    })

static char libdir_path[PATH_MAX] = { 0 };
static char bindir_path[PATH_MAX] = { 0 };

static void
setup_paths(const char *arg0)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/../../lib", arg0);
    if (realpath(path, libdir_path) == NULL) {
        ERROR("can't resolve `lib' directory\n");
    }
    DIR_MUST_EXIST(libdir_path);

    snprintf(path, sizeof path, "%s/..", arg0);
    if (realpath(path, bindir_path) == NULL) {
        ERROR("can't resolve `bin' directory\n");
    }
    DIR_MUST_EXIST(bindir_path);
}

static void
diagnostic_handler(const llvm::DiagnosticInfo &DI, void *ctx)
{
    switch (DI.getSeverity()) {
        case llvm::DS_Error:
            llvm::errs() << "ERROR: ";
            break;
        case llvm::DS_Warning:
            llvm::errs() << "WARNING: ";
            break;
        default:
            break;
    }

    llvm::DiagnosticPrinterRawOStream DP(llvm::errs());
    DI.print(DP);
    llvm::errs() << '\n';
}

static void
assembly_link(std::vector<std::string> &assembly_paths,
        const char *output_path)
{
    auto dest_base = std::string(output_path) + "/";

    if (DIR_MAY_EXIST(output_path)) {
        bool need_link = false;
        for (auto assembly_path : assembly_paths) {
            auto linked_path = dest_base + assembly_path;
            if (FILE_IS_OLDER(assembly_path.c_str(), linked_path.c_str())) {
                need_link = true;
                break;
            }
        }
        if (!need_link) {
            goto skip_link;
        }
    }

    char cmd[PATH_MAX];
    snprintf(cmd, sizeof cmd, "monolinker -d %s -c link -l none -o %s",
            libdir_path, output_path);

    for (auto assembly_path : assembly_paths) {
        strlcat(cmd, " -a ", sizeof cmd);
        strlcat(cmd, assembly_path.c_str(), sizeof cmd);
    }

    if (system(cmd) != 0) {
        ERROR("monolinker pass failed (command was: %s)\n", cmd);
    }

skip_link:
    char *first_assembly = strdup(basename((char *)assembly_paths[0].c_str()));
    assert(first_assembly != NULL);
    assembly_paths.clear();
    DIR *dir = opendir(output_path);
    assert(dir != NULL);
    struct dirent *entry;
    int i = 0, first_assembly_i = -1;
    while ((entry = readdir(dir)) != NULL) {
        const char *s = entry->d_name;
        size_t sl = strlen(s);
        if (sl > 4) {
            const char *sp = s + sl - 4;
            if (strcmp(sp, ".exe") == 0 || strcmp(sp, ".dll") == 0) {
                auto linked_path = dest_base + s;
                assembly_paths.push_back(linked_path);
                if (strcmp(s, first_assembly) == 0) {
                    first_assembly_i = i;
                }
                i++;
            }
        }
    }
    closedir(dir);
    // The first assembly path must remain the same given to the command line.
    assert (first_assembly_i >= 0);
    if (first_assembly_i > 0) {
        std::iter_swap(assembly_paths.begin() + first_assembly_i,
                assembly_paths.begin());
    }
    free(first_assembly);
}

static std::string
assembly_compile(std::string assembly_path, const char *build_dir,
        std::string bitcode_path)
{
    static char monoc_path[PATH_MAX] = { '\0' };
    if (monoc_path[0] == '\0') {
        snprintf(monoc_path, sizeof monoc_path, "%s/monoc", bindir_path);
        FILE_MUST_EXIST(monoc_path);
    }

    if (FILE_IS_OLDER(assembly_path.c_str(), bitcode_path.c_str())) {
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof cmd,
                "MONO_PATH=\"%s\" MONO_ENABLE_COOP=1 " \
                "%s --aot=asmonly,llvmonly,static,llvm-outfile=%s %s " \
                ">& /dev/null",
                build_dir, monoc_path, bitcode_path.c_str(),
                assembly_path.c_str());

        if (system(cmd) != 0) {
            ERROR("bitcode compilation for `%s' failed " \
                    "(command was: %s)\n", assembly_path.c_str(), cmd);
        }
    }

    return bitcode_path;
}

static std::unique_ptr<llvm::Module>
bitcode_link(std::vector<std::string> &paths, llvm::LLVMContext &context)
{
    auto module = llvm::make_unique<llvm::Module>("index.bc", context);
    llvm::Linker linker(*module);

    for (auto path : paths) {
        llvm::SMDiagnostic err;
        auto file_module = llvm::parseIRFile(path, err, context);
        if (!file_module) {
            ERROR("bitcode parsing error: %s:%d: %s\n",
                    err.getFilename().str().c_str(), err.getLineNo(),
                    err.getMessage().str().c_str());
        }

        if (linker.linkInModule(std::move(file_module),
                    llvm::Linker::Flags::OverrideFromSrc)) {
            ERROR("linking %s failed\n", path.c_str());
        }
    }

    return module;
}

static llvm::Module *
aot_init_gen(std::vector<std::string> &assembly_paths, llvm::Module *module,
        llvm::LLVMContext &context)
{
    if (module == NULL) {
        module = new llvm::Module("aot_init.bc", context);
    }

    auto ptr_ty = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(context));

    auto register_f = module->getFunction("mono_aot_register_module");
    if (register_f == NULL) {
        std::vector<llvm::Type *> types;
        types.push_back(ptr_ty);
        auto c = module->getOrInsertFunction("mono_aot_register_module",
                llvm::FunctionType::get(llvm::Type::getVoidTy(context),
                    types, false));
        register_f = llvm::cast<llvm::Function>(c);
    }

    auto c = module->getOrInsertFunction("mono_wasm_aot_init",
            llvm::FunctionType::get(llvm::Type::getVoidTy(context), false));
    auto f = llvm::cast<llvm::Function>(c);
    auto bb = llvm::BasicBlock::Create(context, "entry", f);

    for (auto path : assembly_paths) {
        // /<build-dir>/foo.{exe,dll} -> mono_aot_module_foo_info
        assert(path.size() > 4);
        assert(path[path.size() - 4] == '.');
        size_t beg = path.rfind('/');
        assert(beg != std::string::npos);
        beg++;

        auto name = std::string("mono_aot_module_")
            + path.substr(beg, path.size() - beg - 4) + "_info";

        auto aot_info = module->getGlobalVariable(name.c_str());
        if (aot_info == NULL) {
            auto c = module->getOrInsertGlobal(name.c_str(), ptr_ty);
            aot_info = llvm::cast<llvm::GlobalVariable>(c);
        }

        llvm::CallInst::Create(register_f,
                new llvm::LoadInst(aot_info, "", bb), "", bb);
    }

    llvm::ReturnInst::Create(context, bb);

    return module;
}

static void
wasm_codegen(llvm::Module *module, llvm::CodeGenOpt::Level opt_level,
        llvm::LLVMContext &context, std::string wasm_path)
{
    static bool init_done = false;
    if (!init_done) {
        LLVMInitializeWebAssemblyTarget();
        LLVMInitializeWebAssemblyTargetMC();
        LLVMInitializeWebAssemblyTargetInfo();
        LLVMInitializeWebAssemblyAsmPrinter();
        init_done = true;
    }

    // Important to generate a proper wasm object file.
    module->setTargetTriple("wasm32-unknown-unknown-wasm");

    std::string err;
    auto triple = llvm::Triple(module->getTargetTriple());
    auto target = llvm::TargetRegistry::lookupTarget("wasm32", triple, err);
    if (target == NULL) {
        ERROR("can't lookup wasm32 target: %s\n", err.c_str());
    }

    std::string cpu_str = "";
    std::string features_str = "";
    llvm::TargetOptions options;
    options.MCOptions.AsmVerbose = false;

    auto target_machine = target->createTargetMachine(triple.getTriple(),
            cpu_str, features_str, options, llvm::None,
            llvm::CodeModel::Large, opt_level);

    if (target_machine == NULL) {
        ERROR("couldn't allocate target machine\n");
    }

    llvm::legacy::PassManager pm;
    pm.add(new llvm::TargetLibraryInfoWrapperPass(
                llvm::TargetLibraryInfoImpl(triple)));

    module->setDataLayout(target_machine->createDataLayout());

    std::error_code EC;
    llvm::raw_fd_ostream dest(wasm_path, EC, llvm::sys::fs::F_None);
    if (EC) {
        ERROR("error when opening file %s: %s\n", wasm_path.c_str(),
                EC.message().c_str());
    }

    if (target_machine->addPassesToEmitFile(pm, dest,
                llvm::TargetMachine::CGFT_ObjectFile)) {
        ERROR("target does not support assembly generation\n");
    }

    pm.run(*module);
    dest.flush();
}

static void
wasm_codegen2(std::string &bitcode_path, llvm::CodeGenOpt::Level opt,
        llvm::LLVMContext &context, std::string wasm_path)
{
    if (FILE_IS_OLDER(bitcode_path.c_str(), wasm_path.c_str())) {
        llvm::SMDiagnostic err;
        auto module = llvm::parseIRFile(bitcode_path, err, context);
        if (!module) {
            ERROR("parsing bitcode file `%s' failed: %s:%d: %s\n",
                    bitcode_path.c_str(), err.getFilename().str().c_str(),
                    err.getLineNo(), err.getMessage().str().c_str());
        }

        wasm_codegen(module.get(), opt, context, wasm_path);
    }
}

static void
wasm_link(std::vector<std::string> &paths, std::string output,
        bool strip_debug_info)
{
    std::vector<const char *> args;
    args.push_back("wasm-lld");
    for (auto path : paths) {
        args.push_back(strdup(path.c_str()));
    }
    args.push_back("-o");
    args.push_back(output.c_str());
    args.push_back("--allow-undefined");
    args.push_back("--no-entry");
    if (strip_debug_info) {
        args.push_back("--strip-debug");
    }

    if (!lld::wasm::link(args, false)) {
        ERROR("failed to link wasm files\n");
    }

    for (int i = 0; i < paths.size(); i++) {
        free((char *)args[i + 1]);
    }
}

static void
assembly_strip(std::vector<std::string> &paths, const char *output_path)
{
    for (auto path : paths) {
        const char *base = strrchr(path.c_str(), '/');
        assert(base != NULL);

        char cmd[PATH_MAX];
        snprintf(cmd, sizeof cmd, "mono-cil-strip %s %s%s >& /dev/null",
                path.c_str(), output_path, base);

        if (system(cmd) != 0) {
            ERROR("IL strip for `%s' failed (command was: %s)\n",
                    path.c_str(), cmd);
        }
    }
}

extern "C" {
    extern FILE *jsmin_in;
    extern FILE *jsmin_out;
    void jsmin(void);
}

static void
js_gen(std::vector<std::string> &assembly_paths, const char *output_path)
{
    auto index_js = std::string(libdir_path) + "/index.js";
    FILE_MUST_EXIST(index_js.c_str());

    auto output_index_js = std::string(output_path) + "/index.js";
    FILE *output = fopen(output_index_js.c_str(), "w+");
    if (output == NULL) {
        ERROR("can't open `%s': %s\n", output_index_js.c_str(),
                strerror(errno));
    }

    fprintf(output, "var files=[");
    for (auto path : assembly_paths) {
        const char *base = strrchr(path.c_str(), '/');
        assert(base != NULL);
        fprintf(output, "\"%s\",", base + 1);
    }
    fprintf(output, "];");

    jsmin_in = fopen(index_js.c_str(), "r");
    jsmin_out = output;

    jsmin();

    fclose(jsmin_in);
    fclose(output);

    jsmin_in = NULL;
    jsmin_out = NULL;
}

static std::string
swap_extension(std::string path, const char *new_extension)
{
    auto pos = path.rfind('.');
    if (pos != std::string::npos) {
        assert(pos > 0);
        path = path.substr(0, pos);
    }
    return path + new_extension;
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        ERROR("Usage: %s [options] <input files>\n\n" \
                "Options:\n" \
                "    -b <directory>        Specify build directory\n" \
                "                          (default is `./build')\n" \
                "    -o <directory>        Specify output directory\n" \
                "    -On                   Specify optimization level\n" \
                "                          (0, 1, 2, 3, default is 2)\n" \
                "    --strip-debug         Strip debugging information\n" \
                "    -v                    Verbose output\n" \
                "    -i                    Incremental build (experimental)\n",
                argv[0]);
    }

    const char *build_path = "./build";
    const char *output_path = NULL;
    llvm::CodeGenOpt::Level opt = llvm::CodeGenOpt::Default;
    bool strip_debug_info = false;
    bool verbose = false;
    bool incremental = false;
    std::vector<std::string> assembly_paths, bitcode_paths, wasm_paths;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            if (arg[1] == 'b' && arg[2] == '\0') {
                i++;
                if (i >= argc) {
                    ERROR("expected value for `-b' option\n");
                }
                build_path = argv[i];
            }
            else if (arg[1] == 'o' && arg[2] == '\0') {
                i++;
                if (i >= argc) {
                    ERROR("expected value for `-o' option\n");
                }
                output_path = argv[i];
            }
            else if (arg[1] == 'v' && arg[2] == '\0') {
                verbose = true;
            }
            else if (arg[1] == 'i' && arg[2] == '\0') {
                incremental = true;
            }
            else if (arg[1] == 'O' && arg[3] == '\0') {
                switch (arg[2]) {
                    case '0':
                        opt = llvm::CodeGenOpt::None;
                        break;
                    case '1':
                        opt = llvm::CodeGenOpt::Less;
                        break;
                    case '2':
                        opt = llvm::CodeGenOpt::Default;
                        break;
                    case '3':
                        opt = llvm::CodeGenOpt::Aggressive;
                        break;
                    default:
                        ERROR("malformed `-On' option\n");
                }
            }
            else if (strcmp(arg, "--strip-debug") == 0) {
                strip_debug_info = true;
            }
            else {
                ERROR("invalid `%s' option\n", arg);
            }
        }
        else {
            assembly_paths.push_back(arg);
        }
    }
    if (output_path == NULL) {
        ERROR("`-o' option required\n");
    }
    if (assembly_paths.size() == 0) {
        ERROR("at least one input file is required\n");
    }

    setup_paths(argv[0]);

    if (!DIR_MAY_EXIST(output_path)) {
        if (mkdir(output_path, 0755) != 0) {
            ERROR("can't create output directory `%s': %s\n",
                    output_path, strerror(errno));
        }
    }

    auto output_wasm = std::string(output_path) + "/index.wasm";

    llvm::LLVMContext context;
    context.setDiagnosticHandlerCallBack(diagnostic_handler, NULL, true);

    uint64_t start = 0, total = 0, delta = 0;
    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);

#define T_MEASURE(what, code) \
    start = mach_absolute_time(); \
    if (verbose) { \
        std::string _what = what; \
        printf("%s ... ", _what.c_str()); \
    } \
    code; \
    delta = mach_absolute_time() - start; \
    total += delta; \
    if (verbose) { \
        printf("%.3fs\n", (((double)(delta) * timebase_info.numer) \
                    / (timebase_info.denom * 1000000000))); \
    }

    T_MEASURE("IL link", assembly_link(assembly_paths, build_path));

    bitcode_paths.push_back(std::string(libdir_path) + "/runtime.bc");
    for (auto assembly_path : assembly_paths) {
        auto bitcode_path = swap_extension(assembly_path, ".bc");
        T_MEASURE(std::string("IL/IR compile ") + assembly_path,
                assembly_compile(assembly_path, build_path, bitcode_path));
        bitcode_paths.push_back(bitcode_path);
    }

    if (incremental) {
        for (auto bitcode_path : bitcode_paths) {
            auto wasm_path = swap_extension(bitcode_path, ".wasm");
            T_MEASURE(std::string("IR/WASM codegen ")
                    + bitcode_path.c_str(),
                    wasm_codegen2(bitcode_path, opt, context, wasm_path));
            wasm_paths.push_back(wasm_path);
        }

        auto path = std::string(build_path) + "/aot_init.wasm";
        auto aot_init_mod = aot_init_gen(assembly_paths, NULL, context);
        wasm_codegen(aot_init_mod, opt, context, path);
        wasm_paths.push_back(path);
        delete aot_init_mod;
    }
    else {
        T_MEASURE("IR link",
                auto module = bitcode_link(bitcode_paths, context));

        aot_init_gen(assembly_paths, module.get(), context);

        auto path = std::string(build_path) + "/index.wasm";
        T_MEASURE("IR/WASM codegen",
                wasm_codegen(module.get(), opt, context, path));
        wasm_paths.push_back(path);
    }

    T_MEASURE("WASM link",
            wasm_link(wasm_paths, output_wasm, strip_debug_info));

    T_MEASURE("IL strip", assembly_strip(assembly_paths, output_path));

    T_MEASURE("JS gen", js_gen(assembly_paths, output_path));

#undef T_MEASURE

    return 0;
}
