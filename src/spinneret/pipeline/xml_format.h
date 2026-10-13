#pragma once

#include <string>
#include <string_view>

#include <pugixml.hpp>

namespace spinneret::pipeline {

/// Pretty-prints XML with two-space indentation. Uses `document` when given,
/// otherwise parses `raw`; returns `raw` unchanged if it is not well-formed.
std::string pretty_print_xml(std::string_view raw, const pugi::xml_document* document = nullptr);

}  // namespace spinneret::pipeline
