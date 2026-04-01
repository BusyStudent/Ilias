#pragma once

#include <ilias/runtime/tracing.hpp>
#include <ilias/io/error.hpp>
#include <ilias/result.hpp>

ILIAS_NS_BEGIN

/**
 * @brief The webui for the console
 * 
 */
class ILIAS_API TracingWebUi {
public:
    TracingWebUi(std::string_view bind = "127.0.0.1:8066");
    TracingWebUi(const TracingWebUi&) = delete;
    ~TracingWebUi();

    /**
     * @brief Install the webui to the current thread
     * @note Install the executor before calling this
     * 
     * @return true 
     * @return false 
     */
    auto install() -> bool;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

ILIAS_NS_END