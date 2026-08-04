#pragma once

#include <ilias/runtime/tracing.hpp>
#include <string>
#include <memory>

ILIAS_NS_BEGIN

/**
 * @brief The webui for the console, used for debugging, profiling and tracing, no-op on ILIAS_CORO_TRACE is not defined
 * 
 */
class ILIAS_TRACING_API TracingWebUi {
public:
    /**
     * @brief Construct a new Tracing Web Ui object
     * @note environment variable `ILIAS_TRACING_WEBUI_BIND` can be used to override the bind endpoint
     * 
     * @param bind The bind endpoint of the webui (default: 127.0.0.1:8066)
     */
    TracingWebUi(std::string_view bind = "127.0.0.1:8066");
    ~TracingWebUi();

    /**
     * @brief Install the webui to the current thread
     * @note Install the executor before calling this
     * 
     * @return true 
     * @return false 
     */
    auto install() -> bool;

    /**
     * @brief Get the bind endpoint of the webui
     * 
     * @return std::string_view 
     */
    auto endpoint() const -> std::string_view;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// Disable
#if !defined(ILIAS_CORO_TRACE)
struct TracingWebUi::Impl {};
inline TracingWebUi::TracingWebUi(std::string_view) {}
inline TracingWebUi::~TracingWebUi() {}
inline auto TracingWebUi::install() -> bool { return false; }
inline auto TracingWebUi::endpoint() const -> std::string_view { return {}; }
#endif

ILIAS_NS_END