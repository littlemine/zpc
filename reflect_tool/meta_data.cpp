// Copyright (c) zpc contributors. Licensed under the MIT License.
#include "meta_data.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include <stdexcept>

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // parse_metadata_dsl — top-level entry
  // -----------------------------------------------------------------------

  MetadataContainer parse_metadata_dsl(const std::string& inDsl) {
    MetadataContainer container;
    std::string metadata;

    // The DSL string starts with #<type> followed by key=value pairs.
    // e.g. "#struct name = \"Foo\", serializable = true"
    //      "#field transient = true"
    //      "#property name = \"pos\""
    //      "#trait kind = \"Renderable\""

    struct Prefix {
      const char* text;
      MetadataType type;
    };
    static constexpr Prefix prefixes[] = {
        {"#struct ",   MetadataType::Struct},
        {"#enum ",     MetadataType::Enum},
        {"#function ", MetadataType::Function},
        {"#method ",   MetadataType::Function},
        {"#enum_value ", MetadataType::EnumValue},
        {"#field ",    MetadataType::StructField},
        {"#property ", MetadataType::StructField},
        {"#param ",    MetadataType::FunctionParameter},
        {"#trait ",    MetadataType::Trait},
    };

    for (auto& [pfx, ty] : prefixes) {
      if (inDsl.starts_with(pfx)) {
        metadata = inDsl.substr(std::string_view(pfx).size());
        container.type = ty;
        break;
      }
    }

    // If no prefix matched, treat whole string as metadata with type = None
    if (container.type == MetadataType::None) {
      metadata = inDsl;
    }

    if (!metadata.empty()) {
      DSLParser parser(metadata);
      auto ast = parser.parse();
      for (auto& [k, v] : ast) container.properties.insert_or_assign(k, v);
    }

    return container;
  }

  // -----------------------------------------------------------------------
  // Token helpers
  // -----------------------------------------------------------------------

  std::string token_type_to_string(TokenType type) {
    switch (type) {
      case TokenType::KEY:        return "key";
      case TokenType::STRING:     return "string";
      case TokenType::NUMBER:     return "number";
      case TokenType::LIST_START: return "list start";
      case TokenType::LIST_END:   return "list end";
      case TokenType::EQUAL:      return "equal";
      case TokenType::COMMA:      return "comma";
      case TokenType::END:        return "end";
      default:                    return "unknown";
    }
  }

  // -----------------------------------------------------------------------
  // Tokenizer
  // -----------------------------------------------------------------------

  Tokenizer::Tokenizer(const std::string& input)
      : originString(input), ss(input), currentChar(' ') {
    next_char();
  }

  Token Tokenizer::next_token() {
    consume_whitespace();
    if (currentChar == EOF) return {TokenType::END, ""};

    if (currentChar == '=') { next_char(); return {TokenType::EQUAL, "="}; }
    if (currentChar == ',') { next_char(); return {TokenType::COMMA, ","}; }
    if (currentChar == '(') { next_char(); return {TokenType::LIST_START, "("}; }
    if (currentChar == ')') { next_char(); return {TokenType::LIST_END, ")"}; }
    if (std::isdigit(static_cast<unsigned char>(currentChar)) || currentChar == '-')
      return number();
    if (currentChar == '"') return string();
    return key();
  }

  const std::string& Tokenizer::origin_string() const { return originString; }

  void Tokenizer::next_char() { currentChar = static_cast<char>(ss.get()); }

  void Tokenizer::consume_whitespace() {
    while (std::isspace(static_cast<unsigned char>(currentChar))) next_char();
  }

  Token Tokenizer::number() {
    std::string result;
    if (currentChar == '-') { result.push_back('-'); next_char(); }
    bool hasDot = false;
    while (std::isdigit(static_cast<unsigned char>(currentChar)) || currentChar == '.') {
      if (currentChar == '.') hasDot = true;
      result.push_back(currentChar);
      next_char();
    }
    return {TokenType::NUMBER, result};
  }

  Token Tokenizer::key() {
    std::string result;
    while (currentChar != EOF && currentChar != '=' && currentChar != ','
           && currentChar != '(' && currentChar != ')'
           && !std::isspace(static_cast<unsigned char>(currentChar))) {
      result.push_back(currentChar);
      next_char();
    }
    return {TokenType::KEY, result};
  }

  Token Tokenizer::string() {
    std::string result;
    if (currentChar == '"') next_char(); // skip opening quote
    while (currentChar != '"' && currentChar != EOF) {
      if (currentChar == '\\') {
        next_char();
        if (currentChar == '"') result.push_back('"');
        else result.push_back('\\');
      } else {
        result.push_back(currentChar);
      }
      next_char();
    }
    next_char(); // skip closing quote
    return {TokenType::STRING, result};
  }

  // -----------------------------------------------------------------------
  // DSLParser
  // -----------------------------------------------------------------------

  DSLParser::DSLParser(const std::string& input) : tokenizer(input) { next_token(); }

  std::map<std::string, MetaValue> DSLParser::parse() {
    std::map<std::string, MetaValue> ast;
    while (currentToken.type != TokenType::END) {
      std::string k = expect(TokenType::KEY);
      // If the next token is '=', consume it and parse the value.
      // Otherwise treat the key as a boolean flag with value "true".
      if (currentToken.type == TokenType::EQUAL) {
        next_token(); // consume '='
        ast[k] = parse_value();
      } else {
        ast[k] = std::string("true");
      }
      if (currentToken.type == TokenType::COMMA) next_token();
    }
    return ast;
  }

  void DSLParser::next_token() { currentToken = tokenizer.next_token(); }

  std::string DSLParser::expect(TokenType tokenExpected) {
    if (currentToken.type != tokenExpected) {
      throw std::runtime_error(
          fmt::format("Unexpected token when expecting {}, found {}. Metadata: {}",
                      token_type_to_string(tokenExpected),
                      token_type_to_string(currentToken.type),
                      tokenizer.origin_string()));
    }
    std::string value = currentToken.value;
    next_token();
    return value;
  }

  MetaValue DSLParser::parse_value() {
    if (currentToken.type == TokenType::STRING) {
      std::string result = expect(TokenType::STRING);
      while (currentToken.type == TokenType::STRING)
        result += expect(TokenType::STRING);
      return result;
    }
    if (currentToken.type == TokenType::NUMBER) {
      std::string result = expect(TokenType::NUMBER);
      if (result.find('.') != std::string::npos) return std::stof(result);
      return std::stoi(result);
    }
    if (currentToken.type == TokenType::LIST_START) return parse_list();
    if (currentToken.type == TokenType::KEY) {
      // Treat as enum-like identifier value
      std::string result = currentToken.value;
      next_token();
      return result;
    }
    throw std::runtime_error(
        fmt::format("Unexpected value type {}: '{}'. Metadata: {}",
                    token_type_to_string(currentToken.type),
                    currentToken.value, tokenizer.origin_string()));
  }

  MetaValue DSLParser::parse_list() {
    std::vector<std::string> stringList;
    std::vector<float> numericList;
    next_token(); // skip '('
    while (currentToken.type != TokenType::LIST_END) {
      auto val = parse_value();
      if (std::holds_alternative<std::string>(val))
        stringList.push_back(std::get<std::string>(val));
      else if (std::holds_alternative<float>(val))
        numericList.push_back(std::get<float>(val));
      else if (std::holds_alternative<int>(val))
        numericList.push_back(static_cast<float>(std::get<int>(val)));
      if (currentToken.type == TokenType::COMMA) next_token();
    }
    next_token(); // skip ')'
    if (!stringList.empty()) return stringList;
    return numericList;
  }

} // namespace zs::reflect_tool
