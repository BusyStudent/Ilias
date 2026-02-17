---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "ilias"
  text: "A Modern, Lightweight C++ Asynchronous Runtime"
  tagline: Reshaping C++ Asynchronous Programming with Coroutines and Structured Concurrency
  actions:
    - theme: brand
      text: What is ilias?
      link: /guides/what-is-ilias
    - theme: alt
      text: Quick Start
      link: /guides/quick-start
    - theme: alt
      text: API Reference
      link: /dev/api

features:
  - title: Lightweight
    details: Minimal core dependencies, relying only on the standard library and system headers.
  - title: Modern
    details: Completion-based model, rich Tokio-like APIs, and built-in support for asynchronous cancellation and structured concurrency.
  - title: Simple
    details: One-scheduler-per-thread design and easy-to-use APIs significantly reduce cognitive load.
  - title: Interoperable
    details: Supports interoperability with Qt, making it very easy to integrate into existing code and other event frameworks (such as libuv, Boost.Asio).
---