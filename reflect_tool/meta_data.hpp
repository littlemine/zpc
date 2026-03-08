// Copyright (c) zpc contributors. Licensed under the MIT License.
#pragma once
#include <string>
#include <map>
#include <variant>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // Metadata types — mirror the annotation DSL (#struct, #field, …)
  // -----------------------------------------------------------------------

  enum class MetadataType : uint8_t {
    None = 0,
    Struct,
    Enum,
    Function,
    EnumValue,
    StructField,
    FunctionParameter,
    Trait,
  };

  inline MetadataType string_to_metadata_type(std::string_view str) {
    if (str == "struct")     return MetadataType::Struct;
    if (str == "enum")       return MetadataType::Enum;
    if (str == "function" || str == "method") return MetadataType::Function;
    if (str == "enum_value") return MetadataType::EnumValue;
    if (str == "field")      return MetadataType::StructField;
    if (str == "param")      return MetadataType::FunctionParameter;
    if (str == "trait")      return MetadataType::Trait;
    if (str == "property")   return MetadataType::StructField; // alias
    return MetadataType::None;
  }

  inline const char* metadata_type_to_string(MetadataType type) {
    switch (type) {
      case MetadataType::Struct:            return "struct";
      case MetadataType::Enum:              return "enum";
      case MetadataType::Function:          return "function";
      case MetadataType::EnumValue:         return "enum_value";
      case MetadataType::StructField:       return "field";
      case MetadataType::FunctionParameter: return "param";
      case MetadataType::Trait:             return "trait";
      default:                              return "none";
    }
  }

  using MetaValue
      = std::variant<std::string, int, float, std::vector<std::string>, std::vector<float>>;

  struct MetadataContainer {
    MetadataType type = MetadataType::None;
    std::unordered_map<std::string, MetaValue> properties;
  };

  MetadataContainer parse_metadata_dsl(const std::string& in_dsl);

  // -----------------------------------------------------------------------
  // Tokenizer / Parser for the metadata DSL
  // -----------------------------------------------------------------------

  enum class TokenType { KEY, STRING, NUMBER, LIST_START, LIST_END, EQUAL, COMMA, ENUM, END };

  std::string token_type_to_string(TokenType type);

  struct Token {
    TokenType type;
    std::string value;
  };

  class Tokenizer {
  public:
    explicit Tokenizer(const std::string& input);
    Token next_token();
    const std::string& origin_string() const;

  private:
    void next_char();
    void consume_whitespace();
    Token number();
    Token key();
    Token string();

    std::string originString;
    std::istringstream ss;
    char currentChar;
  };

  class DSLParser {
  public:
    explicit DSLParser(const std::string& input);
    std::map<std::string, MetaValue> parse();

  private:
    void next_token();
    std::string expect(TokenType tokenExpected);
    MetaValue parse_value();
    MetaValue parse_list();

    Tokenizer tokenizer;
    Token currentToken;
  };

} // namespace zs::reflect_tool
