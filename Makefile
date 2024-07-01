CC_FLAGS ?= -O3

ifeq ($(shell uname -m),x86_64)
	CC_FLAGS += -march=native
endif

default: build

clean:
	@rm -rf ecloop bench main a.out *.profraw *.profdata

build: clean
	@# @$(CC) $(CC_FLAGS) main.c -o ecloop
	@clang $(CC_FLAGS) main.c -o ecloop

# -----------------------------------------------------------------------------

add: build
	./ecloop add -f data/btc-puzzles-hash -t 4 -r 8000:ffffff

mul: build
	cat data.txt | ./ecloop mul -f _check_1.txt -t 4 -a cu

blf: build
	@rm -rf /tmp/test.blf
	cat data/btc-puzzles-hash | ./ecloop blf-gen -n 1024 -o /tmp/test.blf
	./ecloop add -f /tmp/test.blf -t 8 -r 8000:ffffff
