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

#if defined(ILIAS_CORO_TRACE)

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

    enum class TaskState : uint8_t {
        Spawned, Running, Suspended, Completed, Stopped
    };
    struct TaskRecord {
        runtime::SpanId id {};
        runtime::SpanId parentId {};
        TaskState state = TaskState::Spawned;
        Clock::time_point createdAt {};
        Clock::time_point lastSeenAt {};
        Clock::time_point lastResumeAt {};
        Clock::duration   totalBusy {};
        size_t resumes = 0;
        bool stopRequested = false;
        bool stopped = false;
        std::pmr::string name;
        std::pmr::string location; // The source location (file:line) of where the task was spawned
        std::pmr::set<runtime::SpanId> children; // The children of this task
    };

    // ── TracingSubscriber ───────────────────────────────────────────
    auto onEvent(const runtime::TraceEvent &event) noexcept -> void override;

    // ── Tracing core ────────────────────────────────────────────────
    auto snapshotJson() -> std::pmr::string;
    auto snapshotTask(std::pmr::string &out) -> void;
    auto snapshotStacktrace(intptr_t spanId) -> std::pmr::string;

    // ── HTTP server ─────────────────────────────────────────────────
    auto handleConnection(BufStream<TcpStream> stream) -> Task<void>;
    auto handleRequest(BufStream<TcpStream> &stream, std::string_view method, std::string_view path) -> IoTask<void>;
    auto sendReply(BufStream<TcpStream> &stream, int status, std::string_view contentType, std::string_view body) -> IoTask<void>;
    auto serve() -> Task<void>;

    Clock::time_point mEpoch = Clock::now();
    std::string       mBind;
    WaitHandle<void>  mServeHandle;
    std::pmr::unsynchronized_pool_resource mPool;
    std::pmr::unordered_map<runtime::SpanId, TaskRecord> mIdMaps {&mPool}; // Mapping id to TaskRecord, used for snapshot
};

TracingWebUi::Impl::Impl() {
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
        std::string_view html {
            _binary_index_html_start,
            _binary_index_html_end
        };
        co_return co_await sendReply(stream, 200, "text/html; charset=utf-8", html);
    }
    if (path == "/styles.css") {
        std::string_view css {
            _binary_styles_css_start,
            _binary_styles_css_end
        };
        co_return co_await sendReply(stream, 200, "text/css; charset=utf-8", css);
    }
    if (path == "/script.js") {
        std::string_view js {
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
    ILIAS_CO_TRYV(co_await stream.writeAll(ilias::makeBuffer(header)));
    if (!body.empty()) {
        ILIAS_CO_TRYV(co_await stream.writeAll(ilias::makeBuffer(body)));
    }
    co_return {};
}

// ─── TracingSubscriber callbacks ─────────────────────────────────────
auto TracingWebUi::Impl::onEvent(const runtime::TraceEvent &event) noexcept -> void {
    switch (event.type) {
        case runtime::TraceEvent::Spawn: { // New task spawn
            // Add Parent
            std::pmr::string location {&mPool};
            std::pmr::string name {&mPool};

            fmtlib::format_to(std::back_inserter(location), "{}:{}", event.location.file_name(), event.location.line());
            std::ranges::replace(location, '\\', '/');

            name = event.span.name;

            // Add it to map
            mIdMaps.emplace(event.span.id, TaskRecord {
                .id = event.span.id,
                .parentId = event.span.parentId,
                .createdAt = Clock::now(),
                .name = std::move(name),
                .location = std::move(location),
                .children = std::pmr::set<runtime::SpanId> {&mPool}
            });
            // Register parent if exist
            if (auto it = mIdMaps.find(event.span.parentId); it != mIdMaps.end()) {
                it->second.children.insert(event.span.id);
            }
            break;
        }
        case runtime::TraceEvent::Complete: { // Task complete
            if (auto it = mIdMaps.find(event.span.parentId); it != mIdMaps.end()) { // Remove child
                auto &[_, record] = *it;
                record.children.erase(event.span.id);
            }
            mIdMaps.erase(event.span.id);
            break;
        }
        case runtime::TraceEvent::Resume: {
            auto it = mIdMaps.find(event.span.id);
            if (it != mIdMaps.end()) {
                auto &record = it->second;
                record.totalBusy = event.span.totalBusy;
                record.resumes = event.span.resumes;
            }
            break;
        }
        case runtime::TraceEvent::NameChange: {
            auto it = mIdMaps.find(event.span.id);
            if (it == mIdMaps.end()) {
                break;
            }
            auto record = &it->second;
            record->name.assign(event.span.name);
            break;
        }
        default: break;
    }
}

auto TracingWebUi::Impl::snapshotJson() -> std::pmr::string {
    // [
    //     {
    //         "id": 0,
    //         "name": "Task 1",
    //         "parent_id": 0,
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
    for (const auto &[id, record] : mIdMaps) {
        auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - record.createdAt
        );
        auto totalBusy = std::chrono::duration_cast<std::chrono::milliseconds>(record.totalBusy);
        fmtlib::format_to(
            std::back_inserter(json),
            R"JSON({{
                "id": {},
                "parent_id": {},
                "name": "{}",
                "state": "{}",
                "total_time": {},
                "busy_time": {},
                "resumes": {},
                "location": "{}",
                "children": [{:n}]
            }},)JSON",
            static_cast<intptr_t>(id),
            static_cast<intptr_t>(record.parentId),
            record.name,
            "Idle",
            totalTime.count(),
            totalBusy.count(),
            record.resumes,
            record.location,
            record.children
        );
    }
    if (!mIdMaps.empty()) {
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

    auto ctxt = runtime::TraceContext::fromId(static_cast<runtime::SpanId>(id));
    if (!ctxt) {
        json.assign("[]");
        return json;
    }
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
    if (!d->install()) {
        return false;
    }
    if (!d->mServeHandle) {
        d->mServeHandle = spawn(d->serve());
    }
    ::fprintf(stderr, "[TracingWebUi] Web UI is available at %s\n", d->mBind.c_str());
    return true;
}

auto TracingWebUi::endpoint() const -> std::string_view {
    return d->mBind;
}

ILIAS_NS_END

#endif // ILIAS_TRACING_WEBUI
