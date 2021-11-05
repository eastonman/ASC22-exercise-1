CC=icc
CPPFLAGS= -Iinclude --std=c++11 -Ofast -axCORE-AVX2 -march=core-avx2 -mtune=core-avx2 -pthread -qopenmp -fma -static-libgcc -static-libstdc++ -stdlib=libc++

.PHONY: default main SLIC

default: SLIC main
	$(CC) $(CPPFLAGS) main.o src/SLIC.o -o main

run: default
	./main

perf: default
	perf record ./main

SLIC: src/SLIC.cpp include/SLIC.h
	$(CC) $(CPPFLAGS) -c src/SLIC.cpp -o src/SLIC.o

asm: src/SLIC.cpp
	$(CC) $(CPPFLAGS) -S src/SLIC.cpp -o slic.S

main: main.cpp
	$(CC) $(CPPFLAGS) -c main.cpp -o main.o

clean:
	rm *.o
	rm src/*.o
	rm main