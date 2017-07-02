# HexVASN
HEXVASAN : Venerable Variadic Vulnerabilities Vanquished 
 

1) To build HexVASAN, follow the following commands:

```
git clone https://github.com/HexHive/HexVASAN_Usenix.git
cd HexVASAN_Usenix
mkdir build && cd build
../cmake_script.sh
ninja
```
2) To test:

```
$BUILD_DIR/bin/clang++ test.cpp -fsanitize=vasan 
./a.out
```

3) To Create Log files set three environment variables: These paths are to create log files for callsites and variadic functions as well as errors if any

```
export VASAN_LOG_PATH="/tmp/vasan/vfun/"
export VASAN_ERR_LOG_PATH="/tmp/vasan/errors/"
export VASAN_C_LOG_PATH="/tmp/vasan/callsite/"
```
