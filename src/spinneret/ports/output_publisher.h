#pragma once

#include <string>

#include "spinneret/domain/correlation_metadata.h"
#include "spinneret/domain/flight_event.h"

namespace spinneret::ports {

/// Port for a destination that receives canonical events.
class OutputPublisher {
public:
    virtual ~OutputPublisher() = default;

    virtual std::string name() const = 0;
    virtual std::string type() const = 0;  ///< "Rest" or "IbmMq".

    /// Whether the runtime config enables this output.
    virtual bool enabled() const = 0;

    /// Whether a failed publish should fail the message. A failure on an
    /// optional output is reported (log, metric, response) but the message
    /// still succeeds.
    virtual bool required() const = 0;

    /// Enabled and, as far as is known, able to deliver.
    virtual bool healthy() const = 0;
    virtual std::string status_detail() const { return {}; }

    /// Delivers synchronously; throws std::exception on failure.
    virtual void publish(const domain::FlightEvent& event, const domain::CorrelationMetadata& correlation) = 0;

    /// Drops cached connections so the next publish uses current settings.
    virtual void reset() {}
};

}  // namespace spinneret::ports
