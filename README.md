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

## Example output

```
 ./main -a
Detecting location...
Location detected: Lithuania, Kaunas

Finding best server...
Found 5873 servers in list
Best server selected: speedtest.litnet.lt:8080

Testing download speed from speedtest.litnet.lt:8080...
Download progress: 29.54 / 30.16 MB (98.0%)...
Downloaded 30.16 MB in 3.29 seconds

Testing upload speed to speedtest.litnet.lt:8080...
Upload progress: 30.00 / 30.00 MB (100.0%)...
Uploaded 30.00 MB in 6.24 seconds

Results:
========
Download speed: 77.00 Mbps
Upload speed: 40.31 Mbps
Server: speedtest.litnet.lt:8080
Location: Lithuania
```
