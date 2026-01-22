# Speed test

Internet download/upload speed testing tool using C, libcurl, getopt, and cJSON.

## Build

```bash
make
```

## Usage

```
Usage: ./main [OPTIONS]

Options:
  -a, --automated          Run full automated test
  -d, --download <server>  Test download speed with specified server
  -u, --upload <server>    Test upload speed with specified server
  -s, --server             Find best server by location
  -l, --location           Detect user location
  -h, --help               Show this help message
```

## Requirements

- C compiler
- make
- libcurl development headers
- `speedtest_server_list.json` (must be in same directory as main executable)

### Installing libcurl

**Debian/Ubuntu:**
```bash
sudo apt install libcurl4-openssl-dev
```

**Fedora:**
```bash
sudo dnf install libcurl-devel
```

**Arch Linux:**
```bash
sudo pacman -S curl
```
