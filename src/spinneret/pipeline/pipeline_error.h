#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace spinneret::pipeline {

/// Why a message failed. The distinction between permanent and transient
/// failures drives retry behaviour: a permanent failure will fail again on
/// redelivery, a transient one may succeed.
enum class ErrorKind {
    InvalidXml,          ///< Empty or malformed XML (permanent).
    UnknownMessageType,  ///< MessageType/MessageSubType not supported (permanent).
    TransformFailed,     ///< A transformer could not build events (permanent).
    PublishFailed,       ///< A required output could not accept the event (transient).
    Internal,            ///< Unexpected error (transient).
};

std::string_view to_string(ErrorKind kind);
bool is_permanent(ErrorKind kind);

class PipelineError : public std::runtime_error {
public:
    PipelineError(ErrorKind kind, const std::string& message)
        : std::runtime_error(message), kind_(kind) {}

    ErrorKind kind() const noexcept { return kind_; }

private:
    ErrorKind kind_;
};

}  // namespace spinneret::pipeline
