CLANG_FLAGS ?= -O3

ifeq ($(shell uname -m),x86_64)
	CLANG_FLAGS += -msse4.1 -msha
endif

default:
	@make build
	./ecloop -i data/btc-puzzles-hash -t 1 -r 800000:ffffff

build: clean
	@clang $(CLANG_FLAGS) main.c -o ecloop

bench: clean
	@clang $(CLANG_FLAGS) bench.c -o ./bench && ./bench

clean:
	@rm -rf ecloop bench main a.out *.profraw *.profdata
