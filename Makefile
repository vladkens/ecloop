.PHONY: default clean build bench fmt add mul rnd blf remote

CC = cc
CC_FLAGS ?= -O3 -ffast-math
# CC_FLAGS ?= -O3 -ffast-math -Wall -Wextra

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

add: build
	./ecloop add -f data/btc-puzzles-hash -r 8000:ffffff

mul: build
	cat data/btc-bw-priv | ./ecloop mul -f data/btc-bw-hash -a cu -q -o /dev/null

rnd: build
	./ecloop rnd -f data/btc-puzzles-hash -r 800000000000000000:ffffffffffffffffff -d 0:32

blf: build
	@rm -rf /tmp/test.blf
	@printf "\n> "
	cat data/btc-puzzles-hash | ./ecloop blf-gen -n 32768 -o /tmp/test.blf
	@printf "\n> "
	cat data/btc-bw-hash | ./ecloop blf-gen -n 32768 -o /tmp/test.blf
	@printf "\n> "
	./ecloop add -f /tmp/test.blf -r 8000:ffffff -q -o /dev/null
	@printf "\n> "
	cat data/btc-bw-priv | ./ecloop mul -f /tmp/test.blf -a cu -q -o /dev/null

verify: build
	./ecloop mult-verify

# -----------------------------------------------------------------------------

range_71 = 400000000000000000:7fffffffffffffffff
range_72 = 800000000000000000:ffffffffffffffffff
range_73 = 1000000000000000000:1ffffffffffffffffff
range_74 = 2000000000000000000:3ffffffffffffffffff
_RANGES_ = $(foreach r,$(filter range_%,$(.VARIABLES)),$(patsubst range_%,%,$r))

puzzle: build
	@$(if $(filter $(_RANGES_),$(n)),,$(error "Invalid range $(n)"))
	./ecloop rnd -f data/btc-puzzles-hash -d 0:32 -r $(range_$(n)) -o ./found_$(n).txt

%:
	@$(if $(filter $(_RANGES_),$@),make puzzle n=$@,)

# -----------------------------------------------------------------------------

host=mele
cmd=add

remote:
	@ssh -tt $(host) '$(CC) --version'
	@rsync -arc --delete-after --exclude={'ecloop'} ./ $(host):/tmp/ecloop
	ssh -tt $(host) 'cd /tmp/ecloop; make $(cmd) CC=$(CC)'
