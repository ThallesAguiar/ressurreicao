# Reverse-engineering configurations

This directory preserves the disassembly inputs used to recover behavior that
is implemented by the game adapters. They are reproducibility evidence, not
runtime configuration for `saturn-sfx`.

The YAML schema is consumed by `rust-dis` from the
[saturn-splitter](https://github.com/sozud/saturn-splitter) project. These
configurations were last verified at saturn-splitter commit
`719440857e29dc1775624aff4ce69f3f4d33e60c`.

Build and run that tool with:

```sh
cargo build --release --manifest-path /path/to/rust-dis/Cargo.toml
/path/to/rust-dis/target/release/rust-dis research/scorcher/lort.yaml
/path/to/rust-dis/target/release/rust-dis research/scorcher/sddrvs.yaml
```

The configurations use fixed `/tmp` paths as disposable workspace
conventions. Copy the required inputs to those paths before running the
splitter, or edit a local copy of the YAML. Generated assembly, linker scripts,
symbols, and C output are intentionally not committed.

## Scorcher

### `scorcher/lort.yaml`

This configuration splits the retail USA `LORT.BIN`, loaded by the game at
SH-2 address `0x06004000`. The verified input is:

```text
path:    /tmp/scorcher-all/LORT.BIN
size:    1032192 bytes
SHA-256: 36501df4b3915ceae21c17b88b8a86a8c57aefadcdf0ac8866299a6bf21a344c
```

It preserves the boundaries used to examine the sound loader, dynamically
loaded sound-task wrapper, and music selector. The key routines recovered
from this image are the MUS file selector at `0x060EB934`, file loader at
`0x0600AE78`, and sound-task wrapper at `0x0600B0A2`.

Splitter output is written under `/tmp/scorcher-dis`.

### `scorcher/sddrvs.yaml`

This configuration is for a captured 1 MiB low-work-RAM image, not a file
stored directly on the disc. The capture covers physical addresses
`0x00200000..0x002FFFFF` after Scorcher has loaded `SDDRVS.TSK`; the verified
capture was taken at frame 1800, when the sound-task vtable was installed.

```text
path:    /tmp/scorcher-runtime.bin.lowram
size:    1048576 bytes
SHA-256: 992a9de648d9152cd7d7dfceb5982be109a382444a17bc775b308654872a98c0
```

It preserves the split used to recover the MUS loader at `0x00200ED0`, volume
wrapper at `0x002020C8`, and area resolver at `0x00202798`. Those routines
established the MUS header fields, upload ordering, area-ready handshake, and
startup command sequence reproduced by the Scorcher adapter.

Splitter output is written under `/tmp/scorcher-sddrvs-dis`.
