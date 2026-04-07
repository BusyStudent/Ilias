#pragma once

#include <ilias/runtime/tracing.hpp>
#include <ilias/io/error.hpp>
#include <ilias/result.hpp>

#if !defined(ILIAS_CORO_TRACE) || defined(ILIAS_NO_FORMAT)
    #define ILIAS_NO_TRACING_WEBUI
    #define ILIAS_WEBUI_API
#else
    #define ILIAS_WEBUI_API ILIAS_API
#endif

ILIAS_NS_BEGIN

/**
 * @brief The webui for the console
 * 
 */
class ILIAS_WEBUI_API TracingWebUi {
public:
    TracingWebUi(std::string_view bind = "127.0.0.1:8066");
    TracingWebUi(const TracingWebUi &) = delete;
    TracingWebUi(TracingWebUi &&) = default;
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
#if defined(ILIAS_NO_TRACING_WEBUI)
struct TracingWebUi::Impl {};
inline TracingWebUi::TracingWebUi(std::string_view) {}
inline TracingWebUi::~TracingWebUi() {}
inline auto TracingWebUi::install() -> bool { return false; }
inline auto TracingWebUi::endpoint() const -> std::string_view { return {}; }
#endif

ILIAS_NS_END