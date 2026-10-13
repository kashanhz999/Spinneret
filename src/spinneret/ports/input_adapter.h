#pragma once

#include <string>

namespace spinneret::ports {

enum class StartResult {
    Started,
    AlreadyRunning,
    Disabled,  ///< The adapter's `enabled` flag is false in the runtime config.
    Failed,    ///< See status_detail() for the reason.
};

/// Port for a channel that receives raw XML and feeds it to the pipeline.
class InputAdapter {
public:
    virtual ~InputAdapter() = default;

    virtual std::string name() const = 0;
    virtual std::string type() const = 0;  ///< "Rest" or "IbmMq".

    /// Starts receiving if the adapter is enabled in the runtime config.
    virtual StartResult start() = 0;
    virtual void stop() = 0;

    virtual bool enabled() const = 0;
    virtual bool running() const = 0;
    virtual bool healthy() const = 0;
    virtual std::string status_detail() const { return {}; }
};

inline const char* to_string(StartResult result) {
    switch (result) {
        case StartResult::Started: return "STARTED";
        case StartResult::AlreadyRunning: return "ALREADY_RUNNING";
        case StartResult::Disabled: return "DISABLED";
        case StartResult::Failed: return "FAILED";
    }
    return "FAILED";
}

}  // namespace spinneret::ports
