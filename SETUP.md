# C++ Setup Guide

This repo uses **C++17** (some examples bump to C++20 where it makes the design clearer). Pick either path below — CMake is recommended once you have more than a handful of examples.

---

## 1. Install the Toolchain

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential g++ cmake make gdb clang-format
```

Verify:

```bash
g++ --version       # expect >= 9
cmake --version     # expect >= 3.16
```
---

## 2. Build Everything (CMake)

```bash
# from repo root
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# run a specific example
./build/phase-1-foundations/01-oop-pillars/01-encapsulation/encapsulation
```

Every leaf directory with a `main.cpp` becomes its own executable, named after the directory. CMake's [`add_subdirectory`](./CMakeLists.txt) recursively picks them up — adding a new example is just *create folder, drop `main.cpp`, rerun cmake*.

```bash
# example: AddressSanitizer build
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j
```

---

## 3. Build a Single File (No CMake)

For quick experiments, compile directly:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -O0 -g main.cpp -o main
./main
```

Concurrency (Phase 5) needs the threads library:

```bash
g++ -std=c++17 -Wall -Wextra -pthread main.cpp -o main
```

### Recommended warning flags

Keep these on while practicing — they catch real bugs:

```
-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
-Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
```

---
