// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/scopes.h"

#include <set>

#include "src/accessors.h"
#include "src/ast/ast.h"
#include "src/bootstrapper.h"
#include "src/counters.h"
#include "src/messages.h"
#include "src/objects-inl.h"
#include "src/objects/module-info.h"
#include "src/objects/scope-info.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/preparsed-scope-data.h"

namespace v8 {
namespace internal {

namespace {
void* kDummyPreParserVariable = reinterpret_cast<void*>(0x1);
void* kDummyPreParserLexicalVariable = reinterpret_cast<void*>(0x2);

bool IsLexical(Variable* variable) {
  if (variable == kDummyPreParserLexicalVariable) return true;
  if (variable == kDummyPreParserVariable) return false;
  return IsLexicalVariableMode(variable->mode());
}

}  // namespace

// ----------------------------------------------------------------------------
// Implementation of LocalsMap
//
// Note: We are storing the handle locations as key values in the hash map.
//       When inserting a new variable via Declare(), we rely on the fact that
//       the handle location remains alive for the duration of that variable
//       use. Because a Variable holding a handle with the same location exists
//       this is ensured.

VariableMap::VariableMap(Zone* zone)
    : ZoneHashMap(8, ZoneAllocationPolicy(zone)) {}

Variable* VariableMap::Declare(Zone* zone, Scope* scope,
                               const AstRawString* name, VariableMode mode,
                               VariableKind kind,
                               InitializationFlag initialization_flag,
                               MaybeAssignedFlag maybe_assigned_flag,
                               bool* added) {
  // AstRawStrings are unambiguous, i.e., the same string is always represented
  // by the same AstRawString*.
  // FIXME(marja): fix the type of Lookup.
  Entry* p =
      ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name), name->hash(),
                                  ZoneAllocationPolicy(zone));
  if (added) *added = p->value == nullptr;
  if (p->value == nullptr) {
    // The variable has not been declared yet -> insert it.
    DCHECK_EQ(name, p->key);
    p->value = new (zone) Variable(scope, name, mode, kind, initialization_flag,
                                   maybe_assigned_flag);
  }
  return reinterpret_cast<Variable*>(p->value);
}

Variable* VariableMap::DeclareName(Zone* zone, const AstRawString* name,
                                   VariableMode mode) {
  Entry* p =
      ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name), name->hash(),
                                  ZoneAllocationPolicy(zone));
  if (p->value == nullptr) {
    // The variable has not been declared yet -> insert it.
    DCHECK_EQ(name, p->key);
    p->value =
        mode == VAR ? kDummyPreParserVariable : kDummyPreParserLexicalVariable;
  }
  return reinterpret_cast<Variable*>(p->value);
}

void VariableMap::Remove(Variable* var) {
  const AstRawString* name = var->raw_name();
  ZoneHashMap::Remove(const_cast<AstRawString*>(name), name->hash());
}

void VariableMap::Add(Zone* zone, Variable* var) {
  const AstRawString* name = var->raw_name();
  Entry* p =
      ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name), name->hash(),
                                  ZoneAllocationPolicy(zone));
  DCHECK_NULL(p->value);
  DCHECK_EQ(name, p->key);
  p->value = var;
}

Variable* VariableMap::Lookup(const AstRawString* name) {
  Entry* p = ZoneHashMap::Lookup(const_cast<AstRawString*>(name), name->hash());
  if (p != NULL) {
    DCHECK(reinterpret_cast<const AstRawString*>(p->key) == name);
    DCHECK(p->value != NULL);
    return reinterpret_cast<Variable*>(p->value);
  }
  return NULL;
}

void SloppyBlockFunctionMap::Delegate::set_statement(Statement* statement) {
  if (statement_ != nullptr) {
    statement_->set_statement(statement);
  }
}

SloppyBlockFunctionMap::SloppyBlockFunctionMap(Zone* zone)
    : ZoneHashMap(8, ZoneAllocationPolicy(zone)) {}

void SloppyBlockFunctionMap::Declare(
    Zone* zone, const AstRawString* name,
    SloppyBlockFunctionMap::Delegate* delegate) {
  // AstRawStrings are unambiguous, i.e., the same string is always represented
  // by the same AstRawString*.
  Entry* p =
      ZoneHashMap::LookupOrInsert(const_cast<AstRawString*>(name), name->hash(),
                                  ZoneAllocationPolicy(zone));
  delegate->set_next(static_cast<SloppyBlockFunctionMap::Delegate*>(p->value));
  p->value = delegate;
}

// ----------------------------------------------------------------------------
// Implementation of Scope

Scope::Scope(Zone* zone)
    : zone_(zone),
      outer_scope_(nullptr),
      variables_(zone),
      scope_type_(SCRIPT_SCOPE) {
  SetDefaults();
}

Scope::Scope(Zone* zone, Scope* outer_scope, ScopeType scope_type)
    : zone_(zone),
      outer_scope_(outer_scope),
      variables_(zone),
      scope_type_(scope_type) {
  DCHECK_NE(SCRIPT_SCOPE, scope_type);
  SetDefaults();
  set_language_mode(outer_scope->language_mode());
  force_context_allocation_ =
      !is_function_scope() && outer_scope->has_forced_context_allocation();
  outer_scope_->AddInnerScope(this);
}

Scope::Snapshot::Snapshot(Scope* scope)
    : outer_scope_(scope),
      top_inner_scope_(scope->inner_scope_),
      top_unresolved_(scope->unresolved_),
      top_local_(scope->GetClosureScope()->locals_.end()),
      top_decl_(scope->GetClosureScope()->decls_.end()),
      outer_scope_calls_eval_(scope->scope_calls_eval_) {
  // Reset in order to record eval calls during this Snapshot's lifetime.
  outer_scope_->scope_calls_eval_ = false;
}

Scope::Snapshot::~Snapshot() {
  // Restore previous calls_eval bit if needed.
  if (outer_scope_calls_eval_) {
    outer_scope_->scope_calls_eval_ = true;
  }
}

DeclarationScope::DeclarationScope(Zone* zone,
                                   AstValueFactory* ast_value_factory)
    : Scope(zone),
      function_kind_(kNormalFunction),
      params_(4, zone),
      sloppy_block_function_map_(zone) {
  DCHECK_EQ(scope_type_, SCRIPT_SCOPE);
  SetDefaults();

  // Make sure that if we don't find the global 'this', it won't be declared as
  // a regular dynamic global by predeclaring it with the right variable kind.
  DeclareDynamicGlobal(ast_value_factory->this_string(), THIS_VARIABLE);
}

DeclarationScope::DeclarationScope(Zone* zone, Scope* outer_scope,
                                   ScopeType scope_type,
                                   FunctionKind function_kind)
    : Scope(zone, outer_scope, scope_type),
      function_kind_(function_kind),
      params_(4, zone),
      sloppy_block_function_map_(zone) {
  DCHECK_NE(scope_type, SCRIPT_SCOPE);
  SetDefaults();
  asm_function_ = outer_scope_->IsAsmModule();
}

ModuleScope::ModuleScope(DeclarationScope* script_scope,
                         AstValueFactory* ast_value_factory)
    : DeclarationScope(ast_value_factory->zone(), script_scope, MODULE_SCOPE,
                       kModule) {
  Zone* zone = ast_value_factory->zone();
  module_descriptor_ = new (zone) ModuleDescriptor(zone);
  set_language_mode(STRICT);
  DeclareThis(ast_value_factory);
}

ModuleScope::ModuleScope(Isolate* isolate, Handle<ScopeInfo> scope_info,
                         AstValueFactory* avfactory)
    : DeclarationScope(avfactory->zone(), MODULE_SCOPE, scope_info) {
  Zone* zone = avfactory->zone();
  Handle<ModuleInfo> module_info(scope_info->ModuleDescriptorInfo(), isolate);

  set_language_mode(STRICT);
  module_descriptor_ = new (zone) ModuleDescriptor(zone);

  // Deserialize special exports.
  Handle<FixedArray> special_exports(module_info->special_exports(), isolate);
  for (int i = 0, n = special_exports->length(); i < n; ++i) {
    Handle<ModuleInfoEntry> serialized_entry(
        ModuleInfoEntry::cast(special_exports->get(i)), isolate);
    module_descriptor_->AddSpecialExport(
        ModuleDescriptor::Entry::Deserialize(isolate, avfactory,
                                             serialized_entry),
        avfactory->zone());
  }

  // Deserialize regular exports.
  module_descriptor_->DeserializeRegularExports(isolate, avfactory,
                                                module_info);

  // Deserialize namespace imports.
  Handle<FixedArray> namespace_imports(module_info->namespace_imports(),
                                       isolate);
  for (int i = 0, n = namespace_imports->length(); i < n; ++i) {
    Handle<ModuleInfoEntry> serialized_entry(
        ModuleInfoEntry::cast(namespace_imports->get(i)), isolate);
    module_descriptor_->AddNamespaceImport(
        ModuleDescriptor::Entry::Deserialize(isolate, avfactory,
                                             serialized_entry),
        avfactory->zone());
  }

  // Deserialize regular imports.
  Handle<FixedArray> regular_imports(module_info->regular_imports(), isolate);
  for (int i = 0, n = regular_imports->length(); i < n; ++i) {
    Handle<ModuleInfoEntry> serialized_entry(
        ModuleInfoEntry::cast(regular_imports->get(i)), isolate);
    module_descriptor_->AddRegularImport(ModuleDescriptor::Entry::Deserialize(
        isolate, avfactory, serialized_entry));
  }
}

Scope::Scope(Zone* zone, ScopeType scope_type, Handle<ScopeInfo> scope_info)
    : zone_(zone),
      outer_scope_(nullptr),
      variables_(zone),
      scope_info_(scope_info),
      scope_type_(scope_type) {
  DCHECK(!scope_info.is_null());
  SetDefaults();
#ifdef DEBUG
  already_resolved_ = true;
#endif
  if (scope_info->CallsEval()) RecordEvalCall();
  set_language_mode(scope_info->language_mode());
  num_heap_slots_ = scope_info->ContextLength();
  DCHECK_LE(Context::MIN_CONTEXT_SLOTS, num_heap_slots_);
}

DeclarationScope::DeclarationScope(Zone* zone, ScopeType scope_type,
                                   Handle<ScopeInfo> scope_info)
    : Scope(zone, scope_type, scope_info),
      function_kind_(scope_info->function_kind()),
      params_(0, zone),
      sloppy_block_function_map_(zone) {
  DCHECK_NE(scope_type, SCRIPT_SCOPE);
  SetDefaults();
}

Scope::Scope(Zone* zone, const AstRawString* catch_variable_name,
             MaybeAssignedFlag maybe_assigned, Handle<ScopeInfo> scope_info)
    : zone_(zone),
      outer_scope_(nullptr),
      variables_(zone),
      scope_info_(scope_info),
      scope_type_(CATCH_SCOPE) {
  SetDefaults();
#ifdef DEBUG
  already_resolved_ = true;
#endif
  // Cache the catch variable, even though it's also available via the
  // scope_info, as the parser expects that a catch scope always has the catch
  // variable as first and only variable.
  Variable* variable = Declare(zone, catch_variable_name, VAR, NORMAL_VARIABLE,
                               kCreatedInitialized, maybe_assigned);
  AllocateHeapSlot(variable);
}

void DeclarationScope::SetDefaults() {
  is_declaration_scope_ = true;
  has_simple_parameters_ = true;
  asm_module_ = false;
  asm_function_ = false;
  force_eager_compilation_ = false;
  has_arguments_parameter_ = false;
  scope_uses_super_property_ = false;
  has_rest_ = false;
  receiver_ = nullptr;
  new_target_ = nullptr;
  function_ = nullptr;
  arguments_ = nullptr;
  rare_data_ = nullptr;
  should_eager_compile_ = false;
  was_lazily_parsed_ = false;
#ifdef DEBUG
  DeclarationScope* outer_declaration_scope =
      outer_scope_ ? outer_scope_->GetDeclarationScope() : nullptr;
  is_being_lazily_parsed_ =
      outer_declaration_scope ? outer_declaration_scope->is_being_lazily_parsed_
                              : false;
#endif
}

void Scope::SetDefaults() {
#ifdef DEBUG
  scope_name_ = nullptr;
  already_resolved_ = false;
  needs_migration_ = false;
#endif
  inner_scope_ = nullptr;
  sibling_ = nullptr;
  unresolved_ = nullptr;

  start_position_ = kNoSourcePosition;
  end_position_ = kNoSourcePosition;

  num_stack_slots_ = 0;
  num_heap_slots_ = Context::MIN_CONTEXT_SLOTS;

  set_language_mode(SLOPPY);

  scope_calls_eval_ = false;
  scope_nonlinear_ = false;
  is_hidden_ = false;
  is_debug_evaluate_scope_ = false;

  inner_scope_calls_eval_ = false;
  force_context_allocation_ = false;

  is_declaration_scope_ = false;
}

bool Scope::HasSimpleParameters() {
  DeclarationScope* scope = GetClosureScope();
  return !scope->is_function_scope() || scope->has_simple_parameters();
}

bool DeclarationScope::ShouldEagerCompile() const {
  return force_eager_compilation_ || should_eager_compile_;
}

void DeclarationScope::set_should_eager_compile() {
  should_eager_compile_ = !was_lazily_parsed_;
}

void DeclarationScope::set_asm_module() {
  asm_module_ = true;
  // Mark any existing inner function scopes as asm function scopes.
  for (Scope* inner = inner_scope_; inner != nullptr; inner = inner->sibling_) {
    if (inner->is_function_scope()) {
      inner->AsDeclarationScope()->set_asm_function();
    }
  }
}

bool Scope::IsAsmModule() const {
  return is_function_scope() && AsDeclarationScope()->asm_module();
}

bool Scope::IsAsmFunction() const {
  return is_function_scope() && AsDeclarationScope()->asm_function();
}

Scope* Scope::DeserializeScopeChain(Isolate* isolate, Zone* zone,
                                    ScopeInfo* scope_info,
                                    DeclarationScope* script_scope,
                                    AstValueFactory* ast_value_factory,
                                    DeserializationMode deserialization_mode) {
  // Reconstruct the outer scope chain from a closure's context chain.
  Scope* current_scope = nullptr;
  Scope* innermost_scope = nullptr;
  Scope* outer_scope = nullptr;
  while (scope_info) {
    if (scope_info->scope_type() == WITH_SCOPE) {
      // For scope analysis, debug-evaluate is equivalent to a with scope.
      outer_scope = new (zone) Scope(zone, WITH_SCOPE, handle(scope_info));

      // TODO(yangguo): Remove once debug-evaluate properly keeps track of the
      // function scope in which we are evaluating.
      if (scope_info->IsDebugEvaluateScope()) {
        outer_scope->set_is_debug_evaluate_scope();
      }
    } else if (scope_info->scope_type() == SCRIPT_SCOPE) {
      // If we reach a script scope, it's the outermost scope. Install the
      // scope info of this script context onto the existing script scope to
      // avoid nesting script scopes.
      if (deserialization_mode == DeserializationMode::kIncludingVariables) {
        script_scope->SetScriptScopeInfo(handle(scope_info));
      }
      DCHECK(!scope_info->HasOuterScopeInfo());
      break;
    } else if (scope_info->scope_type() == FUNCTION_SCOPE) {
      outer_scope =
          new (zone) DeclarationScope(zone, FUNCTION_SCOPE, handle(scope_info));
      if (scope_info->IsAsmFunction())
        outer_scope->AsDeclarationScope()->set_asm_function();
      if (scope_info->IsAsmModule())
        outer_scope->AsDeclarationScope()->set_asm_module();
    } else if (scope_info->scope_type() == EVAL_SCOPE) {
      outer_scope =
          new (zone) DeclarationScope(zone, EVAL_SCOPE, handle(scope_info));
    } else if (scope_info->scope_type() == BLOCK_SCOPE) {
      if (scope_info->is_declaration_scope()) {
        outer_scope =
            new (zone) DeclarationScope(zone, BLOCK_SCOPE, handle(scope_info));
      } else {
        outer_scope = new (zone) Scope(zone, BLOCK_SCOPE, handle(scope_info));
      }
    } else if (scope_info->scope_type() == MODULE_SCOPE) {
      outer_scope = new (zone)
          ModuleScope(isolate, handle(scope_info), ast_value_factory);
    } else {
      DCHECK_EQ(scope_info->scope_type(), CATCH_SCOPE);
      DCHECK_EQ(scope_info->LocalCount(), 1);
      DCHECK_EQ(scope_info->ContextLocalCount(), 1);
      DCHECK_EQ(scope_info->ContextLocalMode(0), VAR);
      DCHECK_EQ(scope_info->ContextLocalInitFlag(0), kCreatedInitialized);
      String* name = scope_info->ContextLocalName(0);
      MaybeAssignedFlag maybe_assigned =
          scope_info->ContextLocalMaybeAssignedFlag(0);
      outer_scope = new (zone)
          Scope(zone, ast_value_factory->GetString(handle(name, isolate)),
                maybe_assigned, handle(scope_info));
    }
    if (deserialization_mode == DeserializationMode::kScopesOnly) {
      outer_scope->scope_info_ = Handle<ScopeInfo>::null();
    }
    if (current_scope != nullptr) {
      outer_scope->AddInnerScope(current_scope);
    }
    current_scope = outer_scope;
    if (innermost_scope == nullptr) innermost_scope = current_scope;
    scope_info = scope_info->HasOuterScopeInfo() ? scope_info->OuterScopeInfo()
                                                 : nullptr;
  }

  if (innermost_scope == nullptr) return script_scope;
  script_scope->AddInnerScope(current_scope);
  return innermost_scope;
}

DeclarationScope* Scope::AsDeclarationScope() {
  DCHECK(is_declaration_scope());
  return static_cast<DeclarationScope*>(this);
}

const DeclarationScope* Scope::AsDeclarationScope() const {
  DCHECK(is_declaration_scope());
  return static_cast<const DeclarationScope*>(this);
}

ModuleScope* Scope::AsModuleScope() {
  DCHECK(is_module_scope());
  return static_cast<ModuleScope*>(this);
}

const ModuleScope* Scope::AsModuleScope() const {
  DCHECK(is_module_scope());
  return static_cast<const ModuleScope*>(this);
}

int Scope::num_parameters() const {
  return is_declaration_scope() ? AsDeclarationScope()->num_parameters() : 0;
}

void DeclarationScope::DeclareSloppyBlockFunction(
    const AstRawString* name, Scope* scope,
    SloppyBlockFunctionStatement* statement) {
  auto* delegate =
      new (zone()) SloppyBlockFunctionMap::Delegate(scope, statement);
  sloppy_block_function_map_.Declare(zone(), name, delegate);
}

void DeclarationScope::HoistSloppyBlockFunctions(AstNodeFactory* factory) {
  DCHECK(is_sloppy(language_mode()));
  DCHECK(is_function_scope() || is_eval_scope() || is_script_scope() ||
         (is_block_scope() && outer_scope()->is_function_scope()));
  DCHECK(HasSimpleParameters() || is_block_scope() || is_being_lazily_parsed_);
  DCHECK_EQ(factory == nullptr, is_being_lazily_parsed_);

  bool has_simple_parameters = HasSimpleParameters();
  // For each variable which is used as a function declaration in a sloppy
  // block,
  SloppyBlockFunctionMap* map = sloppy_block_function_map();
  for (ZoneHashMap::Entry* p = map->Start(); p != nullptr; p = map->Next(p)) {
    AstRawString* name = static_cast<AstRawString*>(p->key);

    // If the variable wouldn't conflict with a lexical declaration
    // or parameter,

    // Check if there's a conflict with a parameter.
    // This depends on the fact that functions always have a scope solely to
    // hold complex parameters, and the names local to that scope are
    // precisely the names of the parameters. IsDeclaredParameter(name) does
    // not hold for names declared by complex parameters, nor are those
    // bindings necessarily declared lexically, so we have to check for them
    // explicitly. On the other hand, if there are not complex parameters,
    // it is sufficient to just check IsDeclaredParameter.
    if (!has_simple_parameters) {
      if (outer_scope_->LookupLocal(name) != nullptr) {
        continue;
      }
    } else {
      if (IsDeclaredParameter(name)) {
        continue;
      }
    }

    Variable* created_variable = nullptr;

    // Write in assignments to var for each block-scoped function declaration
    auto delegates = static_cast<SloppyBlockFunctionMap::Delegate*>(p->value);

    DeclarationScope* decl_scope = this;
    while (decl_scope->is_eval_scope()) {
      decl_scope = decl_scope->outer_scope()->GetDeclarationScope();
    }
    Scope* outer_scope = decl_scope->outer_scope();

    for (SloppyBlockFunctionMap::Delegate* delegate = delegates;
         delegate != nullptr; delegate = delegate->next()) {
      // Check if there's a conflict with a lexical declaration
      Scope* query_scope = delegate->scope()->outer_scope();
      Variable* var = nullptr;
      bool should_hoist = true;

      // Note that we perform this loop for each delegate named 'name',
      // which may duplicate work if those delegates share scopes.
      // It is not sufficient to just do a Lookup on query_scope: for
      // example, that does not prevent hoisting of the function in
      // `{ let e; try {} catch (e) { function e(){} } }`
      do {
        var = query_scope->LookupLocal(name);
        if (var != nullptr && IsLexical(var)) {
          should_hoist = false;
          break;
        }
        query_scope = query_scope->outer_scope();
      } while (query_scope != outer_scope);

      if (!should_hoist) continue;

      // Declare a var-style binding for the function in the outer scope
      if (factory) {
        DCHECK(!is_being_lazily_parsed_);
        if (created_variable == nullptr) {
          VariableProxy* proxy =
              factory->NewVariableProxy(name, NORMAL_VARIABLE);
          auto declaration =
              factory->NewVariableDeclaration(proxy, this, kNoSourcePosition);
          // Based on the preceding check, it doesn't matter what we pass as
          // allow_harmony_restrictive_generators and
          // sloppy_mode_block_scope_function_redefinition.
          bool ok = true;
          created_variable = DeclareVariable(
              declaration, VAR, Variable::DefaultInitializationFlag(VAR), false,
              nullptr, &ok);
          CHECK(ok);  // Based on the preceding check, this should not fail
        }

        Expression* assignment = factory->NewAssignment(
            Token::ASSIGN, NewUnresolved(factory, name),
            delegate->scope()->NewUnresolved(factory, name), kNoSourcePosition);
        Statement* statement =
            factory->NewExpressionStatement(assignment, kNoSourcePosition);
        delegate->set_statement(statement);
      } else {
        DCHECK(is_being_lazily_parsed_);
        if (created_variable == nullptr) {
          created_variable = DeclareVariableName(name, VAR);
          if (created_variable != kDummyPreParserVariable &&
              created_variable != kDummyPreParserLexicalVariable) {
            DCHECK(FLAG_preparser_scope_analysis);
            created_variable->set_maybe_assigned();
          }
        }
      }
    }
  }
}

void DeclarationScope::Analyze(ParseInfo* info, AnalyzeMode mode) {
  RuntimeCallTimerScope runtimeTimer(info->isolate(),
                                     &RuntimeCallStats::CompileScopeAnalysis);
  DCHECK(info->literal() != NULL);
  DeclarationScope* scope = info->literal()->scope();

  Handle<ScopeInfo> outer_scope_info;
  if (info->maybe_outer_scope_info().ToHandle(&outer_scope_info)) {
    if (scope->outer_scope()) {
      DeclarationScope* script_scope = new (info->zone())
          DeclarationScope(info->zone(), info->ast_value_factory());
      info->set_script_scope(script_scope);
      scope->ReplaceOuterScope(Scope::DeserializeScopeChain(
          info->isolate(), info->zone(), *outer_scope_info, script_scope,
          info->ast_value_factory(),
          Scope::DeserializationMode::kIncludingVariables));
    } else {
      DCHECK_EQ(outer_scope_info->scope_type(), SCRIPT_SCOPE);
      scope->SetScriptScopeInfo(outer_scope_info);
    }
  }

  if (scope->is_eval_scope() && is_sloppy(scope->language_mode())) {
    AstNodeFactory factory(info->ast_value_factory());
    scope->HoistSloppyBlockFunctions(&factory);
  }

  // We are compiling one of four cases:
  // 1) top-level code,
  // 2) a function/eval/module on the top-level
  // 3) a function/eval in a scope that was already resolved.
  // 4) an asm.js function
  DCHECK(scope->scope_type() == SCRIPT_SCOPE ||
         scope->outer_scope()->scope_type() == SCRIPT_SCOPE ||
         scope->outer_scope()->already_resolved_ ||
         (info->asm_function_scope() && scope->is_function_scope()));

  // The outer scope is never lazy.
  scope->set_should_eager_compile();

  scope->AllocateVariables(info, mode);

  // Ensuring that the outer script scope has a scope info avoids having
  // special case for native contexts vs other contexts.
  if (info->script_scope()->scope_info_.is_null()) {
    info->script_scope()->scope_info_ =
        handle(ScopeInfo::Empty(info->isolate()));
  }

#ifdef DEBUG
  if (info->script_is_native() ? FLAG_print_builtin_scopes
                               : FLAG_print_scopes) {
    PrintF("Global scope:\n");
    scope->Print();
  }
  scope->CheckScopePositions();
  scope->CheckZones();
#endif
}

void DeclarationScope::DeclareThis(AstValueFactory* ast_value_factory) {
  DCHECK(!already_resolved_);
  DCHECK(is_declaration_scope());
  DCHECK(has_this_declaration());

  bool derived_constructor = IsDerivedConstructor(function_kind_);
  Variable* var =
      Declare(zone(), ast_value_factory->this_string(),
              derived_constructor ? CONST : VAR, THIS_VARIABLE,
              derived_constructor ? kNeedsInitialization : kCreatedInitialized);
  receiver_ = var;
}

void DeclarationScope::DeclareArguments(AstValueFactory* ast_value_factory) {
  DCHECK(is_function_scope());
  DCHECK(!is_arrow_scope());

  arguments_ = LookupLocal(ast_value_factory->arguments_string());
  if (arguments_ == nullptr) {
    // Declare 'arguments' variable which exists in all non arrow functions.
    // Note that it might never be accessed, in which case it won't be
    // allocated during variable allocation.
    arguments_ = Declare(zone(), ast_value_factory->arguments_string(), VAR);
  } else if (IsLexical(arguments_)) {
    // Check if there's lexically declared variable named arguments to avoid
    // redeclaration. See ES#sec-functiondeclarationinstantiation, step 20.
    arguments_ = nullptr;
  }
}

void DeclarationScope::DeclareDefaultFunctionVariables(
    AstValueFactory* ast_value_factory) {
  DCHECK(is_function_scope());
  DCHECK(!is_arrow_scope());

  DeclareThis(ast_value_factory);
  new_target_ = Declare(zone(), ast_value_factory->new_target_string(), CONST);

  if (IsConciseMethod(function_kind_) || IsClassConstructor(function_kind_) ||
      IsAccessorFunction(function_kind_)) {
    EnsureRareData()->this_function =
        Declare(zone(), ast_value_factory->this_function_string(), CONST);
  }
}

Variable* DeclarationScope::DeclareFunctionVar(const AstRawString* name) {
  DCHECK(is_function_scope());
  DCHECK_NULL(function_);
  DCHECK_NULL(variables_.Lookup(name));
  VariableKind kind = is_sloppy(language_mode()) ? SLOPPY_FUNCTION_NAME_VARIABLE
                                                 : NORMAL_VARIABLE;
  function_ =
      new (zone()) Variable(this, name, CONST, kind, kCreatedInitialized);
  if (calls_sloppy_eval()) {
    NonLocal(name, DYNAMIC);
  } else {
    variables_.Add(zone(), function_);
  }
  return function_;
}

Variable* DeclarationScope::DeclareGeneratorObjectVar(
    const AstRawString* name) {
  DCHECK(is_function_scope() || is_module_scope());
  DCHECK_NULL(generator_object_var());

  Variable* result = EnsureRareData()->generator_object =
      NewTemporary(name, kNotAssigned);
  result->set_is_used();
  return result;
}

Variable* DeclarationScope::DeclarePromiseVar(const AstRawString* name) {
  DCHECK(is_function_scope());
  DCHECK_NULL(promise_var());
  Variable* result = EnsureRareData()->promise = NewTemporary(name);
  result->set_is_used();
  return result;
}

bool Scope::HasBeenRemoved() const {
  if (sibling() == this) {
    DCHECK_NULL(inner_scope_);
    DCHECK(is_block_scope());
    return true;
  }
  return false;
}

Scope* Scope::GetUnremovedScope() {
  Scope* scope = this;
  while (scope != nullptr && scope->HasBeenRemoved()) {
    scope = scope->outer_scope();
  }
  DCHECK_NOT_NULL(scope);
  return scope;
}

Scope* Scope::FinalizeBlockScope() {
  DCHECK(is_block_scope());
  DCHECK(!HasBeenRemoved());

  if (variables_.occupancy() > 0 ||
      (is_declaration_scope() && calls_sloppy_eval())) {
    return this;
  }

  // Remove this scope from outer scope.
  outer_scope()->RemoveInnerScope(this);

  // Reparent inner scopes.
  if (inner_scope_ != nullptr) {
    Scope* scope = inner_scope_;
    scope->outer_scope_ = outer_scope();
    while (scope->sibling_ != nullptr) {
      scope = scope->sibling_;
      scope->outer_scope_ = outer_scope();
    }
    scope->sibling_ = outer_scope()->inner_scope_;
    outer_scope()->inner_scope_ = inner_scope_;
    inner_scope_ = nullptr;
  }

  // Move unresolved variables
  if (unresolved_ != nullptr) {
    if (outer_scope()->unresolved_ != nullptr) {
      VariableProxy* unresolved = unresolved_;
      while (unresolved->next_unresolved() != nullptr) {
        unresolved = unresolved->next_unresolved();
      }
      unresolved->set_next_unresolved(outer_scope()->unresolved_);
    }
    outer_scope()->unresolved_ = unresolved_;
    unresolved_ = nullptr;
  }

  if (scope_calls_eval_) outer_scope()->scope_calls_eval_ = true;
  if (inner_scope_calls_eval_) outer_scope()->inner_scope_calls_eval_ = true;

  // This block does not need a context.
  num_heap_slots_ = 0;

  // Mark scope as removed by making it its own sibling.
  sibling_ = this;
  DCHECK(HasBeenRemoved());

  return nullptr;
}

void DeclarationScope::AddLocal(Variable* var) {
  DCHECK(!already_resolved_);
  // Temporaries are only placed in ClosureScopes.
  DCHECK_EQ(GetClosureScope(), this);
  locals_.Add(var);
}

Variable* Scope::Declare(Zone* zone, const AstRawString* name,
                         VariableMode mode, VariableKind kind,
                         InitializationFlag initialization_flag,
                         MaybeAssignedFlag maybe_assigned_flag) {
  bool added;
  Variable* var =
      variables_.Declare(zone, this, name, mode, kind, initialization_flag,
                         maybe_assigned_flag, &added);
  if (added) locals_.Add(var);
  return var;
}

void Scope::Snapshot::Reparent(DeclarationScope* new_parent) const {
  DCHECK_EQ(new_parent, outer_scope_->inner_scope_);
  DCHECK_EQ(new_parent->outer_scope_, outer_scope_);
  DCHECK_EQ(new_parent, new_parent->GetClosureScope());
  DCHECK_NULL(new_parent->inner_scope_);
  DCHECK_NULL(new_parent->unresolved_);
  DCHECK(new_parent->locals_.is_empty());
  Scope* inner_scope = new_parent->sibling_;
  if (inner_scope != top_inner_scope_) {
    for (; inner_scope->sibling() != top_inner_scope_;
         inner_scope = inner_scope->sibling()) {
      inner_scope->outer_scope_ = new_parent;
      if (inner_scope->inner_scope_calls_eval_) {
        new_parent->inner_scope_calls_eval_ = true;
      }
      DCHECK_NE(inner_scope, new_parent);
    }
    inner_scope->outer_scope_ = new_parent;
    if (inner_scope->inner_scope_calls_eval_) {
      new_parent->inner_scope_calls_eval_ = true;
    }
    new_parent->inner_scope_ = new_parent->sibling_;
    inner_scope->sibling_ = nullptr;
    // Reset the sibling rather than the inner_scope_ since we
    // want to keep new_parent there.
    new_parent->sibling_ = top_inner_scope_;
  }

  if (outer_scope_->unresolved_ != top_unresolved_) {
    VariableProxy* last = outer_scope_->unresolved_;
    while (last->next_unresolved() != top_unresolved_) {
      last = last->next_unresolved();
    }
    last->set_next_unresolved(nullptr);
    new_parent->unresolved_ = outer_scope_->unresolved_;
    outer_scope_->unresolved_ = top_unresolved_;
  }

  // TODO(verwaest): This currently only moves do-expression declared variables
  // in default arguments that weren't already previously declared with the same
  // name in the closure-scope. See
  // test/mjsunit/harmony/default-parameter-do-expression.js.
  DeclarationScope* outer_closure = outer_scope_->GetClosureScope();

  new_parent->locals_.MoveTail(outer_closure->locals(), top_local_);
  for (Variable* local : new_parent->locals_) {
    DCHECK(local->mode() == TEMPORARY || local->mode() == VAR);
    DCHECK_EQ(local->scope(), local->scope()->GetClosureScope());
    DCHECK_NE(local->scope(), new_parent);
    local->set_scope(new_parent);
    if (local->mode() == VAR) {
      outer_closure->variables_.Remove(local);
      new_parent->variables_.Add(new_parent->zone(), local);
    }
  }
  outer_closure->locals_.Rewind(top_local_);
  outer_closure->decls_.Rewind(top_decl_);

  // Move eval calls since Snapshot's creation into new_parent.
  if (outer_scope_->scope_calls_eval_) {
    new_parent->scope_calls_eval_ = true;
  }
  // Reset the outer_scope's eval state. It will be restored to its
  // original value as necessary in the destructor of this class.
  outer_scope_->scope_calls_eval_ = false;
}

void Scope::ReplaceOuterScope(Scope* outer) {
  DCHECK_NOT_NULL(outer);
  DCHECK_NOT_NULL(outer_scope_);
  DCHECK(!already_resolved_);
  outer_scope_->RemoveInnerScope(this);
  outer->AddInnerScope(this);
  outer_scope_ = outer;
}

Variable* Scope::LookupInScopeInfo(const AstRawString* name) {
  Handle<String> name_handle = name->string();
  // The Scope is backed up by ScopeInfo. This means it cannot operate in a
  // heap-independent mode, and all strings must be internalized immediately. So
  // it's ok to get the Handle<String> here.
  // If we have a serialized scope info, we might find the variable there.
  // There should be no local slot with the given name.
  DCHECK_LT(scope_info_->StackSlotIndex(*name_handle), 0);

  bool found = false;

  VariableLocation location;
  int index;
  VariableMode mode;
  InitializationFlag init_flag;
  MaybeAssignedFlag maybe_assigned_flag;

  {
    location = VariableLocation::CONTEXT;
    index = ScopeInfo::ContextSlotIndex(scope_info_, name_handle, &mode,
                                        &init_flag, &maybe_assigned_flag);
    found = index >= 0;
  }

  if (!found && scope_type() == MODULE_SCOPE) {
    location = VariableLocation::MODULE;
    index = scope_info_->ModuleIndex(name_handle, &mode, &init_flag,
                                     &maybe_assigned_flag);
    found = index != 0;
  }

  if (!found) {
    index = scope_info_->FunctionContextSlotIndex(*name_handle);
    if (index < 0) return nullptr;  // Nowhere found.
    Variable* var = AsDeclarationScope()->DeclareFunctionVar(name);
    DCHECK_EQ(CONST, var->mode());
    var->AllocateTo(VariableLocation::CONTEXT, index);
    return variables_.Lookup(name);
  }

  VariableKind kind = NORMAL_VARIABLE;
  if (location == VariableLocation::CONTEXT &&
      index == scope_info_->ReceiverContextSlotIndex()) {
    kind = THIS_VARIABLE;
  }
  // TODO(marja, rossberg): Correctly declare FUNCTION, CLASS, NEW_TARGET, and
  // ARGUMENTS bindings as their corresponding VariableKind.

  Variable* var = variables_.Declare(zone(), this, name, mode, kind, init_flag,
                                     maybe_assigned_flag);
  var->AllocateTo(location, index);
  return var;
}

Variable* Scope::Lookup(const AstRawString* name) {
  for (Scope* scope = this;
       scope != NULL;
       scope = scope->outer_scope()) {
    Variable* var = scope->LookupLocal(name);
    if (var != NULL) return var;
  }
  return NULL;
}

Variable* DeclarationScope::DeclareParameter(
    const AstRawString* name, VariableMode mode, bool is_optional, bool is_rest,
    bool* is_duplicate, AstValueFactory* ast_value_factory) {
  DCHECK(!already_resolved_);
  DCHECK(is_function_scope() || is_module_scope());
  DCHECK(!has_rest_);
  DCHECK(!is_optional || !is_rest);
  DCHECK(!is_being_lazily_parsed_);
  DCHECK(!was_lazily_parsed_);
  Variable* var;
  if (mode == TEMPORARY) {
    var = NewTemporary(name);
  } else {
    DCHECK_EQ(mode, VAR);
    var = Declare(zone(), name, mode);
    // TODO(wingo): Avoid O(n^2) check.
    *is_duplicate = IsDeclaredParameter(name);
  }
  has_rest_ = is_rest;
  params_.Add(var, zone());
  if (name == ast_value_factory->arguments_string()) {
    has_arguments_parameter_ = true;
  }
  return var;
}

Variable* DeclarationScope::DeclareParameterName(
    const AstRawString* name, bool is_rest,
    AstValueFactory* ast_value_factory) {
  DCHECK(!already_resolved_);
  DCHECK(is_function_scope() || is_module_scope());
  DCHECK(!has_rest_ || is_rest);
  DCHECK(is_being_lazily_parsed_);
  has_rest_ = is_rest;
  if (name == ast_value_factory->arguments_string()) {
    has_arguments_parameter_ = true;
  }
  if (FLAG_preparser_scope_analysis) {
    Variable* var = Declare(zone(), name, VAR);
    params_.Add(var, zone());
    return var;
  }
  DeclareVariableName(name, VAR);
  return nullptr;
}

Variable* Scope::DeclareLocal(const AstRawString* name, VariableMode mode,
                              InitializationFlag init_flag, VariableKind kind,
                              MaybeAssignedFlag maybe_assigned_flag) {
  DCHECK(!already_resolved_);
  // This function handles VAR, LET, and CONST modes.  DYNAMIC variables are
  // introduced during variable allocation, and TEMPORARY variables are
  // allocated via NewTemporary().
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK_IMPLIES(GetDeclarationScope()->is_being_lazily_parsed(),
                 mode == VAR || mode == LET || mode == CONST);
  DCHECK(!GetDeclarationScope()->was_lazily_parsed());
  return Declare(zone(), name, mode, kind, init_flag, maybe_assigned_flag);
}

Variable* Scope::DeclareVariable(
    Declaration* declaration, VariableMode mode, InitializationFlag init,
    bool allow_harmony_restrictive_generators,
    bool* sloppy_mode_block_scope_function_redefinition, bool* ok) {
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK(!already_resolved_);
  DCHECK(!GetDeclarationScope()->is_being_lazily_parsed());
  DCHECK(!GetDeclarationScope()->was_lazily_parsed());

  if (mode == VAR && !is_declaration_scope()) {
    return GetDeclarationScope()->DeclareVariable(
        declaration, mode, init, allow_harmony_restrictive_generators,
        sloppy_mode_block_scope_function_redefinition, ok);
  }
  DCHECK(!is_catch_scope());
  DCHECK(!is_with_scope());
  DCHECK(is_declaration_scope() ||
         (IsLexicalVariableMode(mode) && is_block_scope()));

  VariableProxy* proxy = declaration->proxy();
  DCHECK(proxy->raw_name() != NULL);
  const AstRawString* name = proxy->raw_name();
  bool is_function_declaration = declaration->IsFunctionDeclaration();

  // Pessimistically assume that top-level variables will be assigned.
  //
  // Top-level variables in a script can be accessed by other scripts or even
  // become global properties. While this does not apply to top-level variables
  // in a module (assuming they are not exported), we must still mark these as
  // assigned because they might be accessed by a lazily parsed top-level
  // function, which, for efficiency, we preparse without variable tracking.
  if (is_script_scope() || is_module_scope()) {
    if (mode != CONST) proxy->set_is_assigned();
  }

  Variable* var = nullptr;
  if (is_eval_scope() && is_sloppy(language_mode()) && mode == VAR) {
    // In a var binding in a sloppy direct eval, pollute the enclosing scope
    // with this new binding by doing the following:
    // The proxy is bound to a lookup variable to force a dynamic declaration
    // using the DeclareEvalVar or DeclareEvalFunction runtime functions.
    var = new (zone())
        Variable(this, name, mode, NORMAL_VARIABLE, init, kMaybeAssigned);
    var->AllocateTo(VariableLocation::LOOKUP, -1);
  } else {
    // Declare the variable in the declaration scope.
    var = LookupLocal(name);
    if (var == NULL) {
      // Declare the name.
      VariableKind kind = NORMAL_VARIABLE;
      if (is_function_declaration) {
        kind = FUNCTION_VARIABLE;
      }
      var = DeclareLocal(name, mode, init, kind, kNotAssigned);
    } else if (IsLexicalVariableMode(mode) ||
               IsLexicalVariableMode(var->mode())) {
      // Allow duplicate function decls for web compat, see bug 4693.
      bool duplicate_allowed = false;
      if (is_sloppy(language_mode()) && is_function_declaration &&
          var->is_function()) {
        DCHECK(IsLexicalVariableMode(mode) &&
               IsLexicalVariableMode(var->mode()));
        // If the duplication is allowed, then the var will show up
        // in the SloppyBlockFunctionMap and the new FunctionKind
        // will be a permitted duplicate.
        FunctionKind function_kind =
            declaration->AsFunctionDeclaration()->fun()->kind();
        duplicate_allowed =
            GetDeclarationScope()->sloppy_block_function_map()->Lookup(
                const_cast<AstRawString*>(name), name->hash()) != nullptr &&
            !IsAsyncFunction(function_kind) &&
            !(allow_harmony_restrictive_generators &&
              IsGeneratorFunction(function_kind));
      }
      if (duplicate_allowed) {
        *sloppy_mode_block_scope_function_redefinition = true;
      } else {
        // The name was declared in this scope before; check for conflicting
        // re-declarations. We have a conflict if either of the declarations
        // is not a var (in script scope, we also have to ignore legacy const
        // for compatibility). There is similar code in runtime.cc in the
        // Declare functions. The function CheckConflictingVarDeclarations
        // checks for var and let bindings from different scopes whereas this
        // is a check for conflicting declarations within the same scope. This
        // check also covers the special case
        //
        // function () { let x; { var x; } }
        //
        // because the var declaration is hoisted to the function scope where
        // 'x' is already bound.
        DCHECK(IsDeclaredVariableMode(var->mode()));
        // In harmony we treat re-declarations as early errors. See
        // ES5 16 for a definition of early errors.
        *ok = false;
        return nullptr;
      }
    } else if (mode == VAR) {
      var->set_maybe_assigned();
    }
  }
  DCHECK_NOT_NULL(var);

  // We add a declaration node for every declaration. The compiler
  // will only generate code if necessary. In particular, declarations
  // for inner local variables that do not represent functions won't
  // result in any generated code.
  //
  // This will lead to multiple declaration nodes for the
  // same variable if it is declared several times. This is not a
  // semantic issue, but it may be a performance issue since it may
  // lead to repeated DeclareEvalVar or DeclareEvalFunction calls.
  decls_.Add(declaration);
  proxy->BindTo(var);
  return var;
}

Variable* Scope::DeclareVariableName(const AstRawString* name,
                                     VariableMode mode) {
  DCHECK(IsDeclaredVariableMode(mode));
  DCHECK(!already_resolved_);
  DCHECK(GetDeclarationScope()->is_being_lazily_parsed());

  if (mode == VAR && !is_declaration_scope()) {
    return GetDeclarationScope()->DeclareVariableName(name, mode);
  }
  DCHECK(!is_with_scope());
  DCHECK(!is_eval_scope());
  // Unlike DeclareVariable, DeclareVariableName allows declaring variables in
  // catch scopes: Parser::RewriteCatchPattern bypasses DeclareVariable by
  // calling DeclareLocal directly, and it doesn't make sense to add a similar
  // bypass mechanism for PreParser.
  DCHECK(is_declaration_scope() || (IsLexicalVariableMode(mode) &&
                                    (is_block_scope() || is_catch_scope())));
  DCHECK(scope_info_.is_null());

  // Declare the variable in the declaration scope.
  if (FLAG_preparser_scope_analysis) {
    Variable* var = LookupLocal(name);
    DCHECK_NE(var, kDummyPreParserLexicalVariable);
    DCHECK_NE(var, kDummyPreParserVariable);
    if (var == nullptr) {
      var = DeclareLocal(name, mode);
    } else if (mode == VAR) {
      DCHECK_EQ(var->mode(), VAR);
      var->set_maybe_assigned();
    }
    var->set_is_used();
    return var;
  } else {
    return variables_.DeclareName(zone(), name, mode);
  }
}

VariableProxy* Scope::NewUnresolved(AstNodeFactory* factory,
                                    const AstRawString* name,
                                    int start_position, VariableKind kind) {
  // Note that we must not share the unresolved variables with
  // the same name because they may be removed selectively via
  // RemoveUnresolved().
  DCHECK(!already_resolved_);
  DCHECK_EQ(factory->zone(), zone());
  VariableProxy* proxy = factory->NewVariableProxy(name, kind, start_position);
  proxy->set_next_unresolved(unresolved_);
  unresolved_ = proxy;
  return proxy;
}

void Scope::AddUnresolved(VariableProxy* proxy) {
  DCHECK(!already_resolved_);
  DCHECK(!proxy->is_resolved());
  proxy->set_next_unresolved(unresolved_);
  unresolved_ = proxy;
}

Variable* DeclarationScope::DeclareDynamicGlobal(const AstRawString* name,
                                                 VariableKind kind) {
  DCHECK(is_script_scope());
  return variables_.Declare(zone(), this, name, DYNAMIC_GLOBAL, kind);
  // TODO(neis): Mark variable as maybe-assigned?
}


bool Scope::RemoveUnresolved(VariableProxy* var) {
  if (unresolved_ == var) {
    unresolved_ = var->next_unresolved();
    var->set_next_unresolved(nullptr);
    return true;
  }
  VariableProxy* current = unresolved_;
  while (current != nullptr) {
    VariableProxy* next = current->next_unresolved();
    if (var == next) {
      current->set_next_unresolved(next->next_unresolved());
      var->set_next_unresolved(nullptr);
      return true;
    }
    current = next;
  }
  return false;
}

Variable* Scope::NewTemporary(const AstRawString* name) {
  return NewTemporary(name, kMaybeAssigned);
}

Variable* Scope::NewTemporary(const AstRawString* name,
                              MaybeAssignedFlag maybe_assigned) {
  DeclarationScope* scope = GetClosureScope();
  Variable* var = new (zone())
      Variable(scope, name, TEMPORARY, NORMAL_VARIABLE, kCreatedInitialized);
  scope->AddLocal(var);
  if (maybe_assigned == kMaybeAssigned) var->set_maybe_assigned();
  return var;
}

Declaration* Scope::CheckConflictingVarDeclarations() {
  for (Declaration* decl : decls_) {
    VariableMode mode = decl->proxy()->var()->mode();
    if (IsLexicalVariableMode(mode) && !is_block_scope()) continue;

    // Iterate through all scopes until and including the declaration scope.
    Scope* previous = NULL;
    Scope* current = decl->scope();
    // Lexical vs lexical conflicts within the same scope have already been
    // captured in Parser::Declare. The only conflicts we still need to check
    // are lexical vs VAR, or any declarations within a declaration block scope
    // vs lexical declarations in its surrounding (function) scope.
    if (IsLexicalVariableMode(mode)) current = current->outer_scope_;
    do {
      // There is a conflict if there exists a non-VAR binding.
      Variable* other_var =
          current->variables_.Lookup(decl->proxy()->raw_name());
      if (other_var != NULL && IsLexicalVariableMode(other_var->mode())) {
        return decl;
      }
      previous = current;
      current = current->outer_scope_;
    } while (!previous->is_declaration_scope());
  }
  return NULL;
}

Declaration* Scope::CheckLexDeclarationsConflictingWith(
    const ZoneList<const AstRawString*>& names) {
  DCHECK(is_block_scope());
  for (int i = 0; i < names.length(); ++i) {
    Variable* var = LookupLocal(names.at(i));
    if (var != nullptr) {
      // Conflict; find and return its declaration.
      DCHECK(IsLexicalVariableMode(var->mode()));
      const AstRawString* name = names.at(i);
      for (Declaration* decl : decls_) {
        if (decl->proxy()->raw_name() == name) return decl;
      }
      DCHECK(false);
    }
  }
  return nullptr;
}

void DeclarationScope::AllocateVariables(ParseInfo* info, AnalyzeMode mode) {
  // Module variables must be allocated before variable resolution
  // to ensure that AccessNeedsHoleCheck() can detect import variables.
  if (is_module_scope()) AsModuleScope()->AllocateModuleVariables();

  ResolveVariablesRecursively(info);
  AllocateVariablesRecursively();

  MaybeHandle<ScopeInfo> outer_scope;
  if (outer_scope_ != nullptr) outer_scope = outer_scope_->scope_info_;

  AllocateScopeInfosRecursively(info->isolate(), outer_scope);
  if (mode == AnalyzeMode::kDebugger) {
    AllocateDebuggerScopeInfos(info->isolate(), outer_scope);
  }
  // The debugger expects all shared function infos to contain a scope info.
  // Since the top-most scope will end up in a shared function info, make sure
  // it has one, even if it doesn't need a scope info.
  // TODO(jochen|yangguo): Remove this requirement.
  if (scope_info_.is_null()) {
    scope_info_ = ScopeInfo::Create(info->isolate(), zone(), this, outer_scope);
  }
}

bool Scope::AllowsLazyParsingWithoutUnresolvedVariables(
    const Scope* outer) const {
  // If none of the outer scopes need to decide whether to context allocate
  // specific variables, we can preparse inner functions without unresolved
  // variables. Otherwise we need to find unresolved variables to force context
  // allocation of the matching declarations. We can stop at the outer scope for
  // the parse, since context allocation of those variables is already
  // guaranteed to be correct.
  for (const Scope* s = this; s != outer; s = s->outer_scope_) {
    // Eval forces context allocation on all outer scopes, so we don't need to
    // look at those scopes. Sloppy eval makes top-level non-lexical variables
    // dynamic, whereas strict-mode requires context allocation.
    if (s->is_eval_scope()) return is_sloppy(s->language_mode());
    // Catch scopes force context allocation of all variables.
    if (s->is_catch_scope()) continue;
    // With scopes do not introduce variables that need allocation.
    if (s->is_with_scope()) continue;
    // If everything is guaranteed to be context allocated we can ignore the
    // scope.
    if (s->has_forced_context_allocation()) continue;
    // Only block scopes and function scopes should disallow preparsing.
    DCHECK(s->is_block_scope() || s->is_function_scope());
    return false;
  }
  return true;
}

bool DeclarationScope::AllowsLazyCompilation() const {
  return !force_eager_compilation_;
}

int Scope::ContextChainLength(Scope* scope) const {
  int n = 0;
  for (const Scope* s = this; s != scope; s = s->outer_scope_) {
    DCHECK(s != NULL);  // scope must be in the scope chain
    if (s->NeedsContext()) n++;
  }
  return n;
}

int Scope::ContextChainLengthUntilOutermostSloppyEval() const {
  int result = 0;
  int length = 0;

  for (const Scope* s = this; s != nullptr; s = s->outer_scope()) {
    if (!s->NeedsContext()) continue;
    length++;
    if (s->calls_sloppy_eval()) result = length;
  }

  return result;
}

int Scope::MaxNestedContextChainLength() {
  int max_context_chain_length = 0;
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    if (scope->is_function_scope()) continue;
    max_context_chain_length = std::max(scope->MaxNestedContextChainLength(),
                                        max_context_chain_length);
  }
  if (NeedsContext()) {
    max_context_chain_length += 1;
  }
  return max_context_chain_length;
}

DeclarationScope* Scope::GetDeclarationScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

const DeclarationScope* Scope::GetClosureScope() const {
  const Scope* scope = this;
  while (!scope->is_declaration_scope() || scope->is_block_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

DeclarationScope* Scope::GetClosureScope() {
  Scope* scope = this;
  while (!scope->is_declaration_scope() || scope->is_block_scope()) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

bool Scope::NeedsScopeInfo() const {
  DCHECK(!already_resolved_);
  DCHECK(GetClosureScope()->ShouldEagerCompile());
  // The debugger expects all functions to have scope infos.
  // TODO(jochen|yangguo): Remove this requirement.
  if (is_function_scope()) return true;
  return NeedsContext();
}

ModuleScope* Scope::GetModuleScope() {
  Scope* scope = this;
  DCHECK(!scope->is_script_scope());
  while (!scope->is_module_scope()) {
    scope = scope->outer_scope();
    DCHECK_NOT_NULL(scope);
  }
  return scope->AsModuleScope();
}

DeclarationScope* Scope::GetReceiverScope() {
  Scope* scope = this;
  while (!scope->is_script_scope() &&
         (!scope->is_function_scope() ||
          scope->AsDeclarationScope()->is_arrow_scope())) {
    scope = scope->outer_scope();
  }
  return scope->AsDeclarationScope();
}

Scope* Scope::GetOuterScopeWithContext() {
  Scope* scope = outer_scope_;
  while (scope && !scope->NeedsContext()) {
    scope = scope->outer_scope();
  }
  return scope;
}

Handle<StringSet> DeclarationScope::CollectNonLocals(
    ParseInfo* info, Handle<StringSet> non_locals) {
  VariableProxy* free_variables = FetchFreeVariables(this, info);
  for (VariableProxy* proxy = free_variables; proxy != nullptr;
       proxy = proxy->next_unresolved()) {
    non_locals = StringSet::Add(non_locals, proxy->name());
  }
  return non_locals;
}

void DeclarationScope::ResetAfterPreparsing(AstValueFactory* ast_value_factory,
                                            bool aborted) {
  DCHECK(is_function_scope());

  // Reset all non-trivial members.
  params_.Clear();
  decls_.Clear();
  locals_.Clear();
  inner_scope_ = nullptr;
  unresolved_ = nullptr;

  if (aborted) {
    // Prepare scope for use in the outer zone.
    zone_ = ast_value_factory->zone();
    variables_.Reset(ZoneAllocationPolicy(zone_));
    sloppy_block_function_map_.Reset(ZoneAllocationPolicy(zone_));
    if (!IsArrowFunction(function_kind_)) {
      DeclareDefaultFunctionVariables(ast_value_factory);
    }
  } else {
    // Make sure this scope isn't used for allocation anymore.
    zone_ = nullptr;
    variables_.Invalidate();
    sloppy_block_function_map_.Invalidate();
  }

#ifdef DEBUG
  needs_migration_ = false;
  is_being_lazily_parsed_ = false;
#endif

  was_lazily_parsed_ = !aborted;
}

void DeclarationScope::AnalyzePartially(
    AstNodeFactory* ast_node_factory,
    PreParsedScopeData* preparsed_scope_data) {
  DCHECK(!force_eager_compilation_);
  VariableProxy* unresolved = nullptr;

  if (!outer_scope_->is_script_scope()) {
    // Try to resolve unresolved variables for this Scope and migrate those
    // which cannot be resolved inside. It doesn't make sense to try to resolve
    // them in the outer Scopes here, because they are incomplete.
    for (VariableProxy* proxy = FetchFreeVariables(this); proxy != nullptr;
         proxy = proxy->next_unresolved()) {
      DCHECK(!proxy->is_resolved());
      VariableProxy* copy = ast_node_factory->CopyVariableProxy(proxy);
      copy->set_next_unresolved(unresolved);
      unresolved = copy;
    }

    // Clear arguments_ if unused. This is used as a signal for optimization.
    if (arguments_ != nullptr &&
        !(MustAllocate(arguments_) && !has_arguments_parameter_)) {
      arguments_ = nullptr;
    }

    if (FLAG_preparser_scope_analysis) {
      // Decide context allocation for the locals and parameters and store the
      // info away.
      AllocateVariablesRecursively();
      CollectVariableData(preparsed_scope_data);
    }
  }
#ifdef DEBUG
  if (FLAG_print_scopes) {
    PrintF("Inner function scope:\n");
    Print();
  }
#endif

  ResetAfterPreparsing(ast_node_factory->ast_value_factory(), false);

  unresolved_ = unresolved;
}

#ifdef DEBUG
namespace {

const char* Header(ScopeType scope_type, FunctionKind function_kind,
                   bool is_declaration_scope) {
  switch (scope_type) {
    case EVAL_SCOPE: return "eval";
    // TODO(adamk): Should we print concise method scopes specially?
    case FUNCTION_SCOPE:
      if (IsGeneratorFunction(function_kind)) return "function*";
      if (IsAsyncFunction(function_kind)) return "async function";
      if (IsArrowFunction(function_kind)) return "arrow";
      return "function";
    case MODULE_SCOPE: return "module";
    case SCRIPT_SCOPE: return "global";
    case CATCH_SCOPE: return "catch";
    case BLOCK_SCOPE: return is_declaration_scope ? "varblock" : "block";
    case WITH_SCOPE: return "with";
  }
  UNREACHABLE();
  return NULL;
}

void Indent(int n, const char* str) { PrintF("%*s%s", n, "", str); }

void PrintName(const AstRawString* name) {
  PrintF("%.*s", name->length(), name->raw_data());
}

void PrintLocation(Variable* var) {
  switch (var->location()) {
    case VariableLocation::UNALLOCATED:
      break;
    case VariableLocation::PARAMETER:
      PrintF("parameter[%d]", var->index());
      break;
    case VariableLocation::LOCAL:
      PrintF("local[%d]", var->index());
      break;
    case VariableLocation::CONTEXT:
      PrintF("context[%d]", var->index());
      break;
    case VariableLocation::LOOKUP:
      PrintF("lookup");
      break;
    case VariableLocation::MODULE:
      PrintF("module");
      break;
  }
}

void PrintVar(int indent, Variable* var) {
  Indent(indent, VariableMode2String(var->mode()));
  PrintF(" ");
  if (var->raw_name()->IsEmpty())
    PrintF(".%p", reinterpret_cast<void*>(var));
  else
    PrintName(var->raw_name());
  PrintF(";  // ");
  PrintLocation(var);
  bool comma = !var->IsUnallocated();
  if (var->has_forced_context_allocation()) {
    if (comma) PrintF(", ");
    PrintF("forced context allocation");
    comma = true;
  }
  if (var->maybe_assigned() == kNotAssigned) {
    if (comma) PrintF(", ");
    PrintF("never assigned");
  }
  PrintF("\n");
}

void PrintMap(int indent, const char* label, VariableMap* map, bool locals,
              Variable* function_var) {
  bool printed_label = false;
  for (VariableMap::Entry* p = map->Start(); p != nullptr; p = map->Next(p)) {
    Variable* var = reinterpret_cast<Variable*>(p->value);
    if (var == function_var) continue;
    if (var == kDummyPreParserVariable ||
        var == kDummyPreParserLexicalVariable) {
      continue;
    }
    bool local = !IsDynamicVariableMode(var->mode());
    if ((locals ? local : !local) &&
        (var->is_used() || !var->IsUnallocated())) {
      if (!printed_label) {
        Indent(indent, label);
        printed_label = true;
      }
      PrintVar(indent, var);
    }
  }
}

}  // anonymous namespace

void DeclarationScope::PrintParameters() {
  PrintF(" (");
  for (int i = 0; i < params_.length(); i++) {
    if (i > 0) PrintF(", ");
    const AstRawString* name = params_[i]->raw_name();
    if (name->IsEmpty())
      PrintF(".%p", reinterpret_cast<void*>(params_[i]));
    else
      PrintName(name);
  }
  PrintF(")");
}

void Scope::Print(int n) {
  int n0 = (n > 0 ? n : 0);
  int n1 = n0 + 2;  // indentation

  // Print header.
  FunctionKind function_kind = is_function_scope()
                                   ? AsDeclarationScope()->function_kind()
                                   : kNormalFunction;
  Indent(n0, Header(scope_type_, function_kind, is_declaration_scope()));
  if (scope_name_ != nullptr && !scope_name_->IsEmpty()) {
    PrintF(" ");
    PrintName(scope_name_);
  }

  // Print parameters, if any.
  Variable* function = nullptr;
  if (is_function_scope()) {
    AsDeclarationScope()->PrintParameters();
    function = AsDeclarationScope()->function_var();
  }

  PrintF(" { // (%d, %d)\n", start_position(), end_position());
  if (is_hidden()) {
    Indent(n1, "// is hidden\n");
  }

  // Function name, if any (named function literals, only).
  if (function != nullptr) {
    Indent(n1, "// (local) function name: ");
    PrintName(function->raw_name());
    PrintF("\n");
  }

  // Scope info.
  if (is_strict(language_mode())) {
    Indent(n1, "// strict mode scope\n");
  }
  if (IsAsmModule()) Indent(n1, "// scope is an asm module\n");
  if (IsAsmFunction()) Indent(n1, "// scope is an asm function\n");
  if (scope_calls_eval_) Indent(n1, "// scope calls 'eval'\n");
  if (is_declaration_scope() && AsDeclarationScope()->uses_super_property()) {
    Indent(n1, "// scope uses 'super' property\n");
  }
  if (inner_scope_calls_eval_) Indent(n1, "// inner scope calls 'eval'\n");
  if (is_declaration_scope()) {
    DeclarationScope* scope = AsDeclarationScope();
    if (scope->was_lazily_parsed()) Indent(n1, "// lazily parsed\n");
    if (scope->ShouldEagerCompile()) Indent(n1, "// will be compiled\n");
  }
  if (has_forced_context_allocation()) {
    Indent(n1, "// forces context allocation\n");
  }
  if (num_stack_slots_ > 0) {
    Indent(n1, "// ");
    PrintF("%d stack slots\n", num_stack_slots_);
  }
  if (num_heap_slots_ > 0) {
    Indent(n1, "// ");
    PrintF("%d heap slots\n", num_heap_slots_);
  }

  // Print locals.
  if (function != nullptr) {
    Indent(n1, "// function var:\n");
    PrintVar(n1, function);
  }

  // Print temporaries.
  {
    bool printed_header = false;
    for (Variable* local : locals_) {
      if (local->mode() != TEMPORARY) continue;
      if (!printed_header) {
        printed_header = true;
        Indent(n1, "// temporary vars:\n");
      }
      PrintVar(n1, local);
    }
  }

  if (variables_.occupancy() > 0) {
    PrintMap(n1, "// local vars:\n", &variables_, true, function);
    PrintMap(n1, "// dynamic vars:\n", &variables_, false, function);
  }

  // Print inner scopes (disable by providing negative n).
  if (n >= 0) {
    for (Scope* scope = inner_scope_; scope != nullptr;
         scope = scope->sibling_) {
      PrintF("\n");
      scope->Print(n1);
    }
  }

  Indent(n0, "}\n");
}

void Scope::CheckScopePositions() {
  // Visible leaf scopes must have real positions.
  if (!is_hidden() && inner_scope_ == nullptr) {
    CHECK_NE(kNoSourcePosition, start_position());
    CHECK_NE(kNoSourcePosition, end_position());
  }
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    scope->CheckScopePositions();
  }
}

void Scope::CheckZones() {
  DCHECK(!needs_migration_);
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    if (scope->is_declaration_scope() &&
        scope->AsDeclarationScope()->was_lazily_parsed()) {
      DCHECK_NULL(scope->zone());
      DCHECK_NULL(scope->inner_scope_);
      continue;
    }
    CHECK_EQ(scope->zone(), zone());
    scope->CheckZones();
  }
}
#endif  // DEBUG

Variable* Scope::NonLocal(const AstRawString* name, VariableMode mode) {
  // Declare a new non-local.
  DCHECK(IsDynamicVariableMode(mode));
  Variable* var = variables_.Declare(zone(), nullptr, name, mode);
  // Allocate it by giving it a dynamic lookup.
  var->AllocateTo(VariableLocation::LOOKUP, -1);
  return var;
}

Variable* Scope::LookupRecursive(VariableProxy* proxy, Scope* outer_scope_end) {
  DCHECK_NE(outer_scope_end, this);
  // Short-cut: whenever we find a debug-evaluate scope, just look everything up
  // dynamically. Debug-evaluate doesn't properly create scope info for the
  // lookups it does. It may not have a valid 'this' declaration, and anything
  // accessed through debug-evaluate might invalidly resolve to stack-allocated
  // variables.
  // TODO(yangguo): Remove once debug-evaluate creates proper ScopeInfo for the
  // scopes in which it's evaluating.
  if (is_debug_evaluate_scope_) return NonLocal(proxy->raw_name(), DYNAMIC);

  // Try to find the variable in this scope.
  Variable* var = LookupLocal(proxy->raw_name());

  // We found a variable and we are done. (Even if there is an 'eval' in this
  // scope which introduces the same variable again, the resulting variable
  // remains the same.)
  if (var != nullptr) return var;

  if (outer_scope_ == outer_scope_end) {
    // We may just be trying to find all free variables. In that case, don't
    // declare them in the outer scope.
    if (!is_script_scope()) return nullptr;
    // No binding has been found. Declare a variable on the global object.
    return AsDeclarationScope()->DeclareDynamicGlobal(proxy->raw_name(),
                                                      NORMAL_VARIABLE);
  }

  DCHECK(!is_script_scope());

  var = outer_scope_->LookupRecursive(proxy, outer_scope_end);

  // The variable could not be resolved statically.
  if (var == nullptr) return var;

  // TODO(marja): Separate LookupRecursive for preparsed scopes better.
  if (var == kDummyPreParserVariable || var == kDummyPreParserLexicalVariable) {
    DCHECK(GetDeclarationScope()->is_being_lazily_parsed());
    DCHECK(FLAG_lazy_inner_functions);
    return var;
  }

  if (is_function_scope() && !var->is_dynamic()) {
    var->ForceContextAllocation();
  }
  // "this" can't be shadowed by "eval"-introduced bindings or by "with"
  // scopes.
  // TODO(wingo): There are other variables in this category; add them.
  if (var->is_this()) return var;

  if (is_with_scope()) {
    // The current scope is a with scope, so the variable binding can not be
    // statically resolved. However, note that it was necessary to do a lookup
    // in the outer scope anyway, because if a binding exists in an outer
    // scope, the associated variable has to be marked as potentially being
    // accessed from inside of an inner with scope (the property may not be in
    // the 'with' object).
    if (!var->is_dynamic() && var->IsUnallocated()) {
      DCHECK(!already_resolved_);
      var->set_is_used();
      var->ForceContextAllocation();
      if (proxy->is_assigned()) var->set_maybe_assigned();
    }
    return NonLocal(proxy->raw_name(), DYNAMIC);
  }

  if (calls_sloppy_eval() && is_declaration_scope()) {
    // A variable binding may have been found in an outer scope, but the current
    // scope makes a sloppy 'eval' call, so the found variable may not be the
    // correct one (the 'eval' may introduce a binding with the same name). In
    // that case, change the lookup result to reflect this situation. Only
    // scopes that can host var bindings (declaration scopes) need be considered
    // here (this excludes block and catch scopes), and variable lookups at
    // script scope are always dynamic.
    if (var->IsGlobalObjectProperty()) {
      return NonLocal(proxy->raw_name(), DYNAMIC_GLOBAL);
    }

    if (var->is_dynamic()) return var;

    Variable* invalidated = var;
    var = NonLocal(proxy->raw_name(), DYNAMIC_LOCAL);
    var->set_local_if_not_shadowed(invalidated);
  }

  return var;
}

void Scope::ResolveVariable(ParseInfo* info, VariableProxy* proxy) {
  DCHECK(info->script_scope()->is_script_scope());
  DCHECK(!proxy->is_resolved());
  Variable* var = LookupRecursive(proxy, nullptr);
  ResolveTo(info, proxy, var);
}

namespace {

bool AccessNeedsHoleCheck(Variable* var, VariableProxy* proxy, Scope* scope) {
  if (var->mode() == DYNAMIC_LOCAL) {
    // Dynamically introduced variables never need a hole check (since they're
    // VAR bindings, either from var or function declarations), but the variable
    // they shadow might need a hole check, which we want to do if we decide
    // that no shadowing variable was dynamically introoduced.
    DCHECK(!var->binding_needs_init());
    return AccessNeedsHoleCheck(var->local_if_not_shadowed(), proxy, scope);
  }

  if (!var->binding_needs_init()) {
    return false;
  }

  // It's impossible to eliminate module import hole checks here, because it's
  // unknown at compilation time whether the binding referred to in the
  // exporting module itself requires hole checks.
  if (var->location() == VariableLocation::MODULE && !var->IsExport()) {
    return true;
  }

  // Check if the binding really needs an initialization check. The check
  // can be skipped in the following situation: we have a LET or CONST
  // binding, both the Variable and the VariableProxy have the same
  // declaration scope (i.e. they are both in global code, in the
  // same function or in the same eval code), the VariableProxy is in
  // the source physically located after the initializer of the variable,
  // and that the initializer cannot be skipped due to a nonlinear scope.
  //
  // The condition on the declaration scopes is a conservative check for
  // nested functions that access a binding and are called before the
  // binding is initialized:
  //   function() { f(); let x = 1; function f() { x = 2; } }
  //
  // The check cannot be skipped on non-linear scopes, namely switch
  // scopes, to ensure tests are done in cases like the following:
  //   switch (1) { case 0: let x = 2; case 1: f(x); }
  // The scope of the variable needs to be checked, in case the use is
  // in a sub-block which may be linear.
  if (var->scope()->GetDeclarationScope() != scope->GetDeclarationScope()) {
    return true;
  }

  if (var->is_this()) {
    DCHECK(IsDerivedConstructor(scope->GetDeclarationScope()->function_kind()));
    // TODO(littledan): implement 'this' hole check elimination.
    return true;
  }

  // We should always have valid source positions.
  DCHECK(var->initializer_position() != kNoSourcePosition);
  DCHECK(proxy->position() != kNoSourcePosition);

  return var->scope()->is_nonlinear() ||
         var->initializer_position() >= proxy->position();
}

}  // anonymous namespace

void Scope::ResolveTo(ParseInfo* info, VariableProxy* proxy, Variable* var) {
#ifdef DEBUG
  if (info->script_is_native()) {
    // To avoid polluting the global object in native scripts
    //  - Variables must not be allocated to the global scope.
    CHECK_NOT_NULL(outer_scope());
    //  - Variables must be bound locally or unallocated.
    if (var->IsGlobalObjectProperty()) {
      // The following variable name may be minified. If so, disable
      // minification in js2c.py for better output.
      Handle<String> name = proxy->raw_name()->string();
      V8_Fatal(__FILE__, __LINE__, "Unbound variable: '%s' in native script.",
               name->ToCString().get());
    }
    VariableLocation location = var->location();
    CHECK(location == VariableLocation::LOCAL ||
          location == VariableLocation::CONTEXT ||
          location == VariableLocation::PARAMETER ||
          location == VariableLocation::UNALLOCATED);
  }
#endif

  DCHECK_NOT_NULL(var);
  if (AccessNeedsHoleCheck(var, proxy, this)) proxy->set_needs_hole_check();
  proxy->BindTo(var);
}

void Scope::ResolveVariablesRecursively(ParseInfo* info) {
  DCHECK(info->script_scope()->is_script_scope());
  // Lazy parsed declaration scopes are already partially analyzed. If there are
  // unresolved references remaining, they just need to be resolved in outer
  // scopes.
  if (is_declaration_scope() && AsDeclarationScope()->was_lazily_parsed()) {
    DCHECK(variables_.occupancy() == 0);
    for (VariableProxy* proxy = unresolved_; proxy != nullptr;
         proxy = proxy->next_unresolved()) {
      Variable* var = outer_scope()->LookupRecursive(proxy, nullptr);
      if (!var->is_dynamic()) {
        var->set_is_used();
        var->ForceContextAllocation();
        if (proxy->is_assigned()) var->set_maybe_assigned();
      }
    }
  } else {
    // Resolve unresolved variables for this scope.
    for (VariableProxy* proxy = unresolved_; proxy != nullptr;
         proxy = proxy->next_unresolved()) {
      ResolveVariable(info, proxy);
    }

    // Resolve unresolved variables for inner scopes.
    for (Scope* scope = inner_scope_; scope != nullptr;
         scope = scope->sibling_) {
      scope->ResolveVariablesRecursively(info);
    }
  }
}

VariableProxy* Scope::FetchFreeVariables(DeclarationScope* max_outer_scope,
                                         ParseInfo* info,
                                         VariableProxy* stack) {
  // Module variables must be allocated before variable resolution
  // to ensure that AccessNeedsHoleCheck() can detect import variables.
  if (info != nullptr && is_module_scope()) {
    AsModuleScope()->AllocateModuleVariables();
  }
  // Lazy parsed declaration scopes are already partially analyzed. If there are
  // unresolved references remaining, they just need to be resolved in outer
  // scopes.
  Scope* lookup =
      is_declaration_scope() && AsDeclarationScope()->was_lazily_parsed()
          ? outer_scope()
          : this;
  for (VariableProxy *proxy = unresolved_, *next = nullptr; proxy != nullptr;
       proxy = next) {
    next = proxy->next_unresolved();
    DCHECK(!proxy->is_resolved());
    Variable* var =
        lookup->LookupRecursive(proxy, max_outer_scope->outer_scope());
    if (var == nullptr) {
      proxy->set_next_unresolved(stack);
      stack = proxy;
    } else if (var != kDummyPreParserVariable &&
               var != kDummyPreParserLexicalVariable) {
      if (info != nullptr) {
        // In this case we need to leave scopes in a way that they can be
        // allocated. If we resolved variables from lazy parsed scopes, we need
        // to context allocate the var.
        ResolveTo(info, proxy, var);
        if (!var->is_dynamic() && lookup != this) var->ForceContextAllocation();
      } else {
        var->set_is_used();
        if (proxy->is_assigned()) {
          var->set_maybe_assigned();
        }
      }
    }
  }

  // Clear unresolved_ as it's in an inconsistent state.
  unresolved_ = nullptr;

  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    stack = scope->FetchFreeVariables(max_outer_scope, info, stack);
  }

  return stack;
}

bool Scope::MustAllocate(Variable* var) {
  if (var == kDummyPreParserLexicalVariable || var == kDummyPreParserVariable) {
    return true;
  }
  DCHECK(var->location() != VariableLocation::MODULE);
  // Give var a read/write use if there is a chance it might be accessed
  // via an eval() call.  This is only possible if the variable has a
  // visible name.
  if ((var->is_this() || !var->raw_name()->IsEmpty()) &&
      (inner_scope_calls_eval_ || is_catch_scope() || is_script_scope())) {
    var->set_is_used();
    if (inner_scope_calls_eval_) var->set_maybe_assigned();
  }
  DCHECK(!var->has_forced_context_allocation() || var->is_used());
  // Global variables do not need to be allocated.
  return !var->IsGlobalObjectProperty() && var->is_used();
}


bool Scope::MustAllocateInContext(Variable* var) {
  // If var is accessed from an inner scope, or if there is a possibility
  // that it might be accessed from the current or an inner scope (through
  // an eval() call or a runtime with lookup), it must be allocated in the
  // context.
  //
  // Exceptions: If the scope as a whole has forced context allocation, all
  // variables will have context allocation, even temporaries.  Otherwise
  // temporary variables are always stack-allocated.  Catch-bound variables are
  // always context-allocated.
  if (has_forced_context_allocation()) return true;
  if (var->mode() == TEMPORARY) return false;
  if (is_catch_scope()) return true;
  if ((is_script_scope() || is_eval_scope()) &&
      IsLexicalVariableMode(var->mode())) {
    return true;
  }
  return var->has_forced_context_allocation() || inner_scope_calls_eval_;
}


void Scope::AllocateStackSlot(Variable* var) {
  if (is_block_scope()) {
    outer_scope()->GetDeclarationScope()->AllocateStackSlot(var);
  } else {
    var->AllocateTo(VariableLocation::LOCAL, num_stack_slots_++);
  }
}


void Scope::AllocateHeapSlot(Variable* var) {
  var->AllocateTo(VariableLocation::CONTEXT, num_heap_slots_++);
}

void DeclarationScope::AllocateParameterLocals() {
  DCHECK(is_function_scope());

  bool uses_sloppy_arguments = false;

  if (arguments_ != nullptr) {
    DCHECK(!is_arrow_scope());
    // 'arguments' is used. Unless there is also a parameter called
    // 'arguments', we must be conservative and allocate all parameters to
    // the context assuming they will be captured by the arguments object.
    // If we have a parameter named 'arguments', a (new) value is always
    // assigned to it via the function invocation. Then 'arguments' denotes
    // that specific parameter value and cannot be used to access the
    // parameters, which is why we don't need to allocate an arguments
    // object in that case.
    if (MustAllocate(arguments_) && !has_arguments_parameter_) {
      // In strict mode 'arguments' does not alias formal parameters.
      // Therefore in strict mode we allocate parameters as if 'arguments'
      // were not used.
      // If the parameter list is not simple, arguments isn't sloppy either.
      uses_sloppy_arguments =
          is_sloppy(language_mode()) && has_simple_parameters();
    } else {
      // 'arguments' is unused. Tell the code generator that it does not need to
      // allocate the arguments object by nulling out arguments_.
      arguments_ = nullptr;
    }
  }

  // The same parameter may occur multiple times in the parameters_ list.
  // If it does, and if it is not copied into the context object, it must
  // receive the highest parameter index for that parameter; thus iteration
  // order is relevant!
  for (int i = num_parameters() - 1; i >= 0; --i) {
    Variable* var = params_[i];
    DCHECK(!has_rest_ || var != rest_parameter());
    DCHECK_EQ(this, var->scope());
    if (uses_sloppy_arguments) {
      var->set_is_used();
      var->set_maybe_assigned();
      var->ForceContextAllocation();
    }
    AllocateParameter(var, i);
  }
}

void DeclarationScope::AllocateParameter(Variable* var, int index) {
  if (MustAllocate(var)) {
    if (MustAllocateInContext(var)) {
      DCHECK(var->IsUnallocated() || var->IsContextSlot());
      if (var->IsUnallocated()) {
        AllocateHeapSlot(var);
      }
    } else {
      DCHECK(var->IsUnallocated() || var->IsParameter());
      if (var->IsUnallocated()) {
        var->AllocateTo(VariableLocation::PARAMETER, index);
      }
    }
  }
}

void DeclarationScope::AllocateReceiver() {
  if (!has_this_declaration()) return;
  DCHECK_NOT_NULL(receiver());
  DCHECK_EQ(receiver()->scope(), this);
  AllocateParameter(receiver(), -1);
}

void Scope::AllocateNonParameterLocal(Variable* var) {
  DCHECK(var->scope() == this);
  if (var->IsUnallocated() && MustAllocate(var)) {
    if (MustAllocateInContext(var)) {
      AllocateHeapSlot(var);
    } else {
      AllocateStackSlot(var);
    }
  }
}

void Scope::AllocateNonParameterLocalsAndDeclaredGlobals() {
  for (Variable* local : locals_) {
    AllocateNonParameterLocal(local);
  }

  if (is_declaration_scope()) {
    AsDeclarationScope()->AllocateLocals();
  }
}

void DeclarationScope::AllocateLocals() {
  // For now, function_ must be allocated at the very end.  If it gets
  // allocated in the context, it must be the last slot in the context,
  // because of the current ScopeInfo implementation (see
  // ScopeInfo::ScopeInfo(FunctionScope* scope) constructor).
  if (function_ != nullptr) {
    AllocateNonParameterLocal(function_);
  }

  DCHECK(!has_rest_ || !MustAllocate(rest_parameter()) ||
         !rest_parameter()->IsUnallocated());

  if (new_target_ != nullptr && !MustAllocate(new_target_)) {
    new_target_ = nullptr;
  }

  NullifyRareVariableIf(RareVariable::kThisFunction,
                        [=](Variable* var) { return !MustAllocate(var); });
}

void ModuleScope::AllocateModuleVariables() {
  for (const auto& it : module()->regular_imports()) {
    Variable* var = LookupLocal(it.first);
    var->AllocateTo(VariableLocation::MODULE, it.second->cell_index);
    DCHECK(!var->IsExport());
  }

  for (const auto& it : module()->regular_exports()) {
    Variable* var = LookupLocal(it.first);
    var->AllocateTo(VariableLocation::MODULE, it.second->cell_index);
    DCHECK(var->IsExport());
  }
}

void Scope::AllocateVariablesRecursively() {
  DCHECK(!already_resolved_);
  DCHECK_IMPLIES(!FLAG_preparser_scope_analysis, num_stack_slots_ == 0);

  // Don't allocate variables of preparsed scopes.
  if (is_declaration_scope() && AsDeclarationScope()->was_lazily_parsed()) {
    return;
  }

  // Allocate variables for inner scopes.
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    scope->AllocateVariablesRecursively();
  }

  DCHECK(!already_resolved_);
  DCHECK_EQ(Context::MIN_CONTEXT_SLOTS, num_heap_slots_);

  // Allocate variables for this scope.
  // Parameters must be allocated first, if any.
  if (is_declaration_scope()) {
    if (is_function_scope()) {
      AsDeclarationScope()->AllocateParameterLocals();
    }
    AsDeclarationScope()->AllocateReceiver();
  }
  AllocateNonParameterLocalsAndDeclaredGlobals();

  // Force allocation of a context for this scope if necessary. For a 'with'
  // scope and for a function scope that makes an 'eval' call we need a context,
  // even if no local variables were statically allocated in the scope.
  // Likewise for modules and function scopes representing asm.js modules.
  bool must_have_context =
      is_with_scope() || is_module_scope() || IsAsmModule() ||
      (is_function_scope() && calls_sloppy_eval()) ||
      (is_block_scope() && is_declaration_scope() && calls_sloppy_eval());

  // If we didn't allocate any locals in the local context, then we only
  // need the minimal number of slots if we must have a context.
  if (num_heap_slots_ == Context::MIN_CONTEXT_SLOTS && !must_have_context) {
    num_heap_slots_ = 0;
  }

  // Allocation done.
  DCHECK(num_heap_slots_ == 0 || num_heap_slots_ >= Context::MIN_CONTEXT_SLOTS);
}

void Scope::AllocateScopeInfosRecursively(Isolate* isolate,
                                          MaybeHandle<ScopeInfo> outer_scope) {
  DCHECK(scope_info_.is_null());
  MaybeHandle<ScopeInfo> next_outer_scope = outer_scope;

  if (NeedsScopeInfo()) {
    scope_info_ = ScopeInfo::Create(isolate, zone(), this, outer_scope);
    // The ScopeInfo chain should mirror the context chain, so we only link to
    // the next outer scope that needs a context.
    if (NeedsContext()) next_outer_scope = scope_info_;
  }

  // Allocate ScopeInfos for inner scopes.
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    if (!scope->is_function_scope() ||
        scope->AsDeclarationScope()->ShouldEagerCompile()) {
      scope->AllocateScopeInfosRecursively(isolate, next_outer_scope);
    }
  }
}

void Scope::AllocateDebuggerScopeInfos(Isolate* isolate,
                                       MaybeHandle<ScopeInfo> outer_scope) {
  if (scope_info_.is_null()) {
    scope_info_ = ScopeInfo::Create(isolate, zone(), this, outer_scope);
  }
  MaybeHandle<ScopeInfo> outer = NeedsContext() ? scope_info_ : outer_scope;
  for (Scope* scope = inner_scope_; scope != nullptr; scope = scope->sibling_) {
    if (scope->is_function_scope()) continue;
    scope->AllocateDebuggerScopeInfos(isolate, outer);
  }
}

void Scope::CollectVariableData(PreParsedScopeData* data) {
  PreParsedScopeData::ScopeScope scope_scope(data, scope_type(),
                                             start_position(), end_position());
  for (Variable* local : locals_) {
    scope_scope.MaybeAddVariable(local);
  }
  for (Scope* inner = inner_scope_; inner != nullptr; inner = inner->sibling_) {
    inner->CollectVariableData(data);
  }
}

int Scope::StackLocalCount() const {
  Variable* function =
      is_function_scope() ? AsDeclarationScope()->function_var() : nullptr;
  return num_stack_slots() -
         (function != nullptr && function->IsStackLocal() ? 1 : 0);
}


int Scope::ContextLocalCount() const {
  if (num_heap_slots() == 0) return 0;
  Variable* function =
      is_function_scope() ? AsDeclarationScope()->function_var() : nullptr;
  bool is_function_var_in_context =
      function != nullptr && function->IsContextSlot();
  return num_heap_slots() - Context::MIN_CONTEXT_SLOTS -
         (is_function_var_in_context ? 1 : 0);
}

}  // namespace internal
}  // namespace v8
