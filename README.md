Bitcoin Core CloudABI experiment
=====================================

Example configure/build (my development platform for this is FreeBSD 11.0 with 
the [appropriate packages](https://nuxi.nl/cloudabi/freebsd/) installed), as well as:

```bash
pkg install x86_64-unknown-cloudabi-argdata x86_64-unknown-cloudabi-boost x86_64-unknown-cloudabi-leveldb x86_64-unknown-cloudabi-libevent x86_64-unknown-cloudabi-libressl x86_64-unknown-cloudabi-zeromq
```

Then set up the build system using:

```bash
./autogen
./configure --host=x86_64-unknown-cloudabi \
    --disable-wallet \
    --with-boost-libdir=/usr/local/x86_64-unknown-cloudabi/lib \
    --without-utils \
    ac_cv_c_bigendian=no \
    CPPFLAGS="-DCLOUDABI -DBOOST_DISABLE_ASSERTS -fno-sanitize=safe-stack" \
    LDFLAGS="-fno-sanitize=safe-stack"
gmake -j10
```

Example `bitcoind.yaml` configuration:
```yaml
%TAG ! tag:nuxi.nl,2015:cloudabi/
---
datadir: !file
  path: /home/user/.bitcoin
console: !fd stdout
rpc: !socket
  bind: 127.0.0.1:8332
p2p: !socket
  bind: 0.0.0.0:8333
zmq: !socket
  bind: 0.0.0.0:28332
args:
  printtoconsole: 1
  rpcuser: "test"
  rpcpassword: "test"
  connect: 0
  debug: ["rpc","http","libevent"]
  listenonion: 0
  rest: true
  zmqpubhashblock: "ipc:///"
  #  zmqpubhashtx: "ipc:///"
  #  zmqpubrawblock: "ipc:///"
  #  zmqpubrawtx: "ipc:///"

```

Invoke `bitcoind` with
```
cloudabi-run src/bitcoind < ~/bitcoind.yaml
```

Running the tests
-------------------

Create a `bitcoin-test.yaml`
```
%TAG ! tag:nuxi.nl,2015:cloudabi/
---
terminal: !fd stdout
arguments: [--color_output]
tempdir: !file
  path: /tmp/test
```

Invoke the tests with
```
cloudabi-run src/test/test_bitcoin < ~/bitcoin-test.yaml
```

Currently, the following tests are failing with the default compile options:

- `key_tests/key_test1`
- `script_tests/script_build`

It looks like these failures are caused by `-fsanitize=safe-stack` which is enabled by default on
CloudABI. Passing `-fno-sanitize=safe-stack` to the compiler and linker makes all tests pass.
This points in the direction of a compiler bug in the specific version of clang4.0-devel.
