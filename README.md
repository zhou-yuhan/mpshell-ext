# Mpshell Extended

Extend [`mpshell`](https://github.com/ravinet/mahimahi/releases/tag/old%2Fmpshell_scripted) from [mahimahi](https://github.com/ravinet/mahimahi) with support for:

- Arbitrary number of links (net interfaces)
- AQM schemes (DropHead, DropTail, Codel, PIE) instead of infinite queue only

## Build
```
./autogen.sh
./configure
make
sudo make install
```

## Usage
```
mpshell config_file program
```

`config_file` is a configuration file in JSON format listing the link number and properties for each link. See `example-config.json`.