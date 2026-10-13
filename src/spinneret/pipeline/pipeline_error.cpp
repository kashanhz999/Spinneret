#include "spinneret/pipeline/pipeline_error.h"

namespace spinneret::pipeline {

std::string_view to_string(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::InvalidXml: return "INVALID_XML";
        case ErrorKind::UnknownMessageType: return "UNKNOWN_MESSAGE_TYPE";
        case ErrorKind::TransformFailed: return "TRANSFORM_FAILED";
        case ErrorKind::PublishFailed: return "PUBLISH_FAILED";
        case ErrorKind::Internal: return "INTERNAL";
    }
    return "INTERNAL";
}

bool is_permanent(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::InvalidXml:
        case ErrorKind::UnknownMessageType:
        case ErrorKind::TransformFailed: return true;
        case ErrorKind::PublishFailed:
        case ErrorKind::Internal: return false;
    }
    return false;
}

}  // namespace spinneret::pipeline
