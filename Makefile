MONO_PATH = ../mono
MUSL_PATH = ../musl
LLVM_PATH = ../llvm-build
BINARYEN_PATH = ../binaryen
D8_PATH = ../wasm-install/bin/d8

BOOT_CFLAGS = -arch i386 -fno-stack-protector

MUSL_CFLAGS = -fno-stack-protector -nostdinc -I$(MUSL_PATH)/include -I$(MUSL_PATH)/src/internal -I$(MUSL_PATH)/arch/wasm32 -target wasm32 -Wno-shift-op-parentheses -Wno-incompatible-library-redeclaration

MONO_INCLUDES = -I$(MONO_PATH) -I$(MONO_PATH)/mono -I$(MONO_PATH)/eglib/src
MONO_CFLAGS = $(BOOT_CFLAGS) $(MONO_INCLUDES) -DHAVE_CONFIG_H -D_THREAD_SAFE -DGC_MACOSX_THREADS -DPLATFORM_MACOSX -DUSE_MMAP -DUSE_MUNMAP -std=gnu99 -fwrapv -DMONO_DLL_EXPORT -DMONO_WASM -DDISABLE_JIT -Wno-unused-value -Wno-tautological-compare -DDISABLE_SOCKETS -DDISABLE_PORTABILITY -DDISABLE_ATTACH -DDISABLE_PERFCOUNTERS

all: run

build/libmini.bc: $(patsubst %, build/mini/%.bc, abcremoval alias-analysis aot-compiler aot-runtime branch-opts cfgdump cfold debug-mini debugger-agent decompose dominators driver dwarfwriter exceptions-x86 graph helpers image-writer jit-icalls linear-scan liveness lldb local-propagation memory-access method-to-ir mini-x86-gsharedvt mini-x86 mini-codegen mini-cross-helpers mini-darwin mini-exceptions mini-gc mini-generic-sharing mini-native-types mini-posix mini-runtime mini-trampolines mini seq-points simd-intrinsics ssa tasklets trace tramp-x86-gsharedvt tramp-x86 type-checking unwind xdebug)

build/libmetadata.bc: $(patsubst %, build/metadata/%.bc, appdomain assembly attach class-accessors class cominterop console-unix coree custom-attrs debug-helpers debug-mono-ppdb debug-mono-symfile decimal-ms domain dynamic-image dynamic-stream environment exception file-mmap-posix file-mmap-windows filewatcher gc-stats gc handle icall image jit-info loader locales lock-tracer marshal mempool metadata-cross-helpers metadata-verify metadata method-builder monitor mono-basic-block mono-conc-hash mono-config mono-config-dirs mono-debug mono-endian mono-hash mono-mlist mono-perfcounters mono-route mono-security number-ms object opcodes profiler property-bag rand reflection remoting runtime security-core-clr security-manager seq-points-data sgen-bridge sgen-mono sgen-new-bridge sgen-old-bridge sgen-stw sgen-tarjan-bridge sgen-toggleref sre-encode sre-save sre string-icalls sysmath threadpool-io threadpool-worker-default threadpool threads verify w32error-unix w32event-unix w32file-unix-glob w32file-unix w32file w32handle-namespace w32handle w32mutex-unix w32process-unix-bsd w32process-unix-default w32process-unix-haiku w32process-unix-osx w32process-unix w32process w32semaphore-unix w32socket-unix w32socket)

build/libutils.bc: $(patsubst %, build/utils/%.bc, atomic mono-os-mutex bsearch mono-path checked-build mono-poll dlmalloc mono-proclib-windows hazard-pointer mono-proclib json mono-property-hash lock-free-alloc mono-publib lock-free-array-queue mono-rand-windows lock-free-queue mono-rand mach-support-x86 mono-sha1 mach-support mono-stdlib memfuncs mono-threads-android mono-codeman mono-threads-coop mono-conc-hashtable mono-threads-freebsd mono-context mono-threads-haiku mono-counters mono-threads-linux mono-dl-darwin mono-threads-mach-helper mono-dl-posix mono-threads-mach mono-dl-windows mono-threads-netbsd mono-dl mono-threads-openbsd mono-error mono-threads-posix-signals mono-filemap mono-threads-posix mono-hwcap-x86 mono-threads-state-machine mono-hwcap mono-threads-windows mono-internal-hash mono-threads mono-io-portability mono-time mono-linked-list-set mono-tls mono-log-android mono-uri mono-log-common mono-value-hash mono-log-darwin monobitset mono-log-posix networking-fallback mono-log-windows networking-missing mono-logger networking-posix mono-math networking-windows mono-md5 networking mono-mmap-windows os-event-unix mono-mmap parse mono-networkinterfaces strenc)

build/libsgen.bc: $(patsubst %, build/sgen/sgen-%.bc, alloc array-list cardtable debug descriptor fin-weak-hash gc gchandles gray hash-table internal layout-stats los marksweep memory-governor nursery-allocator pinning-stats pinning pointer-queue protocol qsort simple-nursery split-nursery thread-pool workers)

build/libeglib.bc: $(patsubst %, build/eglib/%.bc, garray goutput gbytearray gpath gdate-unix gpattern gdir-unix gptrarray gerror gqsort gfile-posix gqueue gfile-unix gshell gfile gslist ghashtable gspawn giconv gstr glist gstring gmarkup gtimer-unix gmem gunicode gmisc-unix gutf8 gmodule-unix)

build/libmono.bc: build/libmini.bc build/libmetadata.bc build/libutils.bc build/libsgen.bc build/libeglib.bc

build/libc.bc: $(patsubst %.c, build/libc/%.bc, $(shell (cd $(MUSL_PATH)/src && ls {ctype,env,errno,exit,internal,ldso,malloc,math,prng,stdio,string,stdlib,time,unistd}/*.c | grep -Ev "(pread|pwrite|strtok_r)")) thread/__lock.c signal/block.c signal/raise.c)

build/libmini.bc build/libmetadata.bc build/libeglib.bc build/libsgen.bc build/libutils.bc build/libmono.bc build/libc.bc:
	$(LLVM_PATH)/bin/llvm-link $^ -o $@

build/libc/%.bc: $(MUSL_PATH)/src/%.c
	@/bin/mkdir -p $(dir $@)
	$(LLVM_PATH)/bin/clang $(MUSL_CFLAGS) $< -c -emit-llvm -o $@

build/mini/%.bc : $(MONO_PATH)/mono/mini/%.c
	@/bin/mkdir -p build/mini
	/usr/bin/clang -I$(MONO_PATH)/mono/mini $(MONO_CFLAGS) $< -c -emit-llvm -o $@

build/metadata/%.bc : $(MONO_PATH)/mono/metadata/%.c
	@/bin/mkdir -p build/metadata
	/usr/bin/clang -I$(MONO_PATH)/mono/metadata $(MONO_CFLAGS) $< -c -emit-llvm -o $@

build/utils/%.bc : $(MONO_PATH)/mono/utils/%.c
	@/bin/mkdir -p build/utils
	/usr/bin/clang -I$(MONO_PATH)/mono/utils $(MONO_CFLAGS) $< -c -emit-llvm -o $@

build/sgen/%.bc : $(MONO_PATH)/mono/sgen/%.c
	@/bin/mkdir -p build/sgen
	/usr/bin/clang -I$(MONO_PATH)/mono/sgen $(MONO_CFLAGS) $< -c -emit-llvm -o $@

build/eglib/%.bc : $(MONO_PATH)/eglib/src/%.c
	@/bin/mkdir -p build/eglib
	/usr/bin/clang $(MONO_CFLAGS) $< -c -emit-llvm -o $@

index.bc:   boot.c stubs.c build/libc.bc build/libmono.bc
	/usr/bin/clang $(BOOT_CFLAGS) $(MONO_INCLUDES) boot.c -c -emit-llvm -o boot.bc
	/usr/bin/clang $(BOOT_CFLAGS) stubs.c -c -emit-llvm -o stubs.bc
	$(LLVM_PATH)/bin/llvm-link build/libc.bc build/libmono.bc boot.bc stubs.bc -o index.bc

index.s:        index.bc
	$(LLVM_PATH)/bin/llc -asm-verbose=false -march=wasm32 -o index.s index.bc
	/usr/bin/perl -p -i -e "s/\.comm/.lcomm/g" index.s

index.wasm:     index.s
	$(BINARYEN_PATH)/bin/s2wasm --validate wasm --allocate-stack 1000000 index.s -o index.wast
	ruby fix-wast.rb index.wast
	$(BINARYEN_PATH)/bin/wasm-as --validate wasm -g index.wast -o index.wasm

run:    index.wasm index.js
	$(D8_PATH) --expose-wasm index.js

clean:
	/bin/rm -rf build
