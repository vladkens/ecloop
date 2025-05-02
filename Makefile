.PHONY: default clean build bench fmt add mul rnd blf remote

CC = cc
CC_FLAGS ?= -O3

ifeq ($(shell uname -m),x86_64)
	CC_FLAGS += -march=native -pthread -lpthread
endif

default: build

clean:
	@rm -rf ecloop bench main a.out *.profraw *.profdata

build: clean
	$(CC) $(CC_FLAGS) main.c -o ecloop

bench: build
	./ecloop bench

fmt:
	@find . -name '*.c' | xargs clang-format -i

# -----------------------------------------------------------------------------

n = 1024

add: build
	./ecloop add -f data/btc-puzzles-hash -r 8000:ffffff

mul: build
	cat data/btc-bw-priv | ./ecloop mul -f data/btc-bw-hash -a cu -q -o /dev/null

rnd: build
	./ecloop rnd -f data/btc-puzzles-hash -r 100000000:ffffffffffffffffffffffff

blf: build
	rm -rf /tmp/test.blf; cat data/btc-puzzles-hash | ./ecloop blf-gen -n $(n) -o /tmp/test.blf
	./ecloop add -f /tmp/test.blf -t 1 -r 8000:ffffff -q -o /dev/null

verify: build
	./ecloop mult-verify

# -----------------------------------------------------------------------------

host=mele
cmd=add

remote:
	@ssh -tt $(host) '$(CC) --version'
	@rsync -arc --delete-after --exclude={'ecloop'} ./ $(host):/tmp/ecloop
	ssh -tt $(host) 'cd /tmp/ecloop; make $(cmd) CC=$(CC)'
