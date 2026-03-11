// Copyright (c) zpc contributors. Licensed under the MIT License.
#pragma once
#include "codegen.hpp"
#include "utils.hpp"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Sema/Sema.h"
#include <clang/AST/DeclCXX.h>

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // Data structures collected from AST
  // -----------------------------------------------------------------------

  struct CollectedField {
    std::string name;
    std::string typeName;        // canonical C++ type name
    std::string typeNameNoTag;   // without struct/class/union prefix
    std::string accessStr;       // "public", "protected", "private"
    uint64_t    typeHash{0};
    std::size_t offset{0};       // only set for offsetof-compatible fields
    std::size_t size{0};
    bool        isPointer{false};
    bool        isReference{false};
    bool        isConst{false};
    bool        isStatic{false};
    bool        hasDefaultArg{false};
    std::string defaultArgExpr;
    MetadataContainer metadata;
  };

  struct CollectedParam {
    std::string name;
    std::string typeName;
    bool        hasDefault{false};
    std::string defaultExpr;
  };

  struct CollectedMethod {
    std::string name;
    std::string returnType;
    std::vector<CollectedParam> params;
    bool isStatic{false};
    bool isConst{false};
    bool isVirtual{false};
    bool isPureVirtual{false};
    bool isNoexcept{false};
    std::string accessStr;   // "public", "protected", "private"
    MetadataContainer metadata;
  };

  struct CollectedBase {
    std::string name;        // qualified name
    std::string nameNoTag;
    uint64_t    typeHash{0};
    std::string accessStr;
    bool        isVirtual{false};
  };

  struct CollectedEnumValue {
    std::string name;
    int64_t     value{0};
    MetadataContainer metadata;
  };

  struct CollectedType {
    std::string qualifiedName;
    std::string nameNoTag;       // without struct/class/union prefix
    std::string displayName;     // short name for editors
    uint64_t    typeHash{0};
    std::size_t size{0};
    std::size_t align{0};
    bool        isClass{false};
    bool        isStruct{false};
    bool        isUnion{false};
    bool        isEnum{false};
    bool        isAbstract{false};
    bool        isAggregate{false};
    std::vector<CollectedField>    fields;
    std::vector<CollectedMethod>   methods;
    std::vector<CollectedBase>     bases;
    std::vector<CollectedEnumValue> enumValues;
    MetadataContainer metadata;
    std::string forwardDecl;
  };

  // -----------------------------------------------------------------------
  // Reflection model — result of processing all TUs
  // -----------------------------------------------------------------------

  struct ReflectionModel {
    std::string debugName;
    std::set<std::string> generatedHeaders;
    /// Original input source files (need to be #include'd before reflected headers)
    std::vector<std::string> inputSourcePaths;
  };

  // -----------------------------------------------------------------------
  // PPCallbacks — track all files opened during preprocessing (visitors)
  // -----------------------------------------------------------------------

  class IncludeTracker : public clang::PPCallbacks {
  public:
    std::set<std::string>* deps = nullptr;
    clang::SourceManager*  sm   = nullptr;

    void FileChanged(clang::SourceLocation Loc, FileChangeReason Reason,
                     clang::SrcMgr::CharacteristicKind FileType,
                     clang::FileID PrevFID) override;
  };

  // -----------------------------------------------------------------------
  // RecursiveASTVisitor — collect reflected records and enums
  // -----------------------------------------------------------------------

  class ReflectionASTConsumer; // forward

  class ReflectDeclVisitor
      : public clang::RecursiveASTVisitor<ReflectDeclVisitor> {
  public:
    ReflectionASTConsumer* consumer = nullptr;

    bool VisitCXXRecordDecl(clang::CXXRecordDecl* rd);
    bool VisitEnumDecl(clang::EnumDecl* ed);

    /// Do not descend into template instantiations.
    bool shouldVisitTemplateInstantiations() const { return false; }
  };

  // -----------------------------------------------------------------------
  // AST consumer — drives the visitor and collects reflected types
  // -----------------------------------------------------------------------

  class ReflectionASTConsumer : public clang::ASTConsumer {
  public:
    ReflectionASTConsumer(CodeCompilerState& state, std::string headerPath,
                          clang::CompilerInstance& compiler, bool verbose);

    void HandleTranslationUnit(clang::ASTContext& context) override;

    // Collected types for this TU
    std::vector<CollectedType> collectedTypes;
    clang::ASTContext* astContext{nullptr};
    bool verbose{false};

  private:
    ReflectDeclVisitor visitor;
    CodeCompilerState& compilerState;
    std::string headerPath;
    clang::CompilerInstance& compilerInstance;

    friend class ReflectDeclVisitor;
  };

  // -----------------------------------------------------------------------
  // Frontend action
  // -----------------------------------------------------------------------

  class ReflectionGeneratorAction : public clang::ASTFrontendAction {
  public:
    ReflectionGeneratorAction(CodeCompilerState& state, std::string headerPath, bool verbose,
                              std::set<std::string>* outDeps = nullptr)
        : compilerState(state), headerPath(std::move(headerPath)),
          verbose(verbose), outDeps_(outDeps) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
        clang::CompilerInstance& compiler, llvm::StringRef) override {
      // Register PPCallbacks for include-dependency tracking (visitor pattern).
      if (outDeps_) {
        auto tracker = std::make_unique<IncludeTracker>();
        tracker->deps = outDeps_;
        tracker->sm   = &compiler.getSourceManager();
        compiler.getPreprocessor().addPPCallbacks(std::move(tracker));
      }
      return std::make_unique<ReflectionASTConsumer>(
          compilerState, headerPath, compiler, verbose);
    }

    /// After the tool has run, this contains all files opened during parsing.
    std::set<std::string>* outDeps_{nullptr};

  private:
    CodeCompilerState& compilerState;
    std::string headerPath;
    bool verbose;
  };

} // namespace zs::reflect_tool
