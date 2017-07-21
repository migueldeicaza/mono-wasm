# mono-wasm

TODO (now):

* put forks of mono and musl in source control
* get `System.Console.PrintLine("hello world")` running
* build mono with the wasm32 target (instead of darwin/i386) so that we can remove the silly hacks

TODO (later):

* write a simple tool that does the IR -> wasm generation (instead of calling llc + the binaryen tools) so that we can better control linking optimizations (ex. LTO)
* investigate threads, sockets, file system...
