all: run

hello.exe:      hello.cs
	mcs -nostdlib -noconfig -r:../../dist/lib/mscorlib.dll hello.cs -out:hello.exe

run:    hello.exe
	../../dist/bin/mono-wasm -g hello.exe -o output
	(cd output && d8 --expose-wasm index.js)
