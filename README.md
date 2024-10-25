# Thread Pool Implementation in C/C++

## Requirements
1. All working threads must be pre-instantiated and ready to work.
2. Task adding and execution must be decoupled.
3. Task is guaranteed to be executed.
4. Thread pool needs to be thread-safe.
5. The number of working threads can be dynamically adjusted based on the task count in queue.
6. Shutdown feature is supported.
