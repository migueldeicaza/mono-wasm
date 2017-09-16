all: run

hello.exe:      hello.cs
	mcs -nostdlib -noconfig -r:../../dist/lib/mscorlib.dll hello.cs -out:hello.exe

output/index.wasm:      hello.exe
	../../dist/bin/mono-wasm -g hello.exe -o output

run:    output/index.wasm
	cp index.html output
	(cd output && python -m SimpleHTTPServer 9000)
