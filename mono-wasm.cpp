#include <mach/mach_time.h>
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

static std::unique_ptr<llvm::Module>
bitcode_link(std::vector<std::string> &paths, llvm::LLVMContext &context)
{
    auto module = llvm::make_unique<llvm::Module>("index.bc", context);
    llvm::Linker linker(*module);

    for (auto path : paths) {
        llvm::SMDiagnostic err;
        auto file_module = llvm::parseIRFile(path, err, context);
        if (!file_module) {
            err.print(path.c_str(), llvm::errs());
            exit(1);
        }

        if (linker.linkInModule(std::move(file_module),
                    llvm::Linker::Flags::OverrideFromSrc)) {
            fprintf(stderr, "linking %s failed\n", path.c_str());
            exit(1);
        }
    }

    return module;
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
        llvm::errs() << err;
        exit(1);
    }

    std::string cpu_str = "";
    std::string features_str = "";
    llvm::TargetOptions options;
    options.MCOptions.AsmVerbose = false;

    auto target_machine = target->createTargetMachine(triple.getTriple(),
            cpu_str, features_str, options, llvm::None,
            llvm::CodeModel::Default, opt_level);

    if (target_machine == NULL) {
        fprintf(stderr, "couldn't allocate target machine\n");
        exit(1);
    }

    llvm::legacy::PassManager pm;
    pm.add(new llvm::TargetLibraryInfoWrapperPass(
                llvm::TargetLibraryInfoImpl(triple)));

    module->setDataLayout(target_machine->createDataLayout());

    malloc_ostream stream(1024 * 1000 * 100);

    if (target_machine->addPassesToEmitFile(pm, stream,
                llvm::TargetMachine::CGFT_AssemblyFile)) {
        llvm::errs() << "target does not support assembly generation\n";
        exit(1);
    }

    pm.run(*module);

    return stream.buffer();
}

static std::unique_ptr<wasm::Linker>
wasm_link(const char *text)
{
    wasm::S2WasmBuilder builder(text, false);

    auto linker = std::make_unique<wasm::Linker>(0, 2000000, 0, 0, false,
            false, "", false);
    linker->linkObject(builder);
    linker->layout();
    return linker;
}

static void
wasm_write(wasm::Module &wasm, bool debug_names, const char *path)
{
    wasm::BufferWithRandomAccess buffer(false);

    wasm::WasmBinaryWriter writer(&wasm, buffer, false);
    writer.setNamesSection(debug_names);
    writer.write();

    wasm::Output output(path, wasm::Flags::Binary, wasm::Flags::Release);
    buffer.writeTo(output);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "Usage: %s [options] <input files>\n\n" \
                "Options:\n" \
                "    -o <output.wasm>      - Output file\n" \
                "    -On                   - Specify optimization level\n" \
                "                            (0, 1, 2, 3, default is 2)\n" \
                "    -g                    - Emit debug information\n",
                argv[0]);
        exit(1);
    }

    const char *output_path = NULL;
    llvm::CodeGenOpt::Level opt = llvm::CodeGenOpt::Default;
    bool emit_debug = false;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-o") == 0) {
            i++;
            if (i >= argc) {
                fprintf(stderr, "expected value for `-o' option\n");
                exit(1);
            }
            output_path = argv[i];
        }
        else if (strcmp(arg, "-g") == 0) {
            emit_debug = true;
        }
        else if (arg[0] == '-' && arg[1] == 'O') {
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
                   fprintf(stderr, "malformed `-On' option\n");
                   exit(1);
           }
        }
        else {
            paths.push_back(arg);
        }
    }
    if (output_path == NULL) {
        fprintf(stderr, "`-o' option required\n");
        exit(1);
    }

    llvm::LLVMContext context;
    context.setDiagnosticHandler(diagnostic_handler, NULL, true);

    uint64_t start = 0, total = 0, delta = 0;
    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);

#define T_PRINT(what, delta) \
    printf("%15s : %.3fs\n", what, \
            (((double)(delta) * timebase_info.numer) \
             / (timebase_info.denom * 1000000000)))

#define T_MEASURE(what, code) \
    start = mach_absolute_time(); \
    code; \
    delta = mach_absolute_time() - start; \
    total += delta; \
    T_PRINT(what, delta)

    T_MEASURE("bitcode link", auto module = bitcode_link(paths, context));

    T_MEASURE("wasm assembly",
            auto text = wasm_assembly(module.get(), opt, context));

    T_MEASURE("wasm link", auto linker = wasm_link(text));

    T_MEASURE("wasm write",
            wasm_write(linker.get()->getOutput().wasm, emit_debug,
                output_path));

    T_PRINT("total", total);

#undef T_MEASURE
#undef T_PRINT

    return 0;
}
