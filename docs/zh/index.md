---
# https://vitepress.dev/reference/default-theme-home-page
layout: home

hero:
  name: "ilias"
  text: "现代化轻量级C++异步运行时"
  tagline: 使用协程和结构化并发重塑C++异步编程
  actions:
    - theme: brand
      text: 什么是 ilias?
      link: zh/guides/what-is-ilias
    - theme: alt
      text: 快速开始
      link: zh/guides/quick-start
    - theme: alt
      text: API 参考
      link: zh/dev/api

features:
  - title: 轻量级
    details: 最小的核心依赖, 只依赖标准库和系统头文件
  - title: 现代化
    details: 基于完成的模型, Tokio 类似的丰富API, 内建异步取消与结构化并发支持
  - title: 简单
    details: 每条线程一个调度器的设计和简单易用的API, 大幅度降低心智负担
  - title: 互操作
    details: 支持和Qt互操作, 非常方便并入已有的代码和其他事件框架 (如libuv, Boost.Asio)
---

