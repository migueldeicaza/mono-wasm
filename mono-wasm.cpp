// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See the LICENSE.txt file in the project root
// for the license information.

#include <mach/mach_time.h>
#include <sys/stat.h>
#include <dirent.h>

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
#include "llvm/Target/TargetSubtargetInfo.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include "s2wasm.h"
#include "wasm-linker.h"
#include "wasm-binary.h"
#include "support/file.h"

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
    assembly_paths.clear();
    DIR *dir = opendir(output_path);
    assert(dir != NULL);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *s = entry->d_name;
        size_t sl = strlen(s);
        if (sl > 4) {
            const char *sp = s + sl - 4;
            if (strcmp(sp, ".exe") == 0 || strcmp(sp, ".dll") == 0) {
                auto linked_path = dest_base + s;
                assembly_paths.push_back(linked_path); 
            }
        }
    }
    closedir(dir);
}

static void
assembly_compile(std::vector<std::string> &assembly_paths,
        std::vector<std::string> &bitcode_paths, const char *build_dir)
{
    assert(assembly_paths.size() > 0);
    assert(bitcode_paths.size() == 0);

    char monoc_path[PATH_MAX];
    snprintf(monoc_path, sizeof monoc_path, "%s/monoc", bindir_path);
    FILE_MUST_EXIST(monoc_path);

    for (auto assembly_path : assembly_paths) {
        std::string bitcode_path = assembly_path + ".bc";

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

        bitcode_paths.push_back(bitcode_path);
    }

    bitcode_paths.push_back(std::string(libdir_path) + "/runtime.bc");
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

static void
aot_init_gen(std::vector<std::string> &assembly_paths, llvm::Module *module,
        llvm::LLVMContext &context)
{
    auto register_f = module->getFunction("mono_aot_register_module");
    assert(register_f != NULL);

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
            ERROR("can't find aot module info variable `%s' " \
                    "for assembly `%s'\n", name.c_str(), path.c_str());
        }

        llvm::CallInst::Create(register_f,
                new llvm::LoadInst(aot_info, "", bb), "", bb);
    }

    llvm::ReturnInst::Create(context, bb);
}

class malloc_ostream : public llvm::raw_pwrite_stream {
    char *memory;
    size_t cap;
    uint64_t pos;

    void write_impl(const char *ptr, size_t size) override {
        if (pos + size > cap) {
            size_t new_cap = pos + size + (cap / 2);
            memory = (char *)realloc(memory, new_cap);
            assert(memory != NULL);
            cap = new_cap;
        }
        memcpy(memory + pos, ptr, size);
        pos += size;
    }

    void pwrite_impl(const char *ptr, size_t size, uint64_t offset) override {
        memcpy(memory + offset, ptr, size);
    }

    uint64_t current_pos() const override {
        return pos;
    }

public:
    malloc_ostream(size_t size) {
        assert(size > 0);
        memory = (char *)malloc(size);
        assert(memory != NULL);
        pos = 0;
        cap = size;
        SetUnbuffered();
    }

    char *buffer() { 
        return memory;
    }

    size_t size() {
        return pos;
    }
};

static const char *
wasm_assembly(llvm::Module *module, llvm::CodeGenOpt::Level opt_level,
        llvm::LLVMContext &context)
{
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyAsmPrinter();

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

    malloc_ostream stream(1024 * 1000 * 100);

    if (target_machine->addPassesToEmitFile(pm, stream,
                llvm::TargetMachine::CGFT_AssemblyFile)) {
        ERROR("target does not support assembly generation\n");
    }

    pm.run(*module);

    return stream.buffer();
}

static std::unique_ptr<wasm::Linker>
wasm_link(const char *text, size_t stack_size)
{
    wasm::S2WasmBuilder builder(text, false);

    auto linker = std::make_unique<wasm::Linker>(0, stack_size, 0, 0, false,
            false, "", false);
    linker->linkObject(builder);
    linker->layout();

    return linker;
}

static void
wasm_write(wasm::Module &wasm, bool debug_names, const char *output_path)
{
    wasm::BufferWithRandomAccess buffer(false);

    wasm::WasmBinaryWriter writer(&wasm, buffer, false);
    writer.setNamesSection(debug_names);
    writer.write();

    auto path = std::string(output_path) + "/index.wasm";
    wasm::Output output(path, wasm::Flags::Binary, wasm::Flags::Release);
    buffer.writeTo(output);
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
js_gen(wasm::Module &wasm, std::vector<std::string> &assembly_paths,
        const char *output_path)
{
    std::vector<std::string> missing_functions, missing_globals;
    for (auto &import : wasm.imports) {
        const char *name = import->name.c_str();
        switch (import->kind) {
            case wasm::ExternalKind::Function:
                missing_functions.push_back(name);
                break;

            case wasm::ExternalKind::Global:
                missing_globals.push_back(name);
                break;

            default:
                break;
        }
    }

    auto index_js = std::string(libdir_path) + "/index.js";
    FILE_MUST_EXIST(index_js.c_str());

    auto output_index_js = std::string(output_path) + "/index.js";
    FILE *output = fopen(output_index_js.c_str(), "w+");
    if (output == NULL) {
        ERROR("can't open `%s': %s\n", output_index_js.c_str(),
                strerror(errno));
    }

    fprintf(output, "var missing_functions=[");
    for (auto name : missing_functions) {
        fprintf(output, "\"%s\",", name.c_str());
    }

    fprintf(output, "];var missing_globals=[");
    for (auto name : missing_globals) {
        fprintf(output, "\"%s\",", name.c_str());
    }

    fprintf(output, "];var files=[");
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

int
main(int argc, char **argv)
{
    if (argc < 2) {
        ERROR("Usage: %s [options] <input files>\n\n" \
                "Options:\n" \
                "    -b <directory>        - Specify build directory\n" \
                "                            (default is `./build')\n" \
                "    -o <directory>        - Specify output directory\n" \
                "    -On                   - Specify optimization level\n" \
                "                            (0, 1, 2, 3, default is 2)\n" \
                "    -g                    - Emit debug information\n" \
                "    -s <size>             - Specify stack size in bytes\n" \
                "                            (default is 2M)\n" \
                "    -v                    - Verbose output\n",
                argv[0]);
    }

    const char *build_path = "./build";
    const char *output_path = NULL;
    llvm::CodeGenOpt::Level opt = llvm::CodeGenOpt::Default;
    bool emit_debug = false;
    bool verbose = false;
    size_t stack_size = 1024 * 1000 * 2;
    std::vector<std::string> assembly_paths, bitcode_paths;
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
            else if (arg[1] == 's' && arg[2] == '\0') {
                i++;
                if (i >= argc) {
                    ERROR("expected value for `-s' option\n");
                }
                stack_size = atoi(argv[i]);
                if (stack_size <= 0) {
                    ERROR("`-s' value must be greater than zero\n");
                }
            }
            else if (arg[1] == 'g' && arg[2] == '\0') {
                emit_debug = true;
            }
            else if (arg[1] == 'v' && arg[2] == '\0') {
                verbose = true;
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

    llvm::LLVMContext context;
    context.setDiagnosticHandler(diagnostic_handler, NULL, true);

    uint64_t start = 0, total = 0, delta = 0;
    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);

#define T_PRINT(what, delta) \
    if (verbose) { \
        printf("%15s : %.3fs\n", what, \
                (((double)(delta) * timebase_info.numer) \
                 / (timebase_info.denom * 1000000000))); \
    }

#define T_MEASURE(what, code) \
    start = mach_absolute_time(); \
    code; \
    delta = mach_absolute_time() - start; \
    total += delta; \
    T_PRINT(what, delta)

    T_MEASURE("IL link", assembly_link(assembly_paths, build_path));

    T_MEASURE("IL compile",
            assembly_compile(assembly_paths, bitcode_paths, build_path));

    T_MEASURE("bitcode link",
            auto module = bitcode_link(bitcode_paths, context));

    aot_init_gen(assembly_paths, module.get(), context);

    T_MEASURE("wasm assembly",
            auto text = wasm_assembly(module.get(), opt, context));

    T_MEASURE("wasm link", auto linker = wasm_link(text, stack_size));

    T_MEASURE("wasm write",
            wasm_write(linker.get()->getOutput().wasm, emit_debug,
                output_path));

    T_MEASURE("IL strip",
            assembly_strip(assembly_paths, output_path));

    T_MEASURE("JS gen",
            js_gen(linker.get()->getOutput().wasm, assembly_paths,
                output_path));

    T_PRINT("total", total);

#undef T_MEASURE
#undef T_PRINT

    return 0;
}
