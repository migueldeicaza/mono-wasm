MONO_RUNTIME_PATH = ../mono-runtime
MONO_COMPILER_PATH = ../mono-compiler
LIBC_PATH = ../libc
LLVM_PATH = ../llvm-build

CLANG = $(LLVM_PATH)/bin/clang

LIBC_CFLAGS = -fno-stack-protector -nostdinc -I$(LIBC_PATH)/include -I$(LIBC_PATH)/arch/wasm32 -target wasm32 -Wno-shift-op-parentheses -Wno-incompatible-library-redeclaration -Wno-bitwise-op-parentheses

LIBC_INTERNAL_CFLAGS = $(LIBC_CFLAGS) -I$(LIBC_PATH)/src/internal

MONO_CFLAGS = $(LIBC_CFLAGS) -I$(MONO_RUNTIME_PATH) -I$(MONO_RUNTIME_PATH)/mono -I$(MONO_RUNTIME_PATH)/eglib/src -DHAVE_CONFIG_H -D_THREAD_SAFE -DUSE_MMAP -DUSE_MUNMAP -std=gnu99 -fwrapv -DMONO_DLL_EXPORT -Wno-unused-value -Wno-tautological-compare -Wno-bitwise-op-parentheses

all: dist-install

build/libmini.bc: $(patsubst %, build/mini/%.bc, abcremoval alias-analysis aot-compiler aot-runtime branch-opts cfgdump cfold debug-mini debugger-agent decompose dominators driver dwarfwriter graph helpers image-writer jit-icalls linear-scan liveness lldb local-propagation memory-access method-to-ir mini-codegen mini-cross-helpers mini-wasm32 mini-exceptions mini-gc mini-generic-sharing mini-native-types mini-posix mini-runtime mini-trampolines mini seq-points simd-intrinsics ssa tasklets trace type-checking unwind xdebug)

build/libmetadata.bc: $(patsubst %, build/metadata/%.bc, appdomain assembly attach class-accessors class cominterop console-wasm coree custom-attrs debug-helpers debug-mono-ppdb debug-mono-symfile decimal-ms domain dynamic-image dynamic-stream environment exception file-mmap-posix file-mmap-windows filewatcher gc-stats gc handle icall image jit-info loader locales lock-tracer marshal mempool metadata-cross-helpers metadata-verify metadata method-builder monitor mono-basic-block mono-conc-hash mono-config mono-config-dirs mono-debug mono-endian mono-hash mono-mlist mono-perfcounters mono-route mono-security number-ms object opcodes profiler property-bag rand reflection remoting runtime security-core-clr security-manager seq-points-data sgen-bridge sgen-mono sgen-new-bridge sgen-old-bridge sgen-stw sgen-tarjan-bridge sgen-toggleref sre-encode sre-save sre string-icalls sysmath threadpool-io threadpool-worker-default threadpool threads verify w32error-unix w32event-unix w32file-unix-glob w32file-unix w32file w32handle-namespace w32handle w32mutex-unix w32process-unix-bsd w32process-unix-default w32process-unix-haiku w32process-unix-osx w32process-unix w32process w32semaphore-unix w32socket-unix w32socket)

build/libutils.bc: $(patsubst %, build/utils/%.bc, atomic mono-os-mutex bsearch mono-path checked-build mono-poll dlmalloc mono-proclib-windows hazard-pointer mono-proclib json mono-property-hash lock-free-alloc mono-publib lock-free-array-queue mono-rand-windows lock-free-queue mono-rand mono-sha1 mach-support mono-stdlib memfuncs mono-threads-android mono-codeman mono-threads-coop mono-conc-hashtable mono-threads-freebsd mono-context mono-threads-haiku mono-counters mono-threads-linux mono-dl-darwin mono-threads-mach-helper mono-dl-posix mono-threads-mach mono-dl-windows mono-threads-netbsd mono-dl mono-threads-openbsd mono-error mono-threads-posix-signals mono-filemap mono-threads-posix mono-threads-state-machine mono-hwcap mono-hwcap-wasm32 mono-threads-windows mono-internal-hash mono-threads mono-io-portability mono-time mono-linked-list-set mono-tls mono-log-android mono-uri mono-log-common mono-value-hash mono-log-darwin monobitset mono-log-posix networking-fallback mono-log-windows networking-missing mono-logger networking-posix mono-math networking-windows mono-md5 networking mono-mmap-windows os-event-unix mono-mmap parse mono-networkinterfaces strenc)

build/libsgen.bc: $(patsubst %, build/sgen/sgen-%.bc, alloc array-list cardtable debug descriptor fin-weak-hash gc gchandles gray hash-table internal layout-stats los marksweep memory-governor nursery-allocator pinning-stats pinning pointer-queue protocol qsort simple-nursery split-nursery thread-pool workers)

build/libeglib.bc: $(patsubst %, build/eglib/%.bc, garray goutput gbytearray gpath gdate-unix gpattern gdir-unix gptrarray gerror gqsort gfile-posix gqueue gfile-unix gshell gfile gslist ghashtable gspawn giconv gstr glist gstring gmarkup gtimer-unix gmem gunicode gmisc-unix gutf8 gmodule-unix)

build/libmono.bc: build/libmini.bc build/libmetadata.bc build/libutils.bc build/libsgen.bc build/libeglib.bc

build/libc.bc: $(patsubst %.c, build/libc/%.bc, $(shell (cd $(LIBC_PATH)/src && ls {ctype,env,errno,exit,internal,ldso,dlmalloc,fcntl,locale,math,prng,signal,stdio,string,stdlib,time,unistd}/*.c | grep -Ev "(pread|pwrite|sigaltstack|strtok_r)")) conf/sysconf.c thread/__lock.c misc/getrlimit.c mman/madvise.c stat/stat.c stat/fstat.c)

build/libmini.bc build/libmetadata.bc build/libeglib.bc build/libsgen.bc build/libutils.bc build/libmono.bc build/libc.bc:
	$(LLVM_PATH)/bin/llvm-link $^ -o $@

build/libc/%.bc: $(LIBC_PATH)/src/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) $(LIBC_INTERNAL_CFLAGS) $< -c -emit-llvm -o $@

build/mini/%.bc : $(MONO_RUNTIME_PATH)/mono/mini/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) -I$(MONO_RUNTIME_PATH)/mono/mini $(MONO_CFLAGS) $< -c -emit-llvm -o $@

build/metadata/%.bc : $(MONO_RUNTIME_PATH)/mono/metadata/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) -I$(MONO_RUNTIME_PATH)/mono/metadata $(MONO_CFLAGS) -DHAVE_SGEN_GC $< -c -emit-llvm -o $@

build/utils/%.bc : $(MONO_RUNTIME_PATH)/mono/utils/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) -I$(MONO_RUNTIME_PATH)/mono/utils $(MONO_CFLAGS) -DHAVE_SGEN_GC $< -c -emit-llvm -o $@

build/sgen/%.bc : $(MONO_RUNTIME_PATH)/mono/sgen/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) -I$(MONO_RUNTIME_PATH)/mono/sgen $(MONO_CFLAGS) -DHAVE_SGEN_GC $< -c -emit-llvm -o $@

build/eglib/%.bc : $(MONO_RUNTIME_PATH)/eglib/src/%.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) $(MONO_CFLAGS) $< -c -emit-llvm -o $@

mscorlib.dll: $(MONO_COMPILER_PATH)/mcs/class/lib/wasm/mscorlib.dll
	cp $< $@

build/boot.bc:        boot.c
	@/bin/mkdir -p $(dir $@)
	$(CLANG) $(MONO_CFLAGS) boot.c -c -emit-llvm -o build/boot.bc

build/runtime.bc:     build/boot.bc build/libc.bc build/libmono.bc
	@/bin/mkdir -p $(dir $@)
	$(LLVM_PATH)/bin/llvm-link build/libc.bc build/libmono.bc build/boot.bc -o build/runtime.bc

MONO_WASM_CXXFLAGS = -Wno-sign-compare -std=c++1y -UNDEBUG -fexceptions
MONO_WASM_LLVM_COMPONENTS = BitReader BitWriter Core IRReader Linker Object Support TransformUtils IPO webassembly Option

jsmin.o:        jsmin.c
	/usr/bin/clang -c jsmin.c -o jsmin.o

mono-wasm:      jsmin.o mono-wasm.cpp
	/usr/bin/clang++ $(shell $(LLVM_PATH)/bin/llvm-config --cxxflags --ldflags) -Wno-gnu $(MONO_WASM_CXXFLAGS) -I$(shell $(LLVM_PATH)/bin/llvm-config --src-root)/tools/lld/include -g mono-wasm.cpp -o mono-wasm -lncurses -lz jsmin.o $(shell $(LLVM_PATH)/bin/llvm-config --libs $(MONO_WASM_LLVM_COMPONENTS)) -llldCommon -llldCore -llldDriver -llldReaderWriter -llldWasm

dist-install:   mono-wasm build/runtime.bc mscorlib.dll
	rm -rf dist
	mkdir -p dist/bin
	cp mono-wasm dist/bin
	cp $(MONO_COMPILER_PATH)/mono/mini/mono dist/bin/monoc
	mkdir -p dist/lib
	cp mscorlib.dll dist/lib
	cp mscorlib.xml dist/lib
	cp build/runtime.bc dist/lib
	cp index.js dist/lib

need-version:
ifndef VERSION
    $(error VERSION is undefined)
endif

make-dist-dir:
DIST_DIR = mono-wasm-macos-$(VERSION)

release:	need-version make-dist-dir dist-install
	mkdir -p releases
	(cd releases \
	  && mkdir $(DIST_DIR) \
	  && ditto ../dist $(DIST_DIR)/dist \
	  && ditto ../sample $(DIST_DIR)/sample \
	  && for i in `ls $(DIST_DIR)/sample`; do (cd $(DIST_DIR)/sample/$$i && make clean); done \
	  && zip -r $(DIST_DIR).zip $(DIST_DIR))

clean:
	/bin/rm -rf build dist mscorlib.dll jsmin.o mono-wasm
