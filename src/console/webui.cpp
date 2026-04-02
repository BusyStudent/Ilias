#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/capture.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/console.hpp>
#include <ilias/net.hpp>
#include <ilias/task.hpp>
#include <ilias/log.hpp>
#include <ilias/io.hpp>

#include <memory_resource>
#include <algorithm>
#include <ranges>
#include <vector>
#include <map>

// Resource by build system
extern "C" {
    // index.html
    extern const char _binary_index_html_start[];
    extern const char _binary_index_html_end[];

    // script.js
    extern const char _binary_script_js_start[];
    extern const char _binary_script_js_end[];

    // style.css
    extern const char _binary_style_css_start[];
    extern const char _binary_style_css_end[];
}

ILIAS_NS_BEGIN

// ═════════════════════════════════════════════════════════════════════
// Implementation
// ═════════════════════════════════════════════════════════════════════
struct TracingWebUi::Impl : public runtime::TracingSubscriber {
    ~Impl();

    using Clock = std::chrono::steady_clock;

    enum class EventKind : uint8_t {
        TaskSpawn, TaskComplete, Resume, Suspend, ChildBegin, ChildEnd
    };

    enum class TaskState : uint8_t {
        Spawned, Running, Suspended, Completed, Stopped
    };
    struct TaskRecord {
        uintptr_t id = 0;
        uintptr_t parentId = 0;
        bool hasParent = false;
        TaskState state = TaskState::Spawned;
        Clock::time_point createdAt {};
        Clock::time_point lastSeenAt {};
        Clock::time_point lastResumeAt {};
        std::chrono::nanoseconds totalBusy {};
        size_t resumes = 0;
        size_t suspends = 0;
        size_t childrenStarted = 0;
        size_t childrenCompleted = 0;
        bool stopRequested = false;
        bool stopped = false;
        std::string location; // The source location (file:line) of where the task was spawned
    };

    struct EventRecord {
        Clock::time_point at{};
        uintptr_t taskId = 0;
        uintptr_t parentId = 0;
        bool hasParent = false;
        std::string kind, state, name, location;
    };

    // ── TracingSubscriber ───────────────────────────────────────────
    auto onTaskSpawn(const runtime::CoroContext &) noexcept -> void override;
    auto onTaskComplete(const runtime::CoroContext &) noexcept -> void override;
    auto onResume(const runtime::CoroContext &) noexcept -> void override;
    auto onSuspend(const runtime::CoroContext &) noexcept -> void override;
    auto onChildBegin(const runtime::CoroContext &) noexcept -> void override;
    auto onChildEnd(const runtime::CoroContext &) noexcept -> void override;

    // ── Tracing core ────────────────────────────────────────────────
    auto observe(EventKind kind, const runtime::CoroContext &ctxt) noexcept -> void;
    auto observeImpl(EventKind kind, const runtime::CoroContext &ctxt) -> void;
    auto snapshotJson() -> std::pmr::string;

    // ── HTTP server ─────────────────────────────────────────────────
    auto handleConnection(BufStream<TcpStream> stream) -> Task<void>;
    auto handleRequest(BufStream<TcpStream> &stream, std::string_view method, std::string_view path) -> IoTask<void>;
    auto sendReply(BufStream<TcpStream> &stream, int status, std::string_view contentType, std::string_view body) -> IoTask<void>;
    auto serve() -> Task<void>;

    Clock::time_point mEpoch = Clock::now();
    std::string       mBind;
    WaitHandle<void>  mServeHandle;
    uintptr_t         mTaskId = 0;
    std::pmr::unsynchronized_pool_resource mPool;
    std::pmr::map<const runtime::CoroContext *, TaskRecord> mTasks {&mPool};
    std::pmr::map<const runtime::CoroContext *, TaskRecord> mChildTasks {&mPool};
};

TracingWebUi::Impl::~Impl() {
    if (mServeHandle) {
        mServeHandle.stop();
        mServeHandle.wait();
    }
}

// ─── HTTP server ─────────────────────────────────────────────────────
auto TracingWebUi::Impl::serve() -> Task<void> {
    co_await this_coro::setName("WebUiServer");
    auto listener = co_await TcpListener::bind(mBind);
    if (!listener) {
        ILIAS_WARN("WebUI", "Failed to bind {}: {}", mBind, listener.error().message());
        co_return;
    }
    if (auto ep = listener->localEndpoint()) {
        ILIAS_INFO("WebUI", "Dashboard at http://{}", ep->toString());
    }
    co_await TaskScope::enter([&](TaskScope &scope) -> Task<void> {
        while (true) {
            auto accepted = co_await listener->accept();
            if (!accepted) {
                ILIAS_WARN("WebUI", "Accept error: {}", accepted.error().message());
                break;
            }
            auto &[stream, ep] = *accepted;
            scope.spawn(handleConnection(std::move(stream)));
        }
    });
}

auto TracingWebUi::Impl::handleConnection(BufStream<TcpStream> stream) -> Task<void> {
    co_await this_coro::setName("WebUiServerLoop");
    auto splitRequestLine = [](std::string_view line) -> std::tuple<std::string_view, std::string_view, std::string_view> {
        auto sp1 = line.find(' ');
        if (sp1 == std::string_view::npos) return {};
        auto sp2 = line.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos) return {};
        return {line.substr(0, sp1), line.substr(sp1 + 1, sp2 - sp1 - 1), line.substr(sp2 + 1)};
    };
    auto line = std::string {};
    auto header = std::string {};
    while (true) {
        line.clear();
        if (!co_await stream.readline(line, "\r\n")) {
            co_return;
        }
        if (line.ends_with("\r\n")) {
            line.resize(line.size() - 2);
        }
        auto [method, path, version] = splitRequestLine(line);
        if (method.empty()) {
            co_return;
        }
        // Consume remaining headers
        while (true) {
            header.clear();
            if (!co_await stream.readline(header, "\r\n")) {
                co_return;
            }
            if (header == "\r\n") { // end of headers
                break;
            }
        }
        if (auto ret = co_await handleRequest(stream, method, path); !ret) {
            co_return;
        }
        if (auto ret = co_await stream.flush(); !ret) {
            co_return;
        }
    }
}

auto TracingWebUi::Impl::handleRequest(BufStream<TcpStream> &stream, std::string_view method, std::string_view path) -> IoTask<void> {
    if (method != "GET") {
        co_return co_await sendReply(stream, 405, "text/plain; charset=utf-8", "method not allowed");
    }
    if (auto q = path.find('?'); q != std::string_view::npos) {
        path = path.substr(0, q);
    }
    if (path == "/") {
        auto html = std::string_view {
            _binary_index_html_start,
            _binary_index_html_end
        };
        co_return co_await sendReply(stream, 200, "text/html; charset=utf-8", html);
    }
    if (path == "/api/tasks") {
        auto json = snapshotJson();
        co_return co_await sendReply(stream, 200, "application/json; charset=utf-8", json);
    }
    if (path == "/favicon.ico") {
        co_return co_await sendReply(stream, 204, "text/plain; charset=utf-8", "");
    }
    co_return co_await sendReply(stream, 404, "text/plain; charset=utf-8", "not found");
}

auto TracingWebUi::Impl::sendReply(BufStream<TcpStream> &stream, int status, std::string_view contentType, std::string_view body) -> IoTask<void> {
    const auto statusText = [](int code) -> std::string_view {
        switch (code) {
            case 200: return "OK";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            default:  return "Unknown";
        }
    };
    std::pmr::string header {&mPool};
    fmtlib::format_to(
        std::back_inserter(header),
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "\r\n",
        status, statusText(status),
        contentType,
        body.size()
    );
    ILIAS_CO_TRY(co_await stream.writeAll(ilias::makeBuffer(header)));
    if (!body.empty()) {
        ILIAS_CO_TRY(co_await stream.writeAll(ilias::makeBuffer(body)));
    }
    co_return {};
}

// ─── TracingSubscriber callbacks ─────────────────────────────────────
auto TracingWebUi::Impl::onTaskSpawn(const runtime::CoroContext &ctxt) noexcept -> void    { observe(EventKind::TaskSpawn, ctxt); }
auto TracingWebUi::Impl::onTaskComplete(const runtime::CoroContext &ctxt) noexcept -> void { observe(EventKind::TaskComplete, ctxt); }
auto TracingWebUi::Impl::onResume(const runtime::CoroContext &ctxt) noexcept -> void       { observe(EventKind::Resume, ctxt); }
auto TracingWebUi::Impl::onSuspend(const runtime::CoroContext &ctxt) noexcept -> void      { observe(EventKind::Suspend, ctxt); }
auto TracingWebUi::Impl::onChildBegin(const runtime::CoroContext &ctxt) noexcept -> void   { observe(EventKind::ChildBegin, ctxt); }
auto TracingWebUi::Impl::onChildEnd(const runtime::CoroContext &ctxt) noexcept -> void     { observe(EventKind::ChildEnd, ctxt); }

auto TracingWebUi::Impl::observe(EventKind kind, const runtime::CoroContext &ctxt) noexcept -> void {
    ILIAS_TRY { observeImpl(kind, ctxt); }
    ILIAS_CATCH (...) {}
}

auto TracingWebUi::Impl::observeImpl(EventKind kind, const runtime::CoroContext &ctxt) -> void {
    switch (kind) {
        case EventKind::TaskSpawn: { // New task spawn
            auto id = ++mTaskId;
            auto location = std::string {};
            if (auto frame = ctxt.topFrame(); frame) {
                location = fmtlib::format("{}:{}", frame->filename(), frame->line());
                std::ranges::replace(location, '\\', '/');
            }

            mTasks.emplace(&ctxt, TaskRecord {
                .id = id,
                .createdAt = Clock::now(),
                .location = std::move(location)
            });
            break;
        }
        case EventKind::TaskComplete: { // Task complete
            mTasks.erase(&ctxt);
            break;
        }
        case EventKind::Resume: {
            auto it = mTasks.find(&ctxt);
            if (it == mTasks.end()) {
                break;
            }
            auto &[_, task] = *it;
            task.lastResumeAt = Clock::now();
            task.resumes++;
            break;
        }
        case EventKind::Suspend: {
            auto it = mTasks.find(&ctxt);
            if (it == mTasks.end()) {
                break;
            }
            auto &[_, task] = *it;
            task.totalBusy += Clock::now() - task.lastResumeAt;
        }
        default: break;
    }
}

auto TracingWebUi::Impl::snapshotJson() -> std::pmr::string {
    // [
    //     {
    //         "id": 0,
    //         "name": "Task 1",
    //         "state": "Running or Idle or Yielded or Completed",
    //         "total_time": 1234,
    //         "busy_time": 1000,
    //         "polls": 5,
    //         "location": "main.cpp:114514"
    //     },
    // ]
    std::pmr::string json {&mPool};
    json += "[";
    for (auto &[ctxt, task] : mTasks) {
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - task.createdAt
        );
        auto totalBusy = std::chrono::duration_cast<std::chrono::milliseconds>(task.totalBusy);
        fmtlib::format_to(
            std::back_inserter(json),
            R"({{
                "id": {},
                "name": "{}",
                "state": "{}",
                "total_time": {},
                "busy_time": {},
                "polls": {},
                "location": "{}"
            }},)",
            task.id,
            ctxt->name(),
            "Idle",
            totalTime.count(),
            totalBusy.count(),
            task.resumes,
            task.location
        );
    }
    if (!mTasks.empty()) {
        json.pop_back(); // Remove trailing comma
    }
    json += "]";
    return json;
}

// MARK: TracingWebUi
TracingWebUi::TracingWebUi(std::string_view bind) : d(std::make_unique<Impl>()) {
    d->mBind.assign(bind);
}
TracingWebUi::~TracingWebUi() = default;

auto TracingWebUi::install() -> bool {
    if (!d->mServeHandle) {
        d->mServeHandle = spawn(d->serve());
    }
    return d->install();
}

ILIAS_NS_END