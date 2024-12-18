CC_FLAGS ?= -O3

ifeq ($(shell uname -m),x86_64)
	CC_FLAGS += -march=native -pthread -lpthread
endif

default: build

clean:
	@rm -rf ecloop bench main a.out *.profraw *.profdata

build: clean
	cc $(CC_FLAGS) main.c -o ecloop

# -----------------------------------------------------------------------------

n = 1024

add: build
	./ecloop add -f data/btc-puzzles-hash -t 4 -r 8000:ffffff

mul: build
	cat misc/bw_priv.txt | ./ecloop mul -f misc/bw_addr.txt -t 4 -a cu -q -o /dev/null

blf: build
	rm -rf /tmp/test.blf; cat data/btc-puzzles-hash | ./ecloop blf-gen -n $(n) -o /tmp/test.blf
	./ecloop add -f /tmp/test.blf -t 1 -r 8000:ffffff -q -o /dev/null

# -----------------------------------------------------------------------------

host=user@colima
cmd=add

check-remote:
	@ssh -tt $(host) 'cc --version'
	@rsync -arc --delete-after --exclude 'addrs.txt' --exclude 'internal/' ./ $(host):/tmp/ecloop
	ssh -tt $(host) 'cd /tmp/ecloop; make $(cmd)'
