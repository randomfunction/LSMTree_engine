# LSM-Tree Key-Value Storage Engine

This project is a simplified, single-threaded LSM-Tree storage engine written in C++.

I built it step by step to understand how a storage engine works behind the scenes. The goal was not to make it ultra fast or production-ready. The goal was to keep it clean, readable, and easy to learn from.

It supports:

- in-memory writes using a `MemTable`
- durable writes using a `Write-Ahead Log (WAL)`
- flushing sorted data to disk as `SSTable` files
- point reads from both memory and disk
- tombstones for delete operations
- Bloom Filters to skip unnecessary SSTable reads
- basic size-tiered compaction
- recovery after restart using the WAL

## Project Structure

```text
include/
  memtable.h
  wal.h
  sstable.h
  bloom_filter.h
  lsm_engine.h

src/
  memtable.cpp
  wal.cpp
  sstable.cpp
  bloom_filter.cpp
  lsm_engine.cpp
  main.cpp
  readme_test.cpp
```

## How It Works

### 1. Write Path

When a key is written:

1. The engine first appends the operation to the WAL file.
2. Then it updates the MemTable in memory.
3. If the MemTable reaches a small threshold, it is flushed to disk as an SSTable.

This order matters because the WAL helps recover data if the program stops unexpectedly.

### 2. Read Path

When a key is requested:

1. The engine checks the MemTable first.
2. If the key is not there, it checks SSTables from newest to oldest.
3. Before opening a full SSTable lookup, it checks the Bloom Filter.

### 3. Delete Path

Delete is handled using a tombstone marker.

That means the engine does not immediately remove the key from disk. It writes a special delete marker, and compaction later cleans old data.

### 4. Recovery

On startup, the engine:

1. reads the manifest file to find active SSTables
2. replays the WAL
3. rebuilds the MemTable in memory

## Build Instructions

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

## Run the Full Demo

This runs the larger end-to-end flow with:

- many inserts
- flushes
- compactions
- crash simulation
- restart recovery

```bash
./build/lsmtree_demo
```

## Run the Short README Test

This is the best one to use for a terminal screenshot in the README because the output is short and neat.

```bash
./build/lsmtree_readme_test
```

Expected output:

```text
LSM TREE QUICK TEST
-------------------
Run 1
  apple  -> red
  banana -> Not Found
  date   -> brown
Run 2 (after restart)
  apple  -> red
  banana -> Not Found
  carrot -> orange
  date   -> brown
Files
  - 00001.sst
  - manifest.txt
  - active.wal
```

## Screenshot

Paste your terminal screenshot here.

Example markdown:

```md
![Terminal Output](./your-screenshot-file.png)
```

If you want, replace `your-screenshot-file.png` with the real image file name after you add it to this folder.

## Important Files

- `src/main.cpp`  
  Full integration demo with more writes, flushes, compactions, and recovery.

- `src/readme_test.cpp`  
  Small clean test with short output for screenshots.

- `src/lsm_engine.cpp`  
  Main coordinator logic for write, read, flush, recovery, and compaction.

- `src/sstable.cpp`  
  Handles SSTable file writing, reading, index loading, and Bloom Filter loading.

- `src/wal.cpp`  
  Handles WAL append and replay.

## Current Limitations

- single-threaded only
- simple manifest format
- simple compaction strategy
- no range scan API
- no block cache
- no checksum validation
- no levels like a full production LSM engine

## Why I Made It This Way

I kept the code simple on purpose:

- `std::map` keeps keys sorted and makes flushing easy
- plain C++ standard library containers keep the code easy to follow
- the file formats are simple enough to inspect and debug
- the project is structured so each part has a clear job

## Future Improvements

- add a cleaner benchmark mode with timing numbers
- support range scans and iterators
- add checksums to detect corrupted files
- add leveled compaction
- support configurable directories and thresholds from command line

## Author Notes

This project is meant to be educational first. If you read through the source files in order, it should feel like a small storage engine that you can actually understand without needing a huge codebase.
# LSMTree_engine
