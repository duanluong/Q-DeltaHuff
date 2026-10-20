# Project Architecture

Q-HUFDEL has three layers:

1. **Python encoder**: reads two models and creates an offline package.
2. **Portable C decoder**: parses the package and returns decoded values.
3. **Application/host layer**: supplies storage, applies deltas, validates
   checksums, and decides when a model is safe to activate.

The decoder does not open files, know device storage, choose a base model, apply updates, or enforce update policy.

## Repository layout

| Path                                                                            | Purpose                            |
| ------------------------------------------------------------------------------- | ---------------------------------- |
| [encoder/](../encoder/)                                                         | Python package-generation tools    |
| [decoder/include/](../decoder/include/)                                         | Public C headers and types         |
| [decoder/src/](../decoder/src/)                                                 | Decoder and bit-management sources |
| [decoder/tests/](../decoder/tests/)                                             | Unity host tests                   |
| [third-party/crc32/](../third-party/crc32/)                                     | CRC-32 implementation              |
| [experiments/examples/deployment/cli/](../experiments/examples/deployment/cli/) | Windows host application           |
| [experiments/models/](../experiments/models/)                                   | Example model assets               |
| [tools/code_format/](../tools/code_format/)                                     | Formatting scripts                 |

## Data flow

```text
current model + target model
          -> Python encoder -> Q-HUFDEL package
          -> C decoder -> decoded values
          -> application -> restored model and checks
```

The encoder accepts a C header containing one initialized byte array or a binary file. It prepares differences, selects Huffman symbols, and writes the header, table, payload, padding, and checksums. It is an offline tool and does not provide device update logic.

`decoder.c` verifies the magic, reads the header, rebuilds the canonical table, and decodes full or chunked payloads. `bit_manager.c` supplies the
MSB-first bit operations. The public API is in [decoder.h](../decoder/include/decoder.h), with package definitions in [Types.h](../decoder/include/Types.h).

The host CLI replaces device storage with file I/O: it reads models and a package, calls the decoder, applies decoded values, checks the result, and can write the restored model.

## Setup

Use Python 3.8 or newer:

```powershell
python -m pip install -r encoder/requirements.txt
```

The GUI also requires Tkinter and a desktop session.

Generate a package from the repository root:

```powershell
python encoder/cli.py NAME current.h CURRENT target.h TARGET
```

Inputs must contain one initialized byte array. Use `--output`/`-o` for a different destination, `--json-report` for statistics, and `python encoder/cli.py --help` for all options.

Run the GUI with:

```powershell
python encoder/gui.py
```

Build the Windows host CLI from its directory with CMake 3.16+, a C11 compiler, and Ninja or MinGW:

```powershell
cmake -S . -B build -G Ninja
cmake --build build --target qhufdel_cli
```

The CLI accepts raw model files or supported C headers; see its README for the command options.
