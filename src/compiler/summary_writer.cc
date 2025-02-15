// Copyright (C) 2019 Toitware ApS.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; version
// 2.1 only.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// The license can be found in the file `LICENSE` in the top level
// directory of this repository.

#include <functional>

#include "summary_writer.h"
#include "toitdoc_node.h"

namespace toit {
namespace compiler {

namespace {  // Anonymous namespace.

/// The path to an element.
struct ToitdocPath {
  enum class Kind {
    CLASS = 1,
    GLOBAL = 2,
    GLOBAL_METHOD = 3,
    STATIC_METHOD = 4,
    CONSTRUCTOR = 5,
    FACTORY = 6,
    METHOD = 7,
    FIELD = 8,
  };

  Kind kind;
  Module* module;
  // Holder, if the element is inside a class.
  ir::Class* klass;
};

template<typename T> static void print_length(std::vector<T> v) { printf("%zd\n", v.size()); }
template<typename T> static void print_length(Set<T> v) { printf("%d\n", v.size()); }
template<typename T> static void print_length(List<T> v) { printf("%d\n", v.length()); }

class ToitdocWriter : public toitdoc::Visitor {
 public:
  ToitdocWriter(Toitdoc<ir::Node*> toitdoc,
                const UnorderedMap<ir::Node*, ToitdocPath>& paths)
      : _toitdoc(toitdoc)
      , _paths(paths) { }

  void write() {
    visit(_toitdoc.contents());
  }

  void visit_Contents(toitdoc::Contents* node) {
    print_list(node->sections(), &ToitdocWriter::visit_Section);
  }

  void visit_Section(toitdoc::Section* node) {
    print_symbol(node->title());
    print_list(node->statements(), &ToitdocWriter::visit_Statement);
  }

  void visit_CodeSection(toitdoc::CodeSection* node) {
    printf("CODE SECTION\n");
    print_symbol(node->code());
  }

  void visit_Itemized(toitdoc::Itemized* node) {
    printf("ITEMIZED\n");
    print_list(node->items(), &ToitdocWriter::visit_Item);
  }

  void visit_Item(toitdoc::Item* node) {
    printf("ITEM\n");  // Not really necessary, as implied by the parent.
    print_list(node->statements(), &ToitdocWriter::visit_Statement);
  }

  void visit_Paragraph(toitdoc::Paragraph* node) {
    printf("PARAGRAPH\n");
    print_list(node->expressions(), &ToitdocWriter::visit_Expression);
  }

  void visit_Text(toitdoc::Text* node) {
    printf("TEXT\n");
    print_symbol(node->text());
  }

  void visit_Code(toitdoc::Code* node) {
    printf("CODE\n");
    print_symbol(node->text());
  }

  void visit_Ref(toitdoc::Ref* node) {
    printf("REF\n");
    print_symbol(node->text());
    auto resolved = _toitdoc.refs()[node->id()];
    if (resolved == null) {
      printf("-1\n");
    } else if (resolved->is_Parameter()) {
      // TODO(florian): handle parameters.
      printf("-2\n");
    } else {
      auto path = _paths.at(resolved);
      int kind_id = static_cast<int>(path.kind);
      auto holder_name = Symbol::invalid();
      auto name = Symbol::invalid();
      auto shape = ResolutionShape::invalid();
      switch (path.kind) {
        case ToitdocPath::Kind::CLASS:
          name = resolved->as_Class()->name();
          break;

        case ToitdocPath::Kind::GLOBAL:
          name = resolved->as_Global()->name();
          break;

        case ToitdocPath::Kind::GLOBAL_METHOD:
          name = resolved->as_Method()->name();
          shape = resolved->as_Method()->resolution_shape();
          break;

        case ToitdocPath::Kind::STATIC_METHOD:
        case ToitdocPath::Kind::CONSTRUCTOR:
        case ToitdocPath::Kind::FACTORY:
        case ToitdocPath::Kind::METHOD: {
          auto method = resolved->as_Method();
          holder_name = path.klass->name();
          name = method->name();
          shape = method->resolution_shape();
          if (method->has_implicit_this()) {
            // For simplicity remove the implicit this argument in toit-refs.
            shape = shape.without_implicit_this();
          }
          break;
        }

        case ToitdocPath::Kind::FIELD:
          holder_name = path.klass->name();
          name = resolved->as_Field()->name();
          break;
      }
      printf("%d\n", kind_id);
      printf("%s\n", path.module->unit()->absolute_path());
      if (holder_name.is_valid()) print_symbol(holder_name);
      print_symbol(name);
      if (shape.is_valid()) {
        print_shape(shape);
      }
    }
  }

  // The following functions are used as callbacks from `print_list`.
  void visit_Statement(toitdoc::Statement* node) { visit(node); }
  void visit_Expression(toitdoc::Expression* node) { visit(node); }

 private:
  Toitdoc<ir::Node*> _toitdoc;
  UnorderedMap<ir::Node*, ToitdocPath> _paths;

  template<typename T, typename T2>
  void print_list(T elements, void (ToitdocWriter::*callback)(T2)) {
    print_length(elements);
    for (auto element : elements) { (this->*callback)(element); }
  }

  void print_symbol(Symbol symbol) {
    if (!symbol.is_valid()) {
      printf("0\n\n");
    } else {
      const char* str = symbol.c_str();
      printf("%zd\n%s\n", strlen(str), str);
    }
  }

  void print_shape(const ResolutionShape& shape) {
    // We are not dealing with optional arguments, as we know that the
    //   functions are unique and don't overlap. (At least in theory).
    printf("%d\n", shape.max_arity());
    printf("%d\n", shape.total_block_count());
    printf("%d\n", shape.names().length());
    printf("%d\n", shape.named_block_count());
    printf("%s\n", shape.is_setter() ? "setter" : "not-setter");
    for (auto name : shape.names()) {
      print_symbol(name);
    }
  }

};

class Writer {
 public:
  explicit Writer(const std::vector<Module*>& modules,
                  const ToitdocRegistry& toitdocs,
                  int core_index,
                  const UnorderedMap<ir::Node*, ToitdocPath>& paths)
      : _modules(modules)
      , _toitdocs(toitdocs)
      , _core_index(core_index)
      , _paths(paths) { }

  void print_modules();

 private:
  const std::vector<Module*> _modules;
  ToitdocRegistry _toitdocs;
  int _core_index;
  UnorderedMap<ir::Node*, ToitdocPath> _paths;
  UnorderedMap<ir::Node*, int> _toplevel_ids;
  Source* _current_source = null;

  template<typename T> void print_toitdoc(T node);
  void print_range(const Source::Range& range);
  void safe_print_symbol(Symbol symbol);
  void print_toplevel_ref(ir::Node* toplevel_element);
  void print_type(ir::Type type);
  void print_method(ir::Method* method);
  void print_class(ir::Class* klass);
  void print_field(ir::Field* field);
  void print_export(Symbol id, const ResolutionEntry& entry);
  void print_dependencies(Module* module);


  template<typename T, typename T2>
  void print_list(T elements, void (Writer::*callback)(T2)) {
    print_length(elements);
    for (auto element : elements) { (this->*callback)(element); }
  }

  template<typename T, typename F>
  void print_list(T elements, F callback) {
    print_length(elements);
    for (auto element : elements) { callback(element); }
  }
};

template<typename T>
void Writer::print_toitdoc(T node) {
  auto toitdoc = _toitdocs.toitdoc_for(node);
  if (toitdoc.is_valid()) {
    ToitdocWriter toitdoc_writer(toitdoc, _paths);
    toitdoc_writer.write();
  } else {
    printf("0\n");
  }
}

void Writer::print_range(const Source::Range& range) {
  printf("%d\n", _current_source->offset_in_source(range.from()));
  printf("%d\n", _current_source->offset_in_source(range.to()));
}

void Writer::safe_print_symbol(Symbol symbol) {
  if (symbol.is_valid()) {
    printf("%s\n", symbol.c_str());
  } else {
    printf("\n");
  }
}

void Writer::print_toplevel_ref(ir::Node* toplevel_element) {
  printf("%d\n", _toplevel_ids.at(toplevel_element));
}

void Writer::print_type(ir::Type type) {
  if (type.is_any()) {
    printf("-1\n");
  } else if (type.is_none()) {
    printf("-2\n");
  } else if (type.is_class()) {
    print_toplevel_ref(type.klass());
  } else {
    UNREACHABLE();
  }
}

void Writer::print_field(ir::Field* field) {
  safe_print_symbol(field->name());
  print_range(field->range());

  printf("%s\n", field->is_final() ? "final" : "mutable");
  print_type(field->type());
  print_toitdoc(field);
}

void Writer::print_method(ir::Method* method) {
  if (method->name().is_valid()) {
    if (method->is_setter()) {
      printf("%s=\n", method->name().c_str());
    } else {
      printf("%s\n", method->name().c_str());
    }
  } else {
    ASSERT(!method->is_setter());
    safe_print_symbol(method->name());
  }
  print_range(method->range());
  auto probe = _toplevel_ids.find(method);
  printf("%d\n", probe == _toplevel_ids.end() ? -1 : probe->second);
  switch (method->kind()) {
    case ir::Method::INSTANCE:
      if (method->is_FieldStub()) {
        ASSERT(!method->is_abstract());
        printf("field stub\n");
      } else if (method->is_abstract()) {
        printf("abstract\n");
      } else {
        printf("instance\n");
      }
      break;
    case ir::Method::CONSTRUCTOR:
      if (method->as_Constructor()->is_synthetic()) {
        printf("default constructor\n");
      } else {
        printf("constructor\n");
      }
      break;
    case ir::Method::GLOBAL_FUN: printf("global fun\n"); break;
    case ir::Method::GLOBAL_INITIALIZER: printf("global initializer\n"); break;
    case ir::Method::FACTORY: printf("factory\n"); break;
    case ir::Method::FIELD_INITIALIZER: UNREACHABLE();
  }
  auto shape = method->resolution_shape();
  int max_unnamed = shape.max_unnamed_non_block() + shape.unnamed_block_count();
  bool has_implicit_this = method->is_instance() || method->is_constructor();
  printf("%d\n", method->parameters().length() - (has_implicit_this ? 1 : 0));
  for (int i = 0; i < method->parameters().length(); i++) {
    if (has_implicit_this && i == 0) continue;
    auto parameter = method->parameters()[i];
    safe_print_symbol(parameter->name());
    printf("%d\n", parameter->original_index());
    bool is_block = false;
    if (i < shape.min_unnamed_non_block()) {
      printf("required\n");
    } else if (i < shape.max_unnamed_non_block()) {
      printf("optional\n");
    } else if (i < shape.max_unnamed_non_block() + shape.unnamed_block_count()) {
      printf("required\n");
      is_block = true;
    } else if (shape.optional_names()[i - max_unnamed]) {
      printf("optional named\n");
    } else {
      printf("required named\n");
      is_block = i >= shape.max_arity() - shape.named_block_count();
    }
    if (is_block) {
      printf("[block]\n");
    } else {
      print_type(parameter->type());
    }
  }
  print_type(method->return_type());
  print_toitdoc(method);
}

void Writer::print_class(ir::Class* klass) {
  safe_print_symbol(klass->name());
  print_range(klass->range());
  printf("%d\n", _toplevel_ids.at(klass));
  const char* kind;
  if (klass->is_interface()) {
    kind = "interface";
  } else if (klass->is_abstract()) {
    kind = "abstract";
  } else {
    kind = "class";
  }
  printf("%s\n", kind);
  if (klass->super() == null) {
    printf("-1\n");
  } else {
    print_toplevel_ref(klass->super());
  }
  print_list(klass->interfaces(), &Writer::print_toplevel_ref);
  print_list(klass->statics()->nodes(), &Writer::print_method);
  print_list(klass->constructors(), &Writer::print_method);
  print_list(klass->factories(), &Writer::print_method);
  print_list(klass->fields(), &Writer::print_field);
  print_list(klass->methods(), &Writer::print_method);
  print_toitdoc(klass);
}

void Writer::print_export(Symbol exported_id, const ResolutionEntry& entry) {
  safe_print_symbol(exported_id);
  switch (entry.kind()) {
    case ResolutionEntry::PREFIX:
      UNREACHABLE();
    case ResolutionEntry::AMBIGUOUS:
      printf("AMBIGUOUS\n");
      break;
    case ResolutionEntry::NODES:
      printf("NODES\n");
      break;
  }
  print_list(entry.nodes(), [&] (ir::Node* node) {
    ASSERT(node->is_Class() || node->is_Method());
    print_toplevel_ref(node);
  });
}

void Writer::print_dependencies(Module* module) {
  bool is_core = module == _modules[_core_index];
  ListBuilder<const char*> deps;
  if (is_core) {
    // Every module (except for core) implicitly imports core.
    deps.add(_modules[_core_index]->unit()->absolute_path());
  }
  auto unit = module->unit();
  for (auto import : unit->imports()) {
    if (import->unit()->absolute_path()[0] != '\0') {
      deps.add(import->unit()->absolute_path());
    }
  }
  print_list(deps.build(), [&] (const char* dep) {
    printf("%s\n", dep);
  });
}

void Writer::print_modules() {
  auto modules = _modules;
  printf("SUMMARY\n");
  // First print the number of classes in each module, so it's easier to
  // use them for typing and inheritance.
  int module_count = 0;
  for (auto module : modules) {
    // Ignore error modules. These are synthetic modules for
    // imports that couldn't be found.
    if (module->is_error_module()) continue;
    module_count++;
  }
  printf("%d\n", module_count);
  UnorderedMap<ir::Node*, int> toplevel_ids;
  int toplevel_id = 0;
  for (auto module : modules) {
    // Ignore error modules.
    if (module->is_error_module()) continue;
    printf("%s\n", module->unit()->absolute_path());
    printf("%d\n",
           module->classes().length() + module->methods().length() + module->globals().length());
    for (auto klass : module->classes()) {
      toplevel_ids[klass] = toplevel_id++;
    }
    for (auto method : module->methods()) {
      toplevel_ids[method] = toplevel_id++;
    }
    for (auto global : module->globals()) {
      toplevel_ids[global] = toplevel_id++;
    }
  }
  _toplevel_ids = toplevel_ids;

  auto core_module = modules[_core_index];

  for (auto module : modules) {
    // Ignore error modules.
    if (module->is_error_module()) continue;

    _current_source = module->unit()->source();

    // For simplicity repeat the module path and the class count.
    printf("%s\n", _current_source->absolute_path());

    print_dependencies(module);

    List<const char*> exported_modules;
    if (module->export_all()) {
      ListBuilder<const char*> builder;
      for (int i = 0; i < module->imported_modules().length(); i++) {
        auto import = module->imported_modules()[i];
        // The implicitly imported core module is always first. We discard those.
        // Other (explicit) imports of the core module are not discarded.
        if (i == 0 && import.module == core_module) continue;
        // Imports with shown identifiers are handled differently.
        if (!import.show_identifiers.is_empty()) continue;
        // Prefixed imports don't transitively export.
        if (import.prefix != null) continue;
        builder.add(import.module->unit()->absolute_path());
      }
      exported_modules = builder.build();
    }
    print_list(exported_modules, [&](const char* path) { printf("%s\n", path); });
    auto exported_identifiers_map = module->scope()->exported_identifiers_map();
    printf("%d\n", exported_identifiers_map.size());
    exported_identifiers_map.for_each([&](Symbol exported_id, ResolutionEntry entry) {
      print_export(exported_id, entry);
    });
    print_list(module->classes(), &Writer::print_class);
    print_list(module->methods(), &Writer::print_method);
    print_list(module->globals(), &Writer::print_method);
    print_toitdoc(module);
  }
}

class ToitdocPathMappingCreator {
 public:
  /// Runs through the program and collects the toitdoc-paths to nodes that are referenced in toitdocs.
  UnorderedMap<ir::Node*, ToitdocPath> create(const std::vector<Module*>& modules,
                                              ToitdocRegistry toitdocs) {
    toitdocs.for_each([&](void* _, Toitdoc<ir::Node*> toitdoc) {
      for (auto ref : toitdoc.refs()) {
        if (ref == null) continue;

        // No need to collect parameter paths.
        if (ref->is_Parameter()) continue;
        _ref_targets.insert(ref);
      }
    });

    for (auto module : modules) {
      visit_container(ToitdocPath::Kind::CLASS, module, null, module->classes());
      visit_container(ToitdocPath::Kind::GLOBAL_METHOD, module, null, module->methods());
      visit_container(ToitdocPath::Kind::GLOBAL, module, null, module->globals());
      for (auto klass : module->classes()) {
        visit_container(ToitdocPath::Kind::STATIC_METHOD, module, klass, klass->statics()->nodes());
        visit_container(ToitdocPath::Kind::CONSTRUCTOR, module, klass, klass->constructors());
        visit_container(ToitdocPath::Kind::FACTORY, module, klass, klass->factories());
        visit_container(ToitdocPath::Kind::FIELD, module, klass, klass->fields());
        visit_container(ToitdocPath::Kind::METHOD, module, klass, klass->methods());
      }
    }
    return _mapping;
  }

 private:
  Set<ir::Node*> _ref_targets;
  UnorderedMap<ir::Node*, ToitdocPath> _mapping;

  template<typename Container>
  void visit_container(ToitdocPath::Kind kind, Module* module, ir::Class* klass, Container list) {
    for (auto element : list) {
      if (_ref_targets.contains(element)) {
        _mapping[element] = {
          .kind = kind,
          .module = module,
          .klass = klass
        };
      }
    }
  }
};

}  // Anonymous namespace.

void print_summary(const std::vector<Module*>& modules,
                   int core_index,
                   ToitdocRegistry toitdocs) {
  auto paths = ToitdocPathMappingCreator().create(modules, toitdocs);
  Writer writer(modules, toitdocs, core_index, paths);
  writer.print_modules();
}

} // namespace toit::compiler
} // namespace toit
