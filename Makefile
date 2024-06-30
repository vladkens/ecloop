CLANG_FLAGS ?= -O3

ifeq ($(shell uname -m),x86_64)
	CLANG_FLAGS += -msse4.1 -msha
endif

default: build

clean:
	@rm -rf ecloop bench main a.out *.profraw *.profdata

build: clean
	@clang $(CLANG_FLAGS) main.c -o ecloop

add: build
	./ecloop add -f data/btc-puzzles-hash -t 1 -r 800000:ffffff -o /tmp/abc.csv

mul: build
	cat data.txt | ./ecloop mul -t 8 -f _check_1.txt -a cu -o /tmp/abc.csv
