# ecloop

A high-performance, CPU-optimized tool for computing public keys on the secp256k1 elliptic curve. It includes features for searching both compressed and uncompressed public keys, with customizable search parameters.

[<img src="https://badges.ws/badge/-/buy%20me%20a%20coffee/ff813f?icon=buymeacoffee&label" alt="donate" />](https://buymeacoffee.com/vladkens)

## Features

- 🍏 Fixed 256-bit modular arithmetic
- 🔄 Group inversion for point addition operations
- 🍇 Precomputed tables for point multiplication
- 🔍 Search for compressed and uncompressed public keys (hash160)
- 🌟 Accelerated SHA-256 with SHA extension (both ARM and x86)
- 🚀 Accelerated RIPEMD-160 using SIMD (NEON for ARM, AVX2 for x86)
- 🎲 Random search within customizable bit ranges
- 🍎 Works seamlessly on macOS and Linux
- 🔧 Customizable search range and thread count for flexible usage

## Build

```sh
git clone https://github.com/vladkens/ecloop.git && cd ecloop
make build
```

_\* On macOS, you may need to run `xcode-select --install` first._

By default, `cc` is used as the compiler. Using `clang` may produce [faster code](https://github.com/vladkens/ecloop/issues/7) than `gcc`. You can explicitly specify the compiler for any `make` command using the `CC` parameter. For example: `make add CC=clang`.

Also, verify correctness with the following commands (some compiler versions may have issues with built-ins used in the code):

```sh
make add # should found 9 keys
make mul # should found 1080 keys
```

## Usage

```text
Usage: ./ecloop <cmd> [-t <threads>] [-f <filepath>] [-a <addr_type>] [-r <range>]

Compute commands:
  add             - search in given range with batch addition
  mul             - search hex encoded private keys (from stdin)
  rnd             - search random range of bits in given range

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

### Quick Start for Bitcoin Puzzles

For a quick start with Bitcoin Puzzles, there are preconfigured make commands. Simply run them, and `ecloop` will start searching for the puzzle in random mode. For example, use `make 71` for puzzle 71, `make 72` for puzzle 72, and so on. If you are lucky and find a key, the result will be saved in the `found_N.txt` file. Shortcuts available for: `28`, `32`, `36`, `71`, `73`, `74`, `75`, `76`, `77`, `78`, `79`.

### Check keys in a given range (sequential addition)

```sh
./ecloop add -f data/btc-puzzles-hash -t 4 -r 800000:ffffff -o /tmp/found.txt
```

- `-f` is a filter file with hash160 values to search for. It can be a list of hex-encoded hashes (one per line) or a Bloom filter (must have a `.blf` extension).
- `-t` sets the number of threads (e.g., 4).
- `r` defines the start:end of the search range.
- `-o` specifies the file where found keys will be saved (if not provided, `stdout` will be used).
- No `-a` option is provided, so only `c` (compressed) hash160 values will be checked.

### Check a given list of keys (multiplication)

```sh
cat privkeys.txt | ./ecloop mul -f data/btc-puzzles.blf -a cu -t 4
```

- `cat privkeys.txt` – the source of HEX-encoded private keys to search (can be a file or a generator program).
- `-f` – a Bloom filter containing hash160 values to search for (may produce false positives but has a much smaller size; for example, all BTC addresses ever used take up ~6 GB).
- `-a` – specifies which type of hash160 to search: `c` for compressed, `u` for uncompressed, `cu` to check both.
- `-t` – sets the number of threads (e.g., 4).

`ecloop` can also take a raw word list and automatically hash each word with SHA-256. Use the `-raw` flag to enable this:

```sh
cat wordlist.txt | ./ecloop mul -f data/btc-puzzles.blf -a cu -t 4 -raw
```

### Random Search

The `rnd` command allows you to search random bit ranges within a specified range (by default, the entire curve space). This mode is useful for exploring random subsets of the keyspace.

#### Example 1: Random Search with Default Offset and Size

```sh
./ecloop rnd -f data/btc-puzzles-hash -o ./found.txt -a cu -q
```

This command performs a random search across the entire keyspace, checking both compressed and uncompressed addresses (`-a cu`), and saves the results to `found.txt`. Quiet mode is enabled (`-q`).

A random bit offset will be used, with a 32-bit range per iteration (4.2M keys).

#### Example 2: Random Search with Custom Offset and Size

```sh
./ecloop rnd -f data/btc-puzzles-hash -d 128:32
```

This command searches a 32-bit range with a 128-bit offset (`-d 128:32`). It will execute a search with a random base key on each large iteration. Bits from `offset` to `offset + size` will be dynamic. For example:

```txt
iter1: d33abfe4b9152c08 7176d067XXXXXXXX 27d4419e6969a205 4d1deb10e4929621
iter2: 1b354d3094405c2f bf8f5c15XXXXXXXX 46804248255476e9 800f26edd71ad0d7
```

X represents dynamic bits; other bits are randomly generated during large iterations.

_Note: You can also combine random search with `-r` param for shorter ranges._

### Generating bloom filter

```sh
cat data/btc-puzzles-hash | ./ecloop blf-gen -n 1024 -o /tmp/test.blf
```

- `cat` reads the list of hex-encoded hash160 values from a file.
- `-n` specifies the number of entries for the Bloom filter (the number of hashes).
- `-o` defines the output file where the filter will be written (the `.blf` extension is required).

The Bloom filter uses p = 0.000001 (1 in 1,000,000 false positives). You can adjust this option by modifying `n`. See the [Bloom Filter Calculator](https://hur.st/bloomfilter/?n=1024&p=0.000001&m=&k=20). A list of all addresses can be found [here](https://bitcointalk.org/index.php?topic=5265993.0).

Created Bloom filter then can be used in `ecloop` as a filter:

```sh
./ecloop add -f /tmp/test.blf -t 4 -r 8000:ffffff
```

_Note: Bloom filter works with all search commands (`add`, `mul`, `rnd`)._

## Benchmark

Get the performance of different functions for a single thread:

```sh
./ecloop bench
```

Should print output like this:

```sh
     _ec_jacobi_add1: 6.70M it/s ~ 0.90s
     _ec_jacobi_add2: 5.44M it/s ~ 1.10s
     _ec_jacobi_dbl1: 5.47M it/s ~ 1.10s
     _ec_jacobi_dbl2: 7.81M it/s ~ 0.77s
       ec_jacobi_mul: 0.02M it/s ~ 0.55s
       ec_gtable_mul: 0.32M it/s ~ 1.54s
       ec_affine_add: 0.31M it/s ~ 1.63s
       ec_affine_dbl: 0.31M it/s ~ 1.63s
   _fe_modinv_binpow: 0.20M it/s ~ 0.50s
   _fe_modinv_addchn: 0.31M it/s ~ 0.32s
              addr33: 4.84M it/s ~ 1.03s
              addr65: 4.27M it/s ~ 1.17s
```

_Note: This benchmark is run on a MacBook Pro M2._

## Build on Windows with WSL

Here are the steps I followed to run `ecloop` on Windows:

1. Open PowerShell
2. Run `wsl --install`
3. Restart Windows.
4. Run `wsl --install Ubuntu` (this command hung when I tried it, so I continued in a new tab)
5. Run `wsl -d Ubuntu`
6. Run: `sudo apt update && sudo apt install -y build-essential git clang`
7. Run `cd ~ && git clone https://github.com/vladkens/ecloop.git && cd ecloop`
8. Run `make build`

If no errors appear, `ecloop` has been compiled successfully and is ready to use. For example, you can run a benchmark with: `./ecloop bench`.

## Performance compare

Tests were done on an Intel N100.

### Single thread

```sh
> time ./keyhunt -m rmd160 -f ../ecloop/data/btc-puzzles-hash -r 8000:fffffff -t 1 -n 16777216
3m53s ~  1.15 MKeys/s

> time ./ecloop add -f data/btc-puzzles-hash -t 1 -r 8000:fffffff
1m06s ~  4.09 MKeys/s
```

### Multiple threads

```sh
> time ./keyhunt -m rmd160 -f ../ecloop/data/btc-puzzles-hash -r 8000:fffffff -t 4 -n 16777216
1m31s ~  2.95 MKeys/s

> time ./ecloop add -f data/btc-puzzles-hash -t 4 -r 8000:fffffff
0m25s ~ 10.73 MKeys/s
```

## Disclaimer

This project was created to explore the mathematics behind elliptic curves in cryptocurrencies. The functionality for searching Bitcoin Puzzles was added as a real-world use case.

## Donations

If you find this project useful, consider supporting its development:

- **BTC**: `bc1q4c3mxpm50awx9qaprx54k5x3t5m9ex658yzk4j`
- **ETH**: `0x4DF8E04C5AC0b06fb9938581D8a1732D5A78703E`
- **SOL**: `4r3CeYxvwJa1btZudmLpHeu2yzeudRw4UTqMUZScD63j`
- **XMR**: `85yjYN1sU3sgGFEkdqLKKxdiQwzQjqyhT74m5j4xwmKqHYfensRMjJrB1HvE9H6R6G5wG938KDpkJLum6GQd5q5yTTk8uhj`
- [Buy Me a Coffee](https://buymeacoffee.com/vladkens)

Thank you for your support!

## Cudos to

- [sharpden](https://github.com/sharpden)

## See also

- Articles about ecloop in [my blog](https://vladkens.cc/tags/ecloop/)
- [ryancdotorg/brainflayer](https://github.com/ryancdotorg/brainflayer)
- [albertobsd/keyhunt](https://github.com/albertobsd/keyhunt)
- [JeanLucPons/VanitySearch](https://github.com/JeanLucPons/VanitySearch)
