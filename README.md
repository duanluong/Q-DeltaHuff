# Q-HUFDEL: Quantized model compression using Delta and Huffman coding

Q-HUFDEL is a lossless byte-compression format for delivering model data over the air. It combines an optional byte-wise delta transform, canonical Huffman coding, and an escape symbol for values that are more efficient to store as raw bytes.

The repository contains:

- A Python encoder and package-processing backend for creating and inspecting compressed packages.
- A portable C decoder for streaming decoded values from an application-owned storage device.
- Host examples, tests, and documentation for integrating the decoder into an MCU project.

Compression operates on a byte sequence. It does not perform model parsing or quantization; any quantization must happen before compression. In delta mode, the encoded values are differences between the target and base byte sequences.

## Documentation

Start with the [architecture and getting started guide](docs/architecture.md) for the repository layout, build instructions, Python tools, and host demo.

| Guide | Description |
| --- | --- |
| [Compression algorithm](docs/algorithm.md) | Transform, symbol selection, canonical Huffman coding, encoding, and reconstruction. |
| [Project architecture and getting started](docs/architecture.md) | Repository layout, build flow, host demo, encoder, and Python API. |
| [MCU integration](docs/mcu-integration.md) | Add the C decoder to firmware, implement storage callbacks, and manage decoded output. |
| [Binary package format](docs/patch-format.md) | Package layout, header, Huffman table, payload, padding, and checksums. |

## Repository Layout

| Path | Purpose |
| --- | --- |
| [`encoder/`](encoder/) | Python compression, decompression, package generation, and GUI tools. |
| [`decoder/`](decoder/) | Portable C decoder library and tests. |
| [`experiments/`](experiments/) | Example applications, deployment files, and model experiments. |
| [`third-party/`](third-party/) | Third-party dependencies used by the project. |
| [`tools/`](tools/) | Formatting and repository maintenance scripts. |

## Quick Start

For the complete setup and build commands, see the [architecture guide](docs/architecture.md).
For firmware deployment, see the [MCU integration guide](docs/mcu-integration.md).
