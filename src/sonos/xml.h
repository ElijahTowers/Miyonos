#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace miyonos {

struct XmlNode {
  std::string name;
  std::map<std::string, std::string> attributes;
  std::string text;
  std::vector<XmlNode> children;
};

struct XmlDocument {
  XmlNode root;
  std::string error;
  bool ok = false;
};

std::string xml_escape(const std::string& value);
bool xml_decode_entities(const std::string& value, std::string& decoded,
                         std::size_t max_output = 2 * 1024 * 1024);
XmlDocument parse_xml(const std::string& xml,
                      std::size_t max_input = 2 * 1024 * 1024,
                      std::size_t max_nodes = 8192,
                      std::size_t max_depth = 32);
std::string xml_local_name(const std::string& name);
const XmlNode* xml_child(const XmlNode& node, const std::string& local_name);
std::vector<const XmlNode*> xml_children(const XmlNode& node,
                                         const std::string& local_name);
const XmlNode* xml_find(const XmlNode& node, const std::string& local_name);
std::string xml_text(const XmlNode& node, const std::string& local_name);
std::string xml_attribute(const XmlNode& node, const std::string& name);

}  // namespace miyonos
