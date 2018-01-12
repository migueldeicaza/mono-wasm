all: hello.exe output/index.wasm output/index.html

hello.exe:      hello.cs
	mcs -nostdlib -noconfig -r:../../dist/lib/mscorlib.dll hello.cs -out:hello.exe

output/index.wasm:      hello.exe
	../../dist/bin/mono-wasm -i hello.exe -o output

output/index.html:      index.html
	cp index.html output

clean:
	rm -rf build output hello.exe
