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
make
```

## Usage

```text
Usage: ecloop <cmd> [-t <threads>] [-f <filepath>] [-a <addr_type>] [-r <range>]

Commands:
  add - search in given range with batch addition
  mul - search hex encoded private keys (from stdin)

Options:
  -t <threads>     - number of threads to run (default: 1)
  -f <filepath>    - filter file to search (list of hashes or bloom fitler)
  -a <addr_type>   - address type to search: c - addr33, u - addr65 (default: c)
  -r <range>       - search range in hex format (example: 8000:ffff, default all)
  -o <fielpath>    - output file to write found keys (default: stdout)
```

### Example 1: Check keys in given range (sequential addition)

`-f` is filter file with hash160 to search. Can be list of hex encoded hashes (one per line) or bloom fitler (must have `.blf` extension). `-t` use 4 threads. `r` – start:end of search range. `-o` file where found keys should be saved (if not provided `stdout` fill be used). No `-a` option provided, so `c` (compressed) hash160 will be checked.

```sh
ecloop add -f data/btc-puzzles-hash -t 4 -r 800000:ffffff -o /tmp/found.txt
```

### Example 2: Check given privkeys list (multiply)

`echo privkeys.txt` – source of HEX encoded priv keys to search (can be file or generator program). `-f` – hash160 to search as bloom filter (can have false positive results, but has a much smaller size; eg. all BTC addresses ever used have size ~6GB). `-a` – what type of hash160 to search (`c` – compressed, `u` – uncopressed, `cu` check both). `-t` use 8 threads.

```sh
echo privkeys.txt | ecloop mul -f data/btc-puzzles.blf -a cu -t 8
```

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

## See also

- [ryancdotorg/brainflayer](https://github.com/ryancdotorg/brainflayer)
- [albertobsd/keyhunt](https://github.com/albertobsd/keyhunt)
