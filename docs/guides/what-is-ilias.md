---
outline: deep
---

# What is ilias?

ilias is a modern, completion-based asynchronous runtime. It aims to provide developers with a smooth development experience and performance similar to Tokio, while also ensuring interoperability with other asynchronous ecosystems.

## Why Reinvent the Wheel?

Previously, when writing network-related code for my Qt applications, I fell into callback hell.

Every time I needed to handle a network request, I had to write a callback function and then connect it to a Qt signal.
I found that as the logic became more complex, this callback hell became difficult to maintain, and the lifecycle was almost uncontrollable, requiring the use of `shared_ptr`. So, I turned my attention to existing coroutine libraries.

`cppcoro` is a pioneer of C++20 stackless coroutines, but it hasn't been maintained for a long time, and its networking part only supports Windows.

`asio` is a long-standing asynchronous library, but its coroutine cancellation forces a reliance on exceptions. I prefer not to use exceptions for cancellation because an exception crossing an await point requires a rethrow + catch, which I find to be rather inefficient.

`folly` and `seastar` are advanced, but they are heavyweight.

So I turned my attention to Rust and saw Tokio. I found its API design to be quite good, with a rich ecosystem. However, in practice, I found that its cancellation and a few other aspects didn't quite suit my taste; for example, the default `spawn` requires `Send + Sync`. Then, I came across `std::execution` and discovered that its cancellation design is excellent and safe, featuring structured concurrency. So, I decided to act on it. After referencing the concepts of Tokio and `stdexec`, ilias was born. I want to make asynchronous programming painless for myself, to be able to write fast, safe programs in C++ that are compatible with existing ecosystems.

## Design Philosophy

- **One scheduler per thread:** The simplest model, easy to integrate into other asynchronous ecosystems, and inherently lock-free.
- **Completion-based + Asynchronous Cancellation:** Cancellation is not a direct `Drop`. It sends a cancellation signal and then asynchronously waits for the underlying cancellation to complete.
