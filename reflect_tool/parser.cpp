// Copyright (c) zpc contributors. Licensed under the MIT License.
#include "parser.hpp"
#include "codegen.hpp"
#include "utils.hpp"
#include "zensim/zpc_tpls/fmt/format.h"

#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/FileManager.h>
#include <clang/Sema/Sema.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

using namespace clang;

namespace zs::reflect_tool {

  // -----------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------

  static std::string access_to_string(clang::AccessSpecifier as) {
    switch (as) {
      case AS_public:    return "public";
      case AS_protected: return "protected";
      case AS_private:   return "private";
      default:           return "none";
    }
  }

  /// Build a forward declaration string for a record type.
  static std::string build_forward_decl(const clang::CXXRecordDecl* rd) {
    std::string result;

    // Check if it's a template specialization — skip forward decl.
    if (llvm::isa<ClassTemplateSpecializationDecl>(rd))
      return fmt::format("// (template specialization — no forward decl for {})\n",
                         rd->getQualifiedNameAsString());

    std::string keyword;
    if (rd->isClass())       keyword = "class";
    else if (rd->isStruct()) keyword = "struct";
    else if (rd->isUnion())  keyword = "union";
    else                     return {};

    // Collect enclosing namespaces
    std::vector<std::string> ns;
    const DeclContext* dc = rd->getDeclContext();
    while (dc && !dc->isTranslationUnit()) {
      if (auto* nd = dyn_cast<NamespaceDecl>(dc))
        ns.push_back(nd->getNameAsString());
      dc = dc->getParent();
    }

    for (auto it = ns.rbegin(); it != ns.rend(); ++it)
      result += fmt::format("namespace {} {{ ", *it);

    result += fmt::format("{} {};", keyword, rd->getNameAsString());

    for (size_t i = 0; i < ns.size(); ++i) result += " }";
    result += "\n";
    return result;
  }

  // -----------------------------------------------------------------------
  // IncludeTracker — PPCallbacks implementation (visitor for preprocessor)
  // -----------------------------------------------------------------------

  void IncludeTracker::FileChanged(clang::SourceLocation Loc,
                                   FileChangeReason Reason,
                                   clang::SrcMgr::CharacteristicKind FileType,
                                   clang::FileID PrevFID) {
    if (Reason != EnterFile || !deps || !sm) return;

    clang::FileID fid = sm->getFileID(Loc);
    if (auto fe = sm->getFileEntryRefForID(fid)) {
      std::string path = fe->getName().str();
      if (!path.empty()) {
        std::replace(path.begin(), path.end(), '\\', '/');
        deps->insert(path);
      }
    }
  }

  // -----------------------------------------------------------------------
  // ReflectDeclVisitor — RecursiveASTVisitor for annotated types
  // -----------------------------------------------------------------------

  bool ReflectDeclVisitor::VisitCXXRecordDecl(CXXRecordDecl* rd) {
    if (!rd || !rd->hasDefinition() || !rd->isThisDeclarationADefinition())
      return true;

    // Skip if marked ZS_NO_REFLECT
    if (has_no_reflect(rd)) return true;

    // Ensure this record has a "zs_reflect" or "reflect" annotation.
    if (!has_reflect_annotation(rd)) return true;

    ASTContext& ctx = *consumer->astContext;
    FNV1aHash hasher;

    CollectedType ct;
    QualType qt(rd->getTypeForDecl(), 0);
    ct.qualifiedName = clang_type_name_no_tag(qt.getCanonicalType());
    ct.nameNoTag     = ct.qualifiedName;
    ct.displayName   = rd->getNameAsString();
    ct.typeHash      = hasher(ct.qualifiedName);
    ct.isClass       = rd->isClass();
    ct.isStruct      = rd->isStruct();
    ct.isUnion       = rd->isUnion();
    ct.isAbstract    = rd->isAbstract();

    // Size and alignment (may not be available for incomplete types)
    if (rd->isCompleteDefinition() && !rd->isDependentType()) {
      const ASTRecordLayout& layout = ctx.getASTRecordLayout(rd);
      ct.size  = layout.getSize().getQuantity();
      ct.align = layout.getAlignment().getQuantity();
      ct.isAggregate = rd->isAggregate();
    }

    ct.forwardDecl = build_forward_decl(rd);
    ct.metadata    = parse_decl_metadata(rd);

    if (consumer->verbose)
      llvm::outs() << "[reflect] Record: " << ct.qualifiedName << "\n";

    // Force implicit members (constructors, destructors, etc.)
    Sema& sema = consumer->compilerInstance.getSema();
    sema.ForceDeclarationOfImplicitMembers(const_cast<CXXRecordDecl*>(rd));

    // ---- Collect fields --------------------------------------------------
    for (auto* field : rd->fields()) {
      if (has_no_reflect(field)) continue;

      CollectedField cf;
      cf.name      = field->getNameAsString();
      QualType ft  = field->getType();
      cf.typeName  = ft.getCanonicalType().getAsString();
      cf.typeNameNoTag = clang_type_name_no_tag(ft.getCanonicalType());
      cf.accessStr = access_to_string(field->getAccess());
      cf.typeHash  = hasher(cf.typeName);
      cf.isPointer   = ft->isPointerType();
      cf.isReference = ft->isReferenceType();
      cf.isConst     = ft.isConstQualified();

      if (field->hasInClassInitializer()) {
        cf.hasDefaultArg = true;
        cf.defaultArgExpr = clang_expr_to_string(field->getInClassInitializer());
      }

      // Try to get field offset
      if (rd->isCompleteDefinition() && !rd->isDependentType()) {
        unsigned idx = field->getFieldIndex();
        const ASTRecordLayout& layout = ctx.getASTRecordLayout(rd);
        cf.offset = layout.getFieldOffset(idx) / 8; // bits -> bytes
        cf.size   = ctx.getTypeSize(ft) / 8;
      }

      cf.metadata = parse_decl_metadata(field);
      ct.fields.push_back(std::move(cf));
    }

    // ---- Collect methods -------------------------------------------------
    for (auto* method : rd->methods()) {
      // Skip special members, operators, and deleted methods
      if (isa<CXXConstructorDecl>(method) || isa<CXXDestructorDecl>(method)
          || isa<CXXConversionDecl>(method))
        continue;
      if (method->isOverloadedOperator()) continue;
      if (method->isDeleted()) continue;
      if (method->getAccess() != AS_public) continue;
      if (has_no_reflect(method)) continue;

      CollectedMethod cm;
      cm.name       = method->getNameAsString();
      cm.returnType = clang_type_name_no_tag(method->getReturnType().getCanonicalType());
      cm.isStatic   = method->isStatic();
      cm.isConst    = method->isConst();
      cm.isVirtual  = method->isVirtual();
      cm.isPureVirtual = method->isPureVirtual();
      cm.isNoexcept = method->getExceptionSpecType() == EST_BasicNoexcept
                      || method->getExceptionSpecType() == EST_NoexceptTrue;
      cm.accessStr  = access_to_string(method->getAccess());
      cm.metadata   = parse_decl_metadata(method);

      for (unsigned i = 0; i < method->getNumParams(); ++i) {
        auto* p = method->getParamDecl(i);
        CollectedParam cp;
        cp.name     = p->getNameAsString();
        cp.typeName = clang_type_name_no_tag(p->getType().getCanonicalType());
        if (p->hasDefaultArg()) {
          cp.hasDefault = true;
          cp.defaultExpr = clang_expr_to_string(p->getDefaultArg());
        }
        cm.params.push_back(std::move(cp));
      }

      ct.methods.push_back(std::move(cm));
    }

    // ---- Collect base classes --------------------------------------------
    for (auto& base : rd->bases()) {
      CollectedBase cb;
      QualType bt = base.getType().getCanonicalType();
      cb.name       = clang_type_name_no_tag(bt);
      cb.nameNoTag  = cb.name;
      cb.typeHash   = hasher(bt.getAsString());
      cb.accessStr  = access_to_string(base.getAccessSpecifier());
      cb.isVirtual  = base.isVirtual();
      ct.bases.push_back(std::move(cb));
    }

    consumer->collectedTypes.push_back(std::move(ct));
    return true;
  }

  // -----------------------------------------------------------------------
  // ReflectDeclVisitor — enum handling
  // -----------------------------------------------------------------------

  bool ReflectDeclVisitor::VisitEnumDecl(EnumDecl* ed) {
    if (!ed) return true;
    if (!has_reflect_annotation(ed)) return true;
    if (has_no_reflect(ed)) return true;

    FNV1aHash hasher;
    CollectedType ct;
    ct.qualifiedName = ed->getQualifiedNameAsString();
    ct.nameNoTag     = ct.qualifiedName;
    ct.displayName   = ed->getNameAsString();
    ct.typeHash      = hasher(ct.qualifiedName);
    ct.isEnum        = true;
    ct.metadata      = parse_decl_metadata(ed);

    for (auto* val : ed->enumerators()) {
      CollectedEnumValue ev;
      ev.name  = val->getNameAsString();
      ev.value = val->getInitVal().getExtValue();
      ev.metadata = parse_decl_metadata(val);
      ct.enumValues.push_back(std::move(ev));
    }

    if (consumer->verbose)
      llvm::outs() << "[reflect] Enum: " << ct.qualifiedName << "\n";

    consumer->collectedTypes.push_back(std::move(ct));
    return true;
  }

  // -----------------------------------------------------------------------
  // ReflectionASTConsumer
  // -----------------------------------------------------------------------

  ReflectionASTConsumer::ReflectionASTConsumer(
      CodeCompilerState& state, std::string headerPath,
      CompilerInstance& compiler, bool verbose)
      : compilerState(state),
        headerPath(std::move(headerPath)),
        compilerInstance(compiler),
        verbose(verbose) {
    visitor.consumer = this;
  }

  void ReflectionASTConsumer::HandleTranslationUnit(ASTContext& context) {
    astContext = &context;

    // ---- Traverse the AST with the RecursiveASTVisitor ------------------
    visitor.TraverseDecl(context.getTranslationUnitDecl());

    // ---- Generate code ---------------------------------------------------
    // Deduplicate: skip types already emitted by a previous source file.
    std::vector<CollectedType> uniqueTypes;
    for (auto& ct : collectedTypes) {
      if (compilerState.emittedHashes.count(ct.typeHash))
        continue;
      compilerState.emittedHashes[ct.typeHash] = true;
      uniqueTypes.push_back(std::move(ct));
    }

    if (!uniqueTypes.empty()) {
      emit_reflected_header(headerPath, uniqueTypes, verbose);

      // Register types in the compiler state for the final register source.
      for (auto& ct : uniqueTypes)
        compilerState.reflectedTypes.push_back(ct);
    }

    astContext = nullptr;
  }

} // namespace zs::reflect_tool
