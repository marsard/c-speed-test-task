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

- libcurl
- `speedtest_server_list.json` (must be in same directory as main executable)
