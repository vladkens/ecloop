# ecloop

A high-performance, CPU-optimized tool for computing public keys on the secp256k1 elliptic curve, with features for searching compressed & uncompressed public keys and customizable search parameters.

[<img src="https://badgen.net/static/-/buy%20me%20a%20coffee/ff813f?icon=buymeacoffee&label" alt="donate" />](https://buymeacoffee.com/vladkens)

## Features

- 🔐 Fixed 256-bit modular arithmetic
- 🔄 Group inversion for point addition operation
- 🔍 Search for compressed & uncompressed public keys (hash160)
- 💡 Utilizes SIMD for optimized sha256 (uses SHA extensions, both ARM and Intel)
- 💻 Works seamlessly on MacOS and Linux
- 🔧 Customizable search range and thread count for flexible usage


## Build & Run

```sh
git clone https://github.com/vladkens/ecloop.git
cd ecloop
make
./ecloop -i data/btc-puzzles-hash -t 1 -r 800000:ffffff
```


## Usage

The program accepts several command-line arguments:

- `-i <filepath>`: Specifies the file with hashes to search
- `-t <threads>`: Specifies the number of threads to run (default is `1`)
- `-r <range>`: Specifies the search range in hex format (example: `8000:ffff`, default is full range)
- `-b`: Checks both compressed and uncompressed (default is compressed)
- `-u`: Checks only uncompressed

```sh
./ecloop -i <filepath> [-t <threads>] [-r <range>] [-b] [-u]
```

## Benchmark

Run:

```sh
make bench
```

Should print output like:

```sh
     _ec_jacobi_add1: 6.55M it/s ~ 1.53 s
     _ec_jacobi_add2: 5.46M it/s ~ 1.83 s
     _ec_jacobi_dbl1: 5.59M it/s ~ 1.79 s
     _ec_jacobi_dbl2: 7.84M it/s ~ 1.28 s
       ec_jacobi_mul: 0.02M it/s ~ 4.30 s
       ec_affine_add: 0.31M it/s ~ 1.63 s
       ec_affine_dbl: 0.30M it/s ~ 1.64 s
   _fe_modinv_binpow: 0.05M it/s ~ 2.14 s
   _fe_modinv_addchn: 0.04M it/s ~ 2.46 s
```

## Disclaimer

This project is written to learn the math over eleptic curves in cryptocurrencies. Functionality as a search for Bitcoin Puzzles is added as a real-world use case.
