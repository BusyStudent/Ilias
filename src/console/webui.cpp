#include <ilias/runtime/tracing.hpp>
#include <ilias/runtime/capture.hpp>
#include <ilias/runtime/coro.hpp>
#include <ilias/console.hpp>
#include <ilias/net.hpp>
#include <ilias/task.hpp>
#include <ilias/log.hpp>
#include <ilias/io.hpp>

#include <memory_resource>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <charconv>
#include <ranges>
#include <vector>
#include <set>

#if !defined(ILIAS_NO_TRACING_WEBUI)

// Resource by build system
extern "C" {
    // index.html
    extern const char _binary_index_html_start[];
    extern const char _binary_index_html_end[];

    // script.js
    extern const char _binary_script_js_start[];
    extern const char _binary_script_js_end[];

    // styles.css
    extern const char _binary_styles_css_start[];
    extern const char _binary_styles_css_end[];
}

ILIAS_NS_BEGIN

// ═════════════════════════════════════════════════════════════════════
// Implementation
// ═════════════════════════════════════════════════════════════════════
struct TracingWebUi::Impl : public runtime::TracingSubscriber {
    Impl();
    ~Impl();

    using Clock = std::chrono::steady_clock;

    enum class EventKind : uint8_t {
        TaskSpawn, TaskComplete, Resume, Suspend, ChildBegin, ChildEnd
    };

    enum class TaskState : uint8_t {
        Spawned, Running, Suspended, Completed, Stopped
    };
    struct TaskRecord {
        intptr_t id = 0;
        std::optional<intptr_t> parentId;
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
        std::pmr::string location; // The source location (file:line) of where the task was spawned
        std::pmr::set<intptr_t> children; // The children of this task
    };

    // ── TracingSubscriber ───────────────────────────────────────────
    auto onTaskSpawn(runtime::CoroContext &) noexcept -> void override;
    auto onTaskComplete(runtime::CoroContext &) noexcept -> void override;
    auto onResume(runtime::CoroContext &) noexcept -> void override;
    auto onSuspend(runtime::CoroContext &) noexcept -> void override;
    auto onChildBegin(runtime::CoroContext &) noexcept -> void override;
    auto onChildEnd(runtime::CoroContext &) noexcept -> void override;

    // ── Tracing core ────────────────────────────────────────────────
    auto observe(EventKind kind, runtime::CoroContext &ctxt) noexcept -> void;
    auto observeImpl(EventKind kind, runtime::CoroContext &ctxt) -> void;
    auto snapshotJson() -> std::pmr::string;
    auto snapshotTask(std::pmr::string &out) -> void;
    auto snapshotStacktrace(intptr_t taskId) -> std::pmr::string;
    auto allocTaskId() -> intptr_t;
    auto allocChildId() -> intptr_t;

    // ── HTTP server ─────────────────────────────────────────────────
    auto handleConnection(BufStream<TcpStream> stream) -> Task<void>;
    auto handleRequest(BufStream<TcpStream> &stream, std::string_view method, std::string_view path) -> IoTask<void>;
    auto sendReply(BufStream<TcpStream> &stream, int status, std::string_view contentType, std::string_view body) -> IoTask<void>;
    auto serve() -> Task<void>;

    Clock::time_point mEpoch = Clock::now();
    std::string       mBind;
    WaitHandle<void>  mServeHandle;
    intptr_t          mTaskId  = 0; // [1, intptr_max]
    intptr_t          mChildId = 0; // [-1, intptr_min]
    std::pmr::unsynchronized_pool_resource mPool;
    std::pmr::polymorphic_allocator<TaskRecord> mAlloc {&mPool};
    std::pmr::unordered_set<const runtime::CoroContext *> mRoots {&mPool}; // The roots of the task tree
    std::pmr::unordered_map<intptr_t, const runtime::CoroContext *> mIdMaps {&mPool}; // Mapping id to raw context, used for stacktrace
};

TracingWebUi::Impl::Impl() {
    mRoots.reserve(1024);
    mIdMaps.reserve(1024);
}

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
    if (path == "/styles.css") {
        auto css = std::string_view {
            _binary_styles_css_start,
            _binary_styles_css_end
        };
        co_return co_await sendReply(stream, 200, "text/css; charset=utf-8", css);
    }
    if (path == "/script.js") {
        auto js = std::string_view {
            _binary_script_js_start,
            _binary_script_js_end
        };
        co_return co_await sendReply(stream, 200, "application/javascript; charset=utf-8", js);
    }
    if (path == "/api/tasks") {
        auto json = snapshotJson();
        co_return co_await sendReply(stream, 200, "application/json; charset=utf-8", json);
    }
    if (path.starts_with("/api/stacktrace/")) { // /api/stacktrace/<id>
        path.remove_prefix(16);
        intptr_t id = 0;
        if (std::from_chars(path.data(), path.data() + path.size(), id).ec != std::errc {}) {
            co_return co_await sendReply(stream, 400, "text/plain; charset=utf-8", "bad request");
        }
        auto json = snapshotStacktrace(id);
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
auto TracingWebUi::Impl::onTaskSpawn(runtime::CoroContext &ctxt) noexcept -> void    { observe(EventKind::TaskSpawn, ctxt); }
auto TracingWebUi::Impl::onTaskComplete(runtime::CoroContext &ctxt) noexcept -> void { observe(EventKind::TaskComplete, ctxt); }
auto TracingWebUi::Impl::onResume(runtime::CoroContext &ctxt) noexcept -> void       { observe(EventKind::Resume, ctxt); }
auto TracingWebUi::Impl::onSuspend(runtime::CoroContext &ctxt) noexcept -> void      { observe(EventKind::Suspend, ctxt); }
auto TracingWebUi::Impl::onChildBegin(runtime::CoroContext &ctxt) noexcept -> void   { observe(EventKind::ChildBegin, ctxt); }
auto TracingWebUi::Impl::onChildEnd(runtime::CoroContext &ctxt) noexcept -> void     { observe(EventKind::ChildEnd, ctxt); }

auto TracingWebUi::Impl::observe(EventKind kind, runtime::CoroContext &ctxt) noexcept -> void {
    ILIAS_TRY { observeImpl(kind, ctxt); }
    ILIAS_CATCH (...) {}
}

auto TracingWebUi::Impl::observeImpl(EventKind kind, runtime::CoroContext &ctxt) -> void {
    switch (kind) {
        case EventKind::TaskSpawn: { // New task spawn
            auto id = allocTaskId();
            auto location = std::pmr::string {&mPool};
            if (auto frame = ctxt.topFrame(); frame) {
                location = fmtlib::format("{}:{}", frame->filename(), frame->line());
                std::ranges::replace(location, '\\', '/');
            }
            auto record = std::allocate_shared<TaskRecord>(mAlloc, TaskRecord {
                .id = id,
                .createdAt = Clock::now(),
                .location = std::move(location),
                .children = std::pmr::set<intptr_t> {&mPool}
            });

            ctxt.setExtraData(record);
            mRoots.emplace(&ctxt);
            mIdMaps.emplace(id, &ctxt);
            break;
        }
        case EventKind::TaskComplete: { // Task complete
            if (auto task = ctxt.extraData<TaskRecord>(); task) {
                mIdMaps.erase(task->id);
            }
            mRoots.erase(&ctxt);
            break;
        }
        case EventKind::ChildBegin: { // New child task begin
            if (!ctxt.parent()) {
                break;
            }
            auto id = allocChildId();
            auto record = std::allocate_shared<TaskRecord>(mAlloc, TaskRecord {
                .id = id,
                .createdAt = Clock::now(),
                .children = std::pmr::set<intptr_t> {&mPool}
            });
            // Add to parent children list
            if (auto parent = ctxt.parent()->extraData<TaskRecord>(); parent) {
                parent->children.emplace(id);
            }
            // Add to pool
            ctxt.setExtraData(record);
            mIdMaps.emplace(id, &ctxt);
            break;
        }
        case EventKind::ChildEnd: { // Child task end
            auto task = ctxt.extraData<TaskRecord>();
            if (!task) {
                break;
            }
            if (auto parent = ctxt.parent()->extraData<TaskRecord>(); parent) {
                parent->children.erase(task->id);
            }
            mIdMaps.erase(task->id);
            break;
        }
        case EventKind::Resume: {
            auto task = ctxt.extraData<TaskRecord>();
            if (!task) {
                break;
            }
            task->lastResumeAt = Clock::now();
            task->resumes++;

            // Add resume count to all parent
            for (auto cur = ctxt.parent(); cur; cur = cur->parent()) {
                if (auto record = cur->extraData<TaskRecord>(); record) {
                    record->resumes++;
                }
            }
            break;
        }
        case EventKind::Suspend: {
            auto task = ctxt.extraData<TaskRecord>();
            if (!task) {
                break;
            }
            auto time = Clock::now() - task->lastResumeAt;
            task->totalBusy += time;

            // Add busy time to all parent
            for (auto cur = ctxt.parent(); cur; cur = cur->parent()) {
                if (auto record = cur->extraData<TaskRecord>(); record) {
                    record->totalBusy += time;
                }
            }
        }
        default: break;
    }
}

auto TracingWebUi::Impl::allocTaskId() -> intptr_t {
    return ++mTaskId;
}

auto TracingWebUi::Impl::allocChildId() -> intptr_t {
    return --mChildId;
}

auto TracingWebUi::Impl::snapshotJson() -> std::pmr::string {
    // [
    //     {
    //         "id": 0,
    //         "name": "Task 1",
    //         "state": "Running or Idle or Yielded or Completed",
    //         "total_time": 1234,
    //         "busy_time": 1000,
    //         "resumes": 5,
    //         "location": "main.cpp:114514",
    //         "children": [-1, -2, -3]
    //     },
    // ]
    std::pmr::string json {&mPool};
    json += "[";
    for (auto ctxt : mRoots) {
        auto task = ctxt->extraData<TaskRecord>();
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - task->createdAt
        );
        auto totalBusy = std::chrono::duration_cast<std::chrono::milliseconds>(task->totalBusy);
        fmtlib::format_to(
            std::back_inserter(json),
            R"({{
                "id": {},
                "name": "{}",
                "state": "{}",
                "total_time": {},
                "busy_time": {},
                "resumes": {},
                "location": "{}",
                "children": [{:n}]
            }},)",
            task->id,
            ctxt->name(),
            "Idle",
            totalTime.count(),
            totalBusy.count(),
            task->resumes,
            task->location,
            task->children
        );
    }
    if (!mRoots.empty()) {
        json.pop_back(); // Remove trailing comma
    }
    json += "]";
    return json;
}

auto TracingWebUi::Impl::snapshotStacktrace(intptr_t id) -> std::pmr::string {
    // [
    //     {
    //         "function": "main"
    //         "file": "main.cpp"
    //         "line": 123
    //     },
    // ]
    std::pmr::string json {&mPool};
    json += "[";

    auto iter = mIdMaps.find(id);
    if (iter == mIdMaps.end()) {
        json.assign("[]");
        return json;
    }
    auto ctxt = iter->second;
    auto stacktrace = ctxt->stacktrace();
    for (auto &frame : stacktrace) {
        fmtlib::format_to(
            std::back_inserter(json),
            R"({{
                "function": "{}",
                "file": "{}",
                "line": {},
                "message": "{}"
            }},)",
            frame.function(),
            frame.filename(),
            frame.line(),
            frame.message()
        );
    }
    if (stacktrace.size() != 0) {
        json.pop_back(); // Remove trailing comma
    }
    json += "]";
    std::ranges::replace(json, '\\', '/'); // The filename may contain backslashes
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

#endif // ILIAS_TRACING_WEBUI