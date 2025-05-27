.PHONY: default clean build bench fmt add mul rnd blf remote

CC = cc
CC_FLAGS ?= -O3 -ffast-math -Wall -Wextra

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
# https://btcpuzzle.info/puzzle

range_28 = 8000000:fffffff
range_32 = 80000000:ffffffff
range_33 = 100000000:1ffffffff
range_34 = 200000000:3ffffffff
range_35 = 400000000:7ffffffff
range_36 = 800000000:fffffffff
range_71 = 400000000000000000:7fffffffffffffffff
range_72 = 800000000000000000:ffffffffffffffffff
range_73 = 1000000000000000000:1ffffffffffffffffff
range_74 = 2000000000000000000:3ffffffffffffffffff
range_76 = 8000000000000000000:fffffffffffffffffff
range_77 = 10000000000000000000:1fffffffffffffffffff
range_78 = 20000000000000000000:3fffffffffffffffffff
range_79 = 40000000000000000000:7fffffffffffffffffff
_RANGES_ = $(foreach r,$(filter range_%,$(.VARIABLES)),$(patsubst range_%,%,$r))

puzzle: build
	@$(if $(filter $(_RANGES_),$(n)),,$(error "Invalid range $(n)"))
	./ecloop rnd -f data/btc-puzzles-hash -d 0:32 -r $(range_$(n)) -o ./found_$(n).txt

%:
	@$(if $(filter $(_RANGES_),$@),make --no-print-directory puzzle n=$@,)

# -----------------------------------------------------------------------------

host=mele
cmd=add

remote:
	@rsync -arc --progress --delete-after --exclude={'ecloop','found*.txt','.git'} ./ $(host):/tmp/ecloop
	@ssh -tt $(host) 'clear; $(CC) --version'
	ssh -tt $(host) 'cd /tmp/ecloop; make $(cmd) CC=$(CC)'

bench-compare:
	@ssh -tt $(host) " \
	cd /tmp; rm -rf ecloop keyhunt; \
	cd /tmp && git clone https://github.com/vladkens/ecloop.git && cd ecloop && make CC=clang; \
	echo '--------------------------------------------------'; \
	cd /tmp && git clone https://github.com/albertobsd/keyhunt.git && cd keyhunt && make; \
	echo '--------------------------------------------------'; \
	cd /tmp; \
	echo '--- t=1 (keyhunt)'; \
	time ./keyhunt/keyhunt -m rmd160 -f ecloop/data/btc-bw-hash -r 8000:fffffff -t 1 -n 16777216; \
	echo '--- t=1 (ecloop)'; \
	time ./ecloop/ecloop add -f ecloop/data/btc-bw-hash -t 1 -r 8000:fffffff; \
	echo '--- t=4 (keyhunt)'; \
	time ./keyhunt/keyhunt -m rmd160 -f ecloop/data/btc-bw-hash -r 8000:fffffff -t 4 -n 16777216; \
	echo '--- t=4 (ecloop)'; \
	time ./ecloop/ecloop add -f ecloop/data/btc-bw-hash -t 4 -r 8000:fffffff; \
	"
