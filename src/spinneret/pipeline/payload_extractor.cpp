#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "spinneret/pipeline/stages.h"
#include "spinneret/util/strings.h"

namespace spinneret::pipeline {

namespace {

using json = nlohmann::ordered_json;

bool is_namespace_declaration(std::string_view attribute_name) {
    return attribute_name == "xmlns" || util::starts_with(attribute_name, "xmlns:");
}

json element_to_json(pugi::xml_node element, int depth) {
    if (depth > PayloadExtractor::kMaxDepth) {
        throw PipelineError(ErrorKind::InvalidXml,
                            "Invalid XML: element nesting exceeds " +
                                std::to_string(PayloadExtractor::kMaxDepth) + " levels");
    }

    json node = json::object();

    // 1. Namespace prefix
    std::string_view prefix = util::name_prefix(element.name());
    if (!prefix.empty()) node["__prefix"] = std::string(prefix);

    // 2. Attributes, keyed by local name
    for (pugi::xml_attribute attribute : element.attributes()) {
        std::string_view name = attribute.name();
        if (is_namespace_declaration(name)) continue;
        node["_" + std::string(util::local_name(name))] = attribute.value();
    }

    // 3. Group child elements by local name (first-seen order); collect text
    std::vector<std::pair<std::string, std::vector<pugi::xml_node>>> groups;
    std::unordered_map<std::string, std::size_t> group_index;
    std::string text;

    for (pugi::xml_node child : element.children()) {
        switch (child.type()) {
            case pugi::node_element: {
                std::string name(util::local_name(child.name()));
                auto [it, inserted] = group_index.try_emplace(name, groups.size());
                if (inserted) groups.emplace_back(name, std::vector<pugi::xml_node>{});
                groups[it->second].second.push_back(child);
                break;
            }
            case pugi::node_pcdata:
            case pugi::node_cdata: text += util::trim(child.value()); break;
            default: break;
        }
    }

    // 4. Text content
    if (!text.empty()) node["__text"] = text;

    // 5. Child elements; repeated names become arrays
    for (auto& [name, elements] : groups) {
        if (elements.size() == 1) {
            node[name] = element_to_json(elements.front(), depth + 1);
        } else {
            json array = json::array();
            for (pugi::xml_node el : elements) array.push_back(element_to_json(el, depth + 1));
            node[name] = std::move(array);
        }
    }

    return node;
}

}  // namespace

json PayloadExtractor::to_json(const pugi::xml_document& document) {
    pugi::xml_node root = document.document_element();
    json wrapper = json::object();
    wrapper[std::string(util::local_name(root.name()))] = element_to_json(root, 1);
    return wrapper;
}

void PayloadExtractor::execute(PipelineContext& context) const {
    if (!context.document) context.document = parse_xml(context.raw_input());
    context.parsed = to_json(*context.document);
    context.add_audit("Stage 3: Payload Parsed (Namespace-Aware)");
}

}  // namespace spinneret::pipeline
