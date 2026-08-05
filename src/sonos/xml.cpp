#include "sonos/xml.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace miyonos {

namespace {

bool append_utf8(unsigned long codepoint, std::string& output,
                 std::size_t max_output) {
  if (codepoint == 0 || codepoint > 0x10ffff ||
      (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
    return false;
  }
  std::size_t bytes = codepoint < 0x80       ? 1
                      : codepoint < 0x800    ? 2
                      : codepoint < 0x10000  ? 3
                                             : 4;
  if (output.size() > max_output - std::min(max_output, bytes)) return false;
  if (bytes == 1) {
    output.push_back(static_cast<char>(codepoint));
  } else if (bytes == 2) {
    output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (bytes == 3) {
    output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
  return true;
}

class Parser {
 public:
  Parser(const std::string& input, std::size_t max_nodes, std::size_t max_depth)
      : input_(input), max_nodes_(max_nodes), max_depth_(max_depth) {}

  XmlDocument parse() {
    XmlDocument document;
    skip_misc();
    if (!parse_node(document.root, 0)) {
      document.error = error_.empty() ? "Malformed XML" : error_;
      return document;
    }
    skip_misc();
    if (position_ != input_.size()) {
      document.error = "Trailing XML data";
      return document;
    }
    document.ok = true;
    return document;
  }

 private:
  void skip_space() {
    while (position_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[position_]))) {
      ++position_;
    }
  }

  bool starts(const char* text) const {
    return input_.compare(position_, std::char_traits<char>::length(text), text) == 0;
  }

  bool skip_until(const char* marker) {
    const auto end = input_.find(marker, position_);
    if (end == std::string::npos) return false;
    position_ = end + std::char_traits<char>::length(marker);
    return true;
  }

  void skip_misc() {
    while (true) {
      skip_space();
      if (starts("<?")) {
        if (!skip_until("?>")) return;
      } else if (starts("<!--")) {
        if (!skip_until("-->")) return;
      } else if (starts("<!DOCTYPE")) {
        const auto end = input_.find('>', position_ + 9);
        if (end == std::string::npos) return;
        position_ = end + 1;
      } else {
        break;
      }
    }
  }

  bool parse_name(std::string& name) {
    const auto begin = position_;
    while (position_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[position_]);
      if (!(std::isalnum(c) || c == '_' || c == '-' || c == ':' || c == '.')) break;
      ++position_;
    }
    if (position_ == begin || position_ - begin > 256) return false;
    name = input_.substr(begin, position_ - begin);
    return true;
  }

  bool parse_attribute_value(std::string& value) {
    if (position_ >= input_.size() ||
        (input_[position_] != '"' && input_[position_] != '\'')) {
      return false;
    }
    const char quote = input_[position_++];
    const auto begin = position_;
    const auto end = input_.find(quote, position_);
    if (end == std::string::npos || end - begin > 65536) return false;
    position_ = end + 1;
    return xml_decode_entities(input_.substr(begin, end - begin), value, 65536);
  }

  bool parse_node(XmlNode& node, std::size_t depth) {
    if (depth > max_depth_ || ++nodes_ > max_nodes_) {
      error_ = "XML complexity limit exceeded";
      return false;
    }
    if (position_ >= input_.size() || input_[position_] != '<' ||
        starts("</") || starts("<!") || starts("<?")) {
      return false;
    }
    ++position_;
    if (!parse_name(node.name)) return false;
    while (true) {
      skip_space();
      if (starts("/>")) {
        position_ += 2;
        return true;
      }
      if (position_ < input_.size() && input_[position_] == '>') {
        ++position_;
        break;
      }
      std::string key;
      if (!parse_name(key)) return false;
      skip_space();
      if (position_ >= input_.size() || input_[position_++] != '=') return false;
      skip_space();
      std::string value;
      if (!parse_attribute_value(value)) return false;
      if (node.attributes.size() >= 128) {
        error_ = "XML attribute limit exceeded";
        return false;
      }
      node.attributes[std::move(key)] = std::move(value);
    }

    std::string text;
    while (position_ < input_.size()) {
      if (starts("</")) {
        position_ += 2;
        std::string close_name;
        if (!parse_name(close_name)) return false;
        skip_space();
        if (position_ >= input_.size() || input_[position_++] != '>' ||
            close_name != node.name) {
          return false;
        }
        node.text = std::move(text);
        return true;
      }
      if (starts("<!--")) {
        if (!skip_until("-->")) return false;
        continue;
      }
      if (starts("<![CDATA[")) {
        position_ += 9;
        const auto end = input_.find("]]>", position_);
        if (end == std::string::npos || text.size() + end - position_ > 2 * 1024 * 1024)
          return false;
        text.append(input_, position_, end - position_);
        position_ = end + 3;
        continue;
      }
      if (input_[position_] == '<') {
        node.children.emplace_back();
        if (!parse_node(node.children.back(), depth + 1)) return false;
        continue;
      }
      const auto end = input_.find('<', position_);
      const auto length = (end == std::string::npos ? input_.size() : end) - position_;
      std::string decoded;
      if (!xml_decode_entities(input_.substr(position_, length), decoded,
                               2 * 1024 * 1024 - std::min<std::size_t>(
                                                   2 * 1024 * 1024, text.size()))) {
        return false;
      }
      text += decoded;
      position_ += length;
    }
    return false;
  }

  const std::string& input_;
  std::size_t position_ = 0;
  std::size_t nodes_ = 0;
  std::size_t max_nodes_;
  std::size_t max_depth_;
  std::string error_;
};

}  // namespace

std::string xml_escape(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 32);
  for (unsigned char c : value) {
    switch (c) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '"': result += "&quot;"; break;
      case '\'': result += "&apos;"; break;
      default:
        if (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') {
          result.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return result;
}

bool xml_decode_entities(const std::string& value, std::string& decoded,
                         std::size_t max_output) {
  decoded.clear();
  decoded.reserve(std::min(value.size(), max_output));
  std::size_t position = 0;
  while (position < value.size()) {
    if (decoded.size() >= max_output) return false;
    if (value[position] != '&') {
      decoded.push_back(value[position++]);
      continue;
    }
    const auto end = value.find(';', position + 1);
    if (end == std::string::npos || end - position > 16) return false;
    const std::string entity = value.substr(position + 1, end - position - 1);
    if (entity == "amp") decoded.push_back('&');
    else if (entity == "lt") decoded.push_back('<');
    else if (entity == "gt") decoded.push_back('>');
    else if (entity == "quot") decoded.push_back('"');
    else if (entity == "apos") decoded.push_back('\'');
    else if (!entity.empty() && entity.front() == '#') {
      const bool hex = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X');
      const std::string number = entity.substr(hex ? 2 : 1);
      unsigned long codepoint = 0;
      const auto parsed = std::from_chars(number.data(), number.data() + number.size(),
                                          codepoint, hex ? 16 : 10);
      if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() ||
          !append_utf8(codepoint, decoded, max_output)) {
        return false;
      }
    } else {
      return false;
    }
    position = end + 1;
  }
  return true;
}

XmlDocument parse_xml(const std::string& xml, std::size_t max_input,
                      std::size_t max_nodes, std::size_t max_depth) {
  if (xml.size() > max_input) {
    XmlDocument result;
    result.error = "XML input exceeds limit";
    return result;
  }
  return Parser(xml, max_nodes, max_depth).parse();
}

std::string xml_local_name(const std::string& name) {
  const auto colon = name.rfind(':');
  return colon == std::string::npos ? name : name.substr(colon + 1);
}

const XmlNode* xml_child(const XmlNode& node, const std::string& local_name) {
  for (const auto& child : node.children) {
    if (xml_local_name(child.name) == local_name) return &child;
  }
  return nullptr;
}

std::vector<const XmlNode*> xml_children(const XmlNode& node,
                                         const std::string& local_name) {
  std::vector<const XmlNode*> result;
  for (const auto& child : node.children) {
    if (xml_local_name(child.name) == local_name) result.push_back(&child);
  }
  return result;
}

const XmlNode* xml_find(const XmlNode& node, const std::string& local_name) {
  if (xml_local_name(node.name) == local_name) return &node;
  for (const auto& child : node.children) {
    if (const auto* result = xml_find(child, local_name)) return result;
  }
  return nullptr;
}

std::string xml_text(const XmlNode& node, const std::string& local_name) {
  const auto* child = xml_find(node, local_name);
  return child ? child->text : std::string{};
}

std::string xml_attribute(const XmlNode& node, const std::string& name) {
  const auto exact = node.attributes.find(name);
  if (exact != node.attributes.end()) return exact->second;
  for (const auto& item : node.attributes) {
    if (xml_local_name(item.first) == name) return item.second;
  }
  return {};
}

}  // namespace miyonos
