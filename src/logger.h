#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>

inline std::shared_ptr<spdlog::logger> getLogger()
{
    static std::shared_ptr<spdlog::logger> logger;
    if (!logger) {
        try {
            logger = spdlog::stdout_color_mt("rpgsave");
            logger->set_level(spdlog::level::trace);
            logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
        } catch (const spdlog::spdlog_ex&) {
            logger = spdlog::get("rpgsave");
        }
    }
    return logger;
}

#define LOG_TRACE(...)  getLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...)  getLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...)   getLogger()->info(__VA_ARGS__)
#define LOG_WARN(...)   getLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...)  getLogger()->error(__VA_ARGS__)
