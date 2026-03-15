---
outline: deep
---

# API Overview

Ilias can be understood in six layers: runtime, tasks, I/O, networking, synchronization, and optional extensions. In day-to-day development, you usually only need a small subset of headers.

## Recommended entry headers

- `#include <ilias.hpp>`
- `#include <ilias/platform.hpp>`
- `#include <ilias/task.hpp>`
- `#include <ilias/io.hpp>`
- `#include <ilias/net.hpp>`
- `#include <ilias/fs.hpp>`
- `#include <ilias/sync.hpp>`
- `#include <ilias/tls.hpp>`
- `#include <ilias/process.hpp>`
- `#include <ilias/signal.hpp>`

## Module summary

| Module | Main APIs | Purpose |
| --- | --- | --- |
| platform | `PlatformContext`, `ilias_main` | Install the current-thread executor and start async programs |
| task | `Task<T>`, `spawn`, `whenAll`, `whenAny`, `TaskScope`, `TaskGroup<T>`, `Thread` | Write and organize coroutines |
| runtime | `Executor`, stop token, `this_coro::*` | Execution, cancellation, coroutine context access |
| io | `BufReader`, `BufWriter`, `BufStream`, `readAll`, `writeAll`, `getline` | Uniform stream-oriented I/O |
| net | `TcpStream`, `TcpListener`, `UdpSocket`, `AddressInfo` | Networking |
| fs | `File`, `OpenOptions` | File I/O |
| sync | `Mutex`, `Event`, `Semaphore`, `oneshot::channel<T>`, `mpsc::channel<T>()` | Synchronization and messaging |
| tls | `TlsContext`, `TlsStream<T>` | TLS client/server support |
| process | `Process::Builder`, `Process` | Async child processes |
| signal | `signal::ctrlC()` | Graceful console shutdown |

## Key public concepts

### `Task<T>` and `IoTask<T>`

- `Task<T>`: regular async result
- `IoTask<T>`: I/O result, effectively `Task<Result<T, std::error_code>>`

### Unified stream model

Many types expose similar operations:

- `read(MutableBuffer)`
- `write(Buffer)`
- `flush()`
- `shutdown()`
- `close()` / `cancel()`

That makes protocol code reusable across `TcpStream`, `TlsStream<T>`, `File`, and `BufStream<T>`.

### Structured concurrency

- `TaskScope`: parent-child lifetime management
- `TaskGroup<T>`: batch task result collection

### Cancellation model

Ilias uses stop tokens rather than making cancellation exception-only.

Common APIs:

- `this_coro::stopToken()`
- `this_coro::isStopRequested()`
- `this_coro::stopped()`
- `handle.stop()`

## Suggested reading order

1. [Quick Start](/guides/quick-start)
2. [What is ilias](/guides/what-is-ilias)
3. The public headers under `include/ilias/`
