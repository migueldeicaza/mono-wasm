all: run

test.exe:      test.cs
	mcs -nostdlib -noconfig -r:../../dist/lib/mscorlib.dll test.cs -out:test.exe

output/index.wasm:      test.exe
	../../dist/bin/mono-wasm -g test.exe -o output

run:    output/index.wasm
	cp index.html output
	(cd output && python -m SimpleHTTPServer 9000)
