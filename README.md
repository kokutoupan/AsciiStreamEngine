# ascii-network-renderer

サーバーサイドでasciiでレンダリングするシステムです。ncと独自クライントで接続できます。


## Build

```sh
cmake -S . -B build
cmake --build build
```

## run

```sh
./build/server
./build/client -p 12345
```
