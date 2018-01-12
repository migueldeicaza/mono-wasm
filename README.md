# mono-wasm

This project is a proof-of-concept aiming at building C# applications into WebAssembly, by using Mono and compiling/linking everything statically into one .wasm file that can be easily delivered to browsers.

The process does not use Emscripten (or Binaryen) but instead uses the experimental WebAssembly backend of LLVM with `clang` and `lld` to generate the final .wasm code. The goal is to use as few dependencies as possible. At the moment the only dependencies are LLVM, `clang` and `lld` trunk.

`mono-wasm` supports 2 build modes: one that links all the LLVM bitcode into one module then performs a WebAssembly codegen on it, and one that compiles project dependencies into WebAssembly incrementally (the runtime and the mscorlib assembly) then uses `lld` to link into a final .wasm file. The later is experimental but will become the default as it allows build times lesser than a second.

The .wasm file is loaded from JavaScript (see `index.js`), which also exposes proper callbacks for system calls that the C library will be calling into. These syscalls are responsible for heap management, I/O, etc.

This project is a work in progress. Feel free to ping me if you have questions or feedback: laurent.sansonetti@microsoft.com

## Related repositories

* [mono-wasm-mono](https://github.com/lrz/mono-wasm-mono): a fork of Mono with changes for a wasm32 target, used to build the runtime and compiler.
* [mono-wasm-libc](https://github.com/lrz/mono-wasm-libc): a fork of the WebAssembly/musl C library with tweaks for our version of Mono and our JS glue.

## Current status

This is a work in progress, but you can see `sample/hello/hello.cs` running here: www.hipbyte.com/~lrz/mono-wasm-hello

## How does it work?

An ASCII graph is worth a thousand words:

```
+----------------+-------------+  +---------------------+
|  Mono runtime  |  C library  |  |    C# assemblies    | <-------+
+----------------+-------------+  +----------+----------+         |
           clang |                           | mono               |
  -target=wasm32 |                           | -aot=llvmonly      |
                 v                           v                    |
+-------------------------------------------------------+         | load
|                       LLVM bitcode                    |         | metadata
+----------------------------+--------------------------+         | (runtime)
                             | mono-wasm                          |
                             | (bitcode -> wasm)                  | 
                             v                                    | 
+-------------------------------------------------------+         |
|                        index.wasm                     |---------+
+----------------------------------------+--------------+               
                 ^                       | libc                         
   load, compile |                       | syscalls                     
    + run main() |                       v                             
+----------------+--------------------------------------+         +-----------+ 
|                         index.js                      | <-----> |  Browser  |
+-------------------------------------------------------+         +-----------+
```

## Build instructions

We will assume that you want to build everything in the ~/src/mono-wasm directory.

```
$ mkdir ~/src/mono-wasm
$ cd ~/src/mono-wasm
$ git clone git@github.com:lrz/mono-wasm.git build
```

### LLVM+clang+lld with WebAssembly target

We need a copy of the LLVM tooling (clang and lld included) with the experimental WebAssembly target enabled. Make sure to build a Release build (as indicated below) otherwise the WASM codegen will be significantly slower.

```
$ cd ~/src/mono-wasm
$ svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
$ cd llvm/tools
$ svn co http://llvm.org/svn/llvm-project/cfe/trunk clang
$ svn co http://llvm.org/svn/llvm-project/lld/trunk lld
$ cd ../..
$ mkdir llvm-build
$ cd llvm-build
$ cmake -G "Unix Makefiles" -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly -DCMAKE_BUILD_TYPE=Release ../llvm
$ make
```

After you did this you should have the LLVM static libraries for the WebAssembly target:

```
$ ls ~/src/mono-wasm/llvm-build/lib | grep WebAssembly
libLLVMWebAssemblyAsmPrinter.a
libLLVMWebAssemblyCodeGen.a
libLLVMWebAssemblyDesc.a
libLLVMWebAssemblyDisassembler.a
libLLVMWebAssemblyInfo.a
```

You should also have the `~/src/mono-wasm/llvm-build/bin/clang` program built with the wasm32 target:

```
$ ~/src/mono-wasm/llvm-build/bin/clang --version
clang version 5.0.0 (trunk 306818)
Target: x86_64-apple-darwin15.6.0
Thread model: posix
InstalledDir: /Users/lrz/src/mono-wasm/llvm-build/bin

  Registered Targets:
[...]
    wasm32     - WebAssembly 32-bit
    wasm64     - WebAssembly 64-bit
```

You should also have the wasm lld (linker) library:

```
$ ls ~/src/mono-wasm/llvm-build/lib/liblldWasm.a
/Users/lrz/src/mono-wasm/llvm-build/lib/liblldWasm.a
```

### Mono compiler

We need a build a copy of the Mono compiler that we will use to generate LLVM bitcode from assemblies. We are building this for 32-bit Intel (i386) because the Mono compiler assumes way too many things from the host environment when generating the bitcode, so we want to match the target architecture (which is also 32-bit).

First, you need to build a copy of the Mono fork of LLVM. We are building it for both 32-bit and 64-bit Intel so that we can easily switch the Mono compiler back to 64-bit later, and we manually have to copy the headers to the build directory as the Mono build system doesn't support external LLVM builds.

```
$ cd ~/src/mono-wasm
$ git clone git@github.com:mono/llvm.git llvm-mono
$ mkdir llvm-mono-build
$ cd llvm-mono-build
$ cmake -G "Unix Makefiles" -DCMAKE_OSX_ARCHITECTURES="i386;x86_64" ../llvm-mono
$ ditto ../llvm-mono/include include
$ make
```

Now, we can now build the Mono compiler itself.

```
$ cd ~/src/mono-wasm
$ git clone git@github.com:lrz/mono-wasm-mono.git mono-compiler
$ cd mono-compiler
$ ./autogen.sh --host=i386-darwin --with-cross-offsets=offsets-wasm32.h CFLAGS="-DCOMPILE_WASM32 -DMONO_CROSS_COMPILE" CXXFLAGS="-DCOMPILE_WASM32 -DMONO_CROSS_COMPILE" --disable-boehm --with-sigaltstack=no --enable-llvm --enable-llvm-runtime --with-llvm=../llvm-mono-build --disable-btls --with-runtime_preset=testing_aot_full
$ cd mono
$ make
```

At the end of this process you should have a `mono` executable installed as `~/src/mono-wasm/mono-compiler/mono/mini/mono` built for the i386 architecture.

```
$ file ~/src/mono-wasm/mono-compiler/mono/mini/mono
mono/mini/mono: Mach-O executable i386
```

Now let's build the `mscorlib.dll` assembly for the WebAssembly profile. We can't use the mono runtime we just built as it's full AOT, so we use assume you have a normal `mono` runtime in your PATH that we can use. Clearly a hack, but in the meantime it works.

```
$ cd ~/src/mono-wasm/mono-compiler/mcs/class/corlib
$ make V=1 PROFILE=wasm RUNTIME=mono STRING_REPLACER=true SN=true
```

After this you should have the assembly file created in the proper location:

```
$ file ~/src/mono-wasm/mono-compiler/mcs/class/lib/wasm/mscorlib.dll 
/Users/lrz/src/mono-wasm/mono-compiler/mcs/class/lib/wasm/mscorlib.dll: PE32 executable (DLL) (console) Intel 80386 Mono/.Net assembly, for MS Windows
```

### Mono runtime

Now we can prepare the Mono runtime. We have to clone a new copy of the source code. We are not building the runtime code using the Mono autotools system, so we have to copy header files that are normally generated.

```
$ cd ~/src/mono-wasm
$ git clone git@github.com:lrz/mono-wasm-mono.git mono-runtime
$ cd mono-runtime
$ cp config-wasm32.h config.h
$ cp eglib/src/eglib-config-wasm32.h eglib/src/eglib-config.h
```

### C library

Similarly as above, we clone a copy of the C library that we will be using.

```
$ cd ~/src/mono-wasm
$ git clone git@github.com:lrz/mono-wasm-libc.git libc
```

### OK ready!

We are ready to build our Hello World.

First, we need to build everything into the `dist` directory:

```
$ cd ~/src/mono-wasm/build
$ vi Makefile               # make sure the *_PATH variables point to proper locations, should be the case if you followed these instructions
$ make
```

This will build the mono runtime and the libc as LLVM bitcode using our version of clang, then link everything into a `runtime.bc` file. This will also build the `mono-wasm` tool which links against the LLVM and lld libraries. Finally, we will copy the Mono compiler and its `mscorlib.dll` file.

```
$ find dist -type f
dist/bin/monoc
dist/bin/mono-wasm
dist/lib/runtime.bc
dist/lib/index.js
dist/lib/mscorlib.dll
dist/lib/mscorlib.xml
```

Once done, you can build the Hello World sample:

```
$ cd sample/hello
$ make
```

## TODO

TODO (now):

* fix garbage collection (need to figure out how to scan the stack)
* ship a first 'alpha' release

TODO (later):

* put mscorlib on a diet (currently 'hello world' is 10MB) by removing more functionality within the `wasm.make` profile and doing more aggressive IL linking
* work on patches for mono based on the changes made in the fork
* merge the WebAssembly LLVM code into the mono/llvm fork so that the Mono compiler can target wasm32 directly, and that we can merge the code into `mono-wasm` (we won't have to ship the Mono compiler separately as `monoc`)
* improve the C# -> JS interop by doing a full C# API replication in JS like embeddinator 4000
* investigate: threads, sockets, debugger, stack unwinding, simd and atomic operations, etc.

## License

This work is distributed under the terms of the MIT license. See the LICENSE.txt file for more information.
