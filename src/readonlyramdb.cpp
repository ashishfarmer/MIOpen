/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <miopen/readonlyramdb.hpp>

#include <miopen/logger.hpp>

#include <fstream>
#include <mutex>
#include <sstream>

namespace miopen {
ReadonlyRamDb& ReadonlyRamDb::GetCached(const std::string& path, bool warn_if_unreadable)
{
    static std::mutex mutex;
    static const std::lock_guard<std::mutex> lock{mutex};

    static auto instances = std::unordered_map<std::string, ReadonlyRamDb>{};
    const auto it         = instances.find(path);

    if(it != instances.end())
        return it->second;

    auto& instance = instances
                         .emplace(std::piecewise_construct,
                                  std::forward_as_tuple(path),
                                  std::forward_as_tuple(path))
                         .first->second;
    instance.Prefetch(warn_if_unreadable);
    return instance;
}

template <class TFunc>
static auto Measure(const std::string& funcName, TFunc&& func)
{
    if(!miopen::IsLogging(LoggingLevel::Info))
        return func();

    const auto start = std::chrono::high_resolution_clock::now();
    func();
    const auto end = std::chrono::high_resolution_clock::now();
    MIOPEN_LOG_I("ReadonlyRamDb::" << funcName << " time: " << (end - start).count() * .000001f << " ms");
}

void ReadonlyRamDb::Prefetch(bool warn_if_unreadable)
{
    Measure("Prefetch", [this, warn_if_unreadable]() {
        auto file = std::ifstream{db_path};

        if(!file)
        {
            const auto log_level = warn_if_unreadable ? LoggingLevel::Warning : LoggingLevel::Info;
            MIOPEN_LOG(log_level, "File is unreadable: " << db_path);
            return;
        }

        auto line   = std::string{};
        auto n_line = 0;

        while(std::getline(file, line))
        {
            ++n_line;

            if(line.empty())
                continue;

            const auto key_size = line.find('=');
            const bool is_key   = (key_size != std::string::npos && key_size != 0);

            if(!is_key)
            {
                MIOPEN_LOG_E("Ill-formed record: key not found: " << db_path << "#" << n_line);
                continue;
            }

            const auto key      = line.substr(0, key_size);
            const auto contents = line.substr(key_size + 1);

            cache.emplace(key, CacheItem{n_line, contents});
        }
    });
}
} // namespace miopen