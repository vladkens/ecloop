# ecloop

A high-performance, CPU-optimized tool for computing public keys on the secp256k1 elliptic curve, with features for searching compressed & uncompressed public keys and customizable search parameters.

[<img src="https://badgen.net/static/-/buy%20me%20a%20coffee/ff813f?icon=buymeacoffee&label" alt="donate" />](https://buymeacoffee.com/vladkens)

## Features

- 🍏 Fixed 256-bit modular arithmetic
- 🔄 Group inversion for point addition operation
- 🍇 Precomputed table for points multiplication
- 🔍 Search for compressed & uncompressed public keys (hash160)
- 🌟 Utilizes SIMD for optimized sha256 (uses SHA extensions, both ARM and Intel)
- 🍎 Works seamlessly on MacOS and Linux
- 🔧 Customizable search range and thread count for flexible usage


## Build

```sh
git clone https://github.com/vladkens/ecloop.git && cd ecloop
make build
```

Note: Build has been tested with `clang`. It may work with `gcc14`, but this has not been thoroughly tested. If anyone knows how to get this to work with gcc14 or earlier – I'd be happy to get a PR.

## Usage

```text
Usage: ecloop <cmd> [-t <threads>] [-f <filepath>] [-a <addr_type>] [-r <range>]

Compute commands:
  add             - search in given range with batch addition
  mul             - search hex encoded private keys (from stdin)

Compute options:
  -f <file>       - filter file to search (list of hashes or bloom fitler)
  -o <file>       - output file to write found keys (default: stdout)
  -t <threads>    - number of threads to run (default: 1)
  -a <addr_type>  - address type to search: c - addr33, u - addr65 (default: c)
  -r <range>      - search range in hex format (example: 8000:ffff, default all)
  -q              - quiet mode (no output to stdout; -o required)

Other commands:
  blf-gen         - create bloom filter from list of hex-encoded hash160
  bench           - run benchmark of internal functions
  bench-gtable    - run benchmark of ecc multiplication (with different table size)
```

### Example 1: Check keys in given range (sequential addition)

`-f` is filter file with hash160 to search. Can be list of hex encoded hashes (one per line) or bloom fitler (must have `.blf` extension). `-t` use 4 threads. `r` – start:end of search range. `-o` file where found keys should be saved (if not provided `stdout` fill be used). No `-a` option provided, so `c` (compressed) hash160 will be checked.

```sh
ecloop add -f data/btc-puzzles-hash -t 4 -r 800000:ffffff -o /tmp/found.txt
```

### Example 2: Check given privkeys list (multiply)

`cat privkeys.txt` – source of HEX encoded priv keys to search (can be file or generator program). `-f` – hash160 to search as bloom filter (can have false positive results, but has a much smaller size; eg. all BTC addresses ever used have size ~6GB). `-a` – what type of hash160 to search (`c` – compressed, `u` – uncopressed, `cu` check both). `-t` use 8 threads.

```sh
cat privkeys.txt | ecloop mul -f data/btc-puzzles.blf -a cu -t 4
```

`ecloop` can also take a raw word list and automatically hash it with sha256. Use `-raw` flag to it.

```sh
cat wordlist.txt | ecloop mul -f data/btc-puzzles.blf -a cu -t 4 -raw
```

### Example 3: Generating bloom filter

`cat` reads the list of hex-encoded hash160 values from a file. `-n` specifies the number of entries for the Bloom filter (count of hashes). `-o` defines the output where to write filter (`.blf` extension requried).

Bloom filter uses p = 0.000001 (1 in 1,000,000 false positive). You can adjusting this option by playing with `n`. See [Bloom Filter Calculator](https://hur.st/bloomfilter/?n=1024&p=0.000001&m=&k=20). List of all addressed can be found [here](https://bitcointalk.org/index.php?topic=5265993.0). 

```sh
cat data/btc-puzzles-hash | ecloop blf-gen -n 1024 -o /tmp/test.blf
```

Then created bloom filter can be used in `ecloop` as filter:
```sh
ecloop add -f /tmp/test.blf -t 4 -r 8000:ffffff
```

Note: Bloom filter works with both `add` and `mul` commands.

## Benchmark

Get performance of different function for single thread:

```sh
ecloop bench
```

Should print output like:

```sh
     _ec_jacobi_add1: 6.52M it/s ~ 0.92s
     _ec_jacobi_add2: 5.26M it/s ~ 1.14s
     _ec_jacobi_dbl1: 5.42M it/s ~ 1.11s
     _ec_jacobi_dbl2: 7.57M it/s ~ 0.79s
       ec_jacobi_mul: 0.02M it/s ~ 0.57s
       ec_gtable_mul: 0.29M it/s ~ 1.73s
       ec_affine_add: 0.30M it/s ~ 1.67s
       ec_affine_dbl: 0.30M it/s ~ 1.69s
   _fe_modinv_binpow: 0.20M it/s ~ 0.51s
   _fe_modinv_addchn: 0.31M it/s ~ 0.32s
              addr33: 4.95M it/s ~ 1.01s
              addr65: 4.41M it/s ~ 1.14s
```

## Disclaimer

This project is written to learn the math over elliptic curves in cryptocurrencies. Functionality as a search for Bitcoin Puzzles is added as a real-world use case.

## Cudos to
- [sharpden](https://github.com/sharpden)

## See also

- [ryancdotorg/brainflayer](https://github.com/ryancdotorg/brainflayer)
- [albertobsd/keyhunt](https://github.com/albertobsd/keyhunt)
- [JeanLucPons/VanitySearch](https://github.com/JeanLucPons/VanitySearch)
