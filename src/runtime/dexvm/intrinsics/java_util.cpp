// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_util_collections.cpp ----
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/collection_runtime.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_util_collections {
using namespace detail;
namespace {

constexpr std::uint32_t kPublic = 0x0001U;
constexpr std::uint32_t kProtected = 0x0004U;
constexpr std::uint32_t kAbstract = 0x0400U;
constexpr std::uint32_t kSynchronized = 0x0020U;
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

[[noreturn]] void Null(std::string_view what) {
  throw VmJavaThrow{"Ljava/lang/NullPointerException;", std::string(what)};
}

[[noreturn]] void BadIndex(const std::int32_t index, const std::size_t size) {
  throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                    "index " + std::to_string(index) + ", size " +
                        std::to_string(size)};
}

[[noreturn]] void ConcurrentModification() {
  throw VmJavaThrow{"Ljava/util/ConcurrentModificationException;", {}};
}

[[noreturn]] void NoElement() {
  throw VmJavaThrow{"Ljava/util/NoSuchElementException;", {}};
}

[[noreturn]] void IllegalIteratorState() {
  throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                    "iterator has no current element"};
}

[[nodiscard]] std::optional<VmValue>
InvokeVirtual(IntrinsicContext &context, const VmObjectRef receiver,
              const std::string_view name, const std::string_view descriptor,
              std::vector<VmValue> arguments = {}) {
  if (!receiver.IsValid())
    Null("virtual receiver == null");
  auto &vm = context.vm;
  auto &linker = vm.Linker();
  const auto java_class = vm.Model().ObjectClass(receiver);
  const auto index = linker.FindVtableIndex(java_class, std::string(name),
                                            std::string(descriptor));
  if (!index.has_value()) {
    throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                      std::string(name) + std::string(descriptor)};
  }
  arguments.insert(arguments.begin(), VmValue::Ref(receiver));
  const auto outcome =
      vm.Call(linker.Class(java_class).vtable[*index], arguments);
  if (outcome.exception.IsValid()) {
    vm.SetPendingException(outcome.exception);
    return std::nullopt;
  }
  return outcome.value;
}

[[nodiscard]] std::optional<std::int32_t> GuestHash(IntrinsicContext &context,
                                                    const VmObjectRef object) {
  if (!object.IsValid())
    return 0;
  const auto outcome = InvokeVirtual(context, object, "hashCode", "()I");
  if (!outcome.has_value())
    return std::nullopt;
  return outcome->AsInt();
}

[[nodiscard]] std::optional<bool> GuestEquals(IntrinsicContext &context,
                                              const VmObjectRef left,
                                              const VmObjectRef right) {
  if (left == right)
    return true;
  if (!left.IsValid())
    return false;
  const auto outcome = InvokeVirtual(
      context, left, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(right)});
  if (!outcome.has_value())
    return std::nullopt;
  return outcome->AsInt() != 0;
}

struct SequenceWindow final {
  CollectionRuntime::SequenceState *state{};
  CollectionRuntime::SubListState *sub_list{};
  std::size_t offset{};
  std::size_t size{};
};

[[nodiscard]] SequenceWindow Sequence(IntrinsicContext &context,
                                      const VmObjectRef owner) {
  auto &runtime = context.vm.Collections();
  if (auto *view = runtime.FindSubList(owner); view != nullptr) {
    auto *state = runtime.FindSequence(view->owner);
    if (state == nullptr || state->mod_count != view->expected_mod_count) {
      ConcurrentModification();
    }
    return {state, view, view->offset, view->size};
  }
  auto &state = runtime.EnsureSequence(owner);
  return {&state, nullptr, 0, state.elements.size()};
}

void SequenceChanged(SequenceWindow &window) {
  ++window.state->mod_count;
  if (window.sub_list != nullptr) {
    window.sub_list->size = window.size;
    window.sub_list->expected_mod_count = window.state->mod_count;
  }
}

[[nodiscard]] std::size_t CheckedPosition(const std::int32_t index,
                                          const std::size_t size,
                                          const bool allow_end = false) {
  if (index < 0 || static_cast<std::size_t>(index) > size ||
      (!allow_end && static_cast<std::size_t>(index) == size)) {
    BadIndex(index, size);
  }
  return static_cast<std::size_t>(index);
}

struct Lookup final {
  bool failed{};
  std::size_t index{kNoIndex};
};

[[nodiscard]] Lookup SequenceFind(IntrinsicContext &context,
                                  const SequenceWindow &window,
                                  const VmObjectRef value,
                                  const bool reverse = false) {
  for (std::size_t step = 0; step < window.size; ++step) {
    const auto relative = reverse ? window.size - step - 1 : step;
    const auto equal = GuestEquals(
        context, value, window.state->elements[window.offset + relative]);
    if (!equal.has_value())
      return {true, kNoIndex};
    if (*equal)
      return {false, relative};
  }
  return {};
}

[[nodiscard]] Lookup MapFind(IntrinsicContext &context,
                             CollectionRuntime::MapState &map,
                             const VmObjectRef key) {
  const auto hash = GuestHash(context, key);
  if (!hash.has_value())
    return {true, kNoIndex};
  for (std::size_t index = 0; index < map.entries.size(); ++index) {
    if (map.entries[index].hash != *hash)
      continue;
    const auto equal = GuestEquals(context, key, map.entries[index].key);
    if (!equal.has_value())
      return {true, kNoIndex};
    if (*equal)
      return {false, index};
  }
  return {};
}

[[nodiscard]] CollectionRuntime::MapNode *EntryNode(IntrinsicContext &context,
                                                    const VmObjectRef entry) {
  auto &runtime = context.vm.Collections();
  const auto *state = runtime.FindEntry(entry);
  if (state == nullptr)
    IllegalIteratorState();
  auto *map = runtime.FindMap(state->owner);
  if (map == nullptr)
    IllegalIteratorState();
  const auto found = std::ranges::find(map->entries, state->entry_id,
                                       &CollectionRuntime::MapNode::id);
  if (found == map->entries.end())
    IllegalIteratorState();
  return &*found;
}

[[nodiscard]] VmObjectRef
NewIterator(Interpreter &vm, const VmObjectRef owner,
            const CollectionRuntime::IteratorKind kind,
            const std::size_t cursor, const std::uint64_t expected_mod_count) {
  const auto iterator =
      vm.NewIntrinsicInstance("Ljava/util/CollectionIterator;");
  vm.Collections().SetIterator(iterator, {owner, kind, cursor, std::nullopt,
                                          std::nullopt, expected_mod_count});
  return iterator;
}

[[nodiscard]] VmObjectRef NewMapView(Interpreter &vm, const VmObjectRef owner,
                                     const CollectionRuntime::ViewKind kind) {
  std::string_view descriptor;
  switch (kind) {
  case CollectionRuntime::ViewKind::keys:
    descriptor = "Ljava/util/MapKeySet;";
    break;
  case CollectionRuntime::ViewKind::values:
    descriptor = "Ljava/util/MapValues;";
    break;
  case CollectionRuntime::ViewKind::entries:
    descriptor = "Ljava/util/MapEntrySet;";
    break;
  }
  const auto view = vm.NewIntrinsicInstance(descriptor);
  vm.Collections().SetMapView(view, {owner, kind});
  return view;
}

[[nodiscard]] VmObjectRef NewEntry(Interpreter &vm, const VmObjectRef owner,
                                   const std::uint64_t id) {
  const auto entry = vm.NewIntrinsicInstance("Ljava/util/MapEntry;");
  vm.Collections().SetEntry(entry, {owner, id});
  return entry;
}

[[nodiscard]] std::optional<std::vector<VmObjectRef>>
SnapshotCollection(IntrinsicContext &context, const VmObjectRef collection) {
  if (!collection.IsValid())
    Null("collection == null");
  auto &runtime = context.vm.Collections();
  if (runtime.FindSequence(collection) != nullptr ||
      runtime.FindSubList(collection) != nullptr) {
    const auto window = Sequence(context, collection);
    return std::vector<VmObjectRef>(
        window.state->elements.begin() +
            static_cast<std::ptrdiff_t>(window.offset),
        window.state->elements.begin() +
            static_cast<std::ptrdiff_t>(window.offset + window.size));
  }
  if (const auto *view = runtime.FindMapView(collection); view != nullptr) {
    std::vector<VmObjectRef> result;
    const auto *map = runtime.FindMap(view->owner);
    if (map == nullptr)
      return result;
    result.reserve(map->entries.size());
    for (const auto &node : map->entries) {
      switch (view->kind) {
      case CollectionRuntime::ViewKind::keys:
        result.push_back(node.key);
        break;
      case CollectionRuntime::ViewKind::values:
        result.push_back(node.value);
        break;
      case CollectionRuntime::ViewKind::entries:
        result.push_back(NewEntry(context.vm, view->owner, node.id));
        break;
      }
    }
    return result;
  }
  const auto iterator =
      InvokeVirtual(context, collection, "iterator", "()Ljava/util/Iterator;");
  if (!iterator.has_value())
    return std::nullopt;
  std::vector<VmObjectRef> result;
  for (;;) {
    const auto has_next =
        InvokeVirtual(context, iterator->ref, "hasNext", "()Z");
    if (!has_next.has_value())
      return std::nullopt;
    if (has_next->AsInt() == 0)
      break;
    const auto next =
        InvokeVirtual(context, iterator->ref, "next", "()Ljava/lang/Object;");
    if (!next.has_value())
      return std::nullopt;
    result.push_back(next->ref);
  }
  return result;
}

[[nodiscard]] std::optional<std::vector<std::pair<VmObjectRef, VmObjectRef>>>
SnapshotMap(IntrinsicContext &context, const VmObjectRef source) {
  if (!source.IsValid())
    Null("map == null");
  if (const auto *map = context.vm.Collections().FindMap(source);
      map != nullptr) {
    std::vector<std::pair<VmObjectRef, VmObjectRef>> result;
    result.reserve(map->entries.size());
    for (const auto &node : map->entries) {
      result.emplace_back(node.key, node.value);
    }
    return result;
  }
  const auto entry_set =
      InvokeVirtual(context, source, "entrySet", "()Ljava/util/Set;");
  if (!entry_set.has_value())
    return std::nullopt;
  const auto entries = SnapshotCollection(context, entry_set->ref);
  if (!entries.has_value())
    return std::nullopt;
  std::vector<std::pair<VmObjectRef, VmObjectRef>> result;
  result.reserve(entries->size());
  for (const auto entry : *entries) {
    const auto key =
        InvokeVirtual(context, entry, "getKey", "()Ljava/lang/Object;");
    if (!key.has_value())
      return std::nullopt;
    const auto value =
        InvokeVirtual(context, entry, "getValue", "()Ljava/lang/Object;");
    if (!value.has_value())
      return std::nullopt;
    result.emplace_back(key->ref, value->ref);
  }
  return result;
}

void AddSequenceMethods(IntrinsicClassBuilder &builder,
                        const std::uint32_t flags = kPublic,
                        const bool reject_null = false) {
  builder.VirtualMethod(
      "add", "(Ljava/lang/Object;)Z",
      [reject_null](IntrinsicContext &context) {
        if (reject_null && !context.arguments[0].ref.IsValid()) {
          Null("ArrayDeque element == null");
        }
        auto window = Sequence(context, context.receiver);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + window.size),
            context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Int(1);
      },
      flags);
  builder.VirtualMethod(
      "add", "(ILjava/lang/Object;)V",
      [reject_null](IntrinsicContext &context) {
        if (reject_null && !context.arguments[1].ref.IsValid()) {
          Null("ArrayDeque element == null");
        }
        auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size, true);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + index),
            context.arguments[1].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "addAll", "(Ljava/util/Collection;)Z",
      [reject_null](IntrinsicContext &context) {
        const auto incoming =
            SnapshotCollection(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Int(0);
        if (reject_null &&
            std::ranges::any_of(*incoming, [](const VmObjectRef value) {
              return !value.IsValid();
            })) {
          Null("ArrayDeque element == null");
        }
        if (incoming->empty())
          return VmValue::Int(0);
        auto window = Sequence(context, context.receiver);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + window.size),
            incoming->begin(), incoming->end());
        window.size += incoming->size();
        SequenceChanged(window);
        return VmValue::Int(1);
      },
      flags);
  builder.VirtualMethod(
      "addAll", "(ILjava/util/Collection;)Z",
      [reject_null](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size, true);
        const auto incoming =
            SnapshotCollection(context, context.arguments[1].ref);
        if (!incoming.has_value())
          return VmValue::Int(0);
        if (reject_null &&
            std::ranges::any_of(*incoming, [](const VmObjectRef value) {
              return !value.IsValid();
            })) {
          Null("ArrayDeque element == null");
        }
        if (incoming->empty())
          return VmValue::Int(0);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + index),
            incoming->begin(), incoming->end());
        window.size += incoming->size();
        SequenceChanged(window);
        return VmValue::Int(1);
      },
      flags);
  builder.VirtualMethod(
      "get", "(I)Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size);
        return VmValue::Ref(window.state->elements[window.offset + index]);
      },
      flags);
  builder.VirtualMethod(
      "set", "(ILjava/lang/Object;)Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size);
        auto &slot = window.state->elements[window.offset + index];
        const auto previous = slot;
        slot = context.arguments[1].ref;
        return VmValue::Ref(previous);
      },
      flags);
  builder.VirtualMethod(
      "remove", "(I)Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size);
        const auto absolute = window.offset + index;
        const auto previous = window.state->elements[absolute];
        window.state->elements.erase(window.state->elements.begin() +
                                     static_cast<std::ptrdiff_t>(absolute));
        --window.size;
        SequenceChanged(window);
        return VmValue::Ref(previous);
      },
      flags);
  builder.VirtualMethod(
      "remove", "(Ljava/lang/Object;)Z",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        const auto found =
            SequenceFind(context, window, context.arguments[0].ref);
        if (found.failed)
          return VmValue::Int(0);
        if (found.index == kNoIndex)
          return VmValue::Int(0);
        window.state->elements.erase(
            window.state->elements.begin() +
            static_cast<std::ptrdiff_t>(window.offset + found.index));
        --window.size;
        SequenceChanged(window);
        return VmValue::Int(1);
      },
      flags);
  builder.VirtualMethod(
      "size", "()I",
      [](IntrinsicContext &context) {
        return VmValue::Int(static_cast<std::int32_t>(
            Sequence(context, context.receiver).size));
      },
      flags);
  builder.VirtualMethod(
      "isEmpty", "()Z",
      [](IntrinsicContext &context) {
        return VmValue::Int(Sequence(context, context.receiver).size == 0);
      },
      flags);
  builder.VirtualMethod(
      "clear", "()V",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        if (window.size != 0) {
          window.state->elements.erase(
              window.state->elements.begin() +
                  static_cast<std::ptrdiff_t>(window.offset),
              window.state->elements.begin() +
                  static_cast<std::ptrdiff_t>(window.offset + window.size));
          window.size = 0;
          SequenceChanged(window);
        }
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "contains", "(Ljava/lang/Object;)Z",
      [](IntrinsicContext &context) {
        const auto found =
            SequenceFind(context, Sequence(context, context.receiver),
                         context.arguments[0].ref);
        return VmValue::Int(!found.failed && found.index != kNoIndex);
      },
      flags);
  builder.VirtualMethod(
      "containsAll", "(Ljava/util/Collection;)Z",
      [](IntrinsicContext &context) {
        const auto incoming =
            SnapshotCollection(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Int(0);
        const auto window = Sequence(context, context.receiver);
        for (const auto value : *incoming) {
          const auto found = SequenceFind(context, window, value);
          if (found.failed)
            return VmValue::Int(0);
          if (found.index == kNoIndex)
            return VmValue::Int(0);
        }
        return VmValue::Int(1);
      },
      flags);
  const auto filter = [flags](IntrinsicClassBuilder &target,
                              const std::string &name, const bool retain) {
    target.VirtualMethod(
        name, "(Ljava/util/Collection;)Z",
        [retain](IntrinsicContext &context) {
          const auto incoming =
              SnapshotCollection(context, context.arguments[0].ref);
          if (!incoming.has_value())
            return VmValue::Int(0);
          auto window = Sequence(context, context.receiver);
          bool changed = false;
          std::size_t relative = 0;
          while (relative < window.size) {
            bool contained = false;
            for (const auto candidate : *incoming) {
              const auto equal =
                  GuestEquals(context, candidate,
                              window.state->elements[window.offset + relative]);
              if (!equal.has_value())
                return VmValue::Int(0);
              if (*equal) {
                contained = true;
                break;
              }
            }
            if (contained != retain) {
              window.state->elements.erase(
                  window.state->elements.begin() +
                  static_cast<std::ptrdiff_t>(window.offset + relative));
              --window.size;
              changed = true;
            } else {
              ++relative;
            }
          }
          if (changed)
            SequenceChanged(window);
          return VmValue::Int(changed);
        },
        flags);
  };
  filter(builder, "removeAll", false);
  filter(builder, "retainAll", true);
  builder.VirtualMethod(
      "indexOf", "(Ljava/lang/Object;)I",
      [](IntrinsicContext &context) {
        const auto found =
            SequenceFind(context, Sequence(context, context.receiver),
                         context.arguments[0].ref);
        return VmValue::Int(found.failed || found.index == kNoIndex
                                ? -1
                                : static_cast<std::int32_t>(found.index));
      },
      flags);
  builder.VirtualMethod(
      "lastIndexOf", "(Ljava/lang/Object;)I",
      [](IntrinsicContext &context) {
        const auto found =
            SequenceFind(context, Sequence(context, context.receiver),
                         context.arguments[0].ref, true);
        return VmValue::Int(found.failed || found.index == kNoIndex
                                ? -1
                                : static_cast<std::int32_t>(found.index));
      },
      flags);
  builder.VirtualMethod(
      "iterator", "()Ljava/util/Iterator;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        return VmValue::Ref(
            NewIterator(context.vm, context.receiver,
                        CollectionRuntime::IteratorKind::sequence, 0,
                        window.state->mod_count));
      },
      flags);
  builder.VirtualMethod(
      "listIterator", "()Ljava/util/ListIterator;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        return VmValue::Ref(
            NewIterator(context.vm, context.receiver,
                        CollectionRuntime::IteratorKind::sequence, 0,
                        window.state->mod_count));
      },
      flags);
  builder.VirtualMethod(
      "listIterator", "(I)Ljava/util/ListIterator;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size, true);
        return VmValue::Ref(
            NewIterator(context.vm, context.receiver,
                        CollectionRuntime::IteratorKind::sequence, index,
                        window.state->mod_count));
      },
      flags);
  builder.VirtualMethod(
      "subList", "(II)Ljava/util/List;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto begin =
            CheckedPosition(context.arguments[0].AsInt(), window.size, true);
        const auto end =
            CheckedPosition(context.arguments[1].AsInt(), window.size, true);
        if (end < begin)
          BadIndex(context.arguments[1].AsInt(), window.size);
        const auto view =
            context.vm.NewIntrinsicInstance("Ljava/util/SubList;");
        const auto base_owner = window.sub_list == nullptr
                                    ? context.receiver
                                    : window.sub_list->owner;
        context.vm.Collections().SetSubList(
            view, {base_owner, window.offset + begin, end - begin,
                   window.state->mod_count});
        return VmValue::Ref(view);
      },
      flags);
  builder.VirtualMethod(
      "toArray", "()[Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto element_class =
            context.vm.Linker().ResolveDescriptor("Ljava/lang/Object;");
        const auto array_class =
            context.vm.Linker().ResolveDescriptor("[Ljava/lang/Object;");
        const auto array = context.vm.Model().NewObjectArray(
            array_class, element_class, static_cast<JniSize>(window.size));
        for (std::size_t index = 0; index < window.size; ++index) {
          context.vm.Model().SetObjectElement(
              array, static_cast<JniSize>(index),
              window.state->elements[window.offset + index]);
        }
        return VmValue::Ref(array);
      },
      flags);
  builder.VirtualMethod(
      "toArray", "([Ljava/lang/Object;)[Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto target = context.arguments[0].ref;
        if (!target.IsValid())
          Null("array == null");
        const auto window = Sequence(context, context.receiver);
        auto result = target;
        const auto target_length =
            static_cast<std::size_t>(context.vm.Model().ArrayLength(target));
        if (target_length < window.size) {
          const auto array_class = context.vm.Model().ObjectClass(target);
          const auto component =
              context.vm.Model().ObjectArrayElementClass(target);
          result = context.vm.Model().NewObjectArray(
              array_class, component, static_cast<JniSize>(window.size));
        }
        for (std::size_t index = 0; index < window.size; ++index) {
          context.vm.Model().SetObjectElement(
              result, static_cast<JniSize>(index),
              window.state->elements[window.offset + index]);
        }
        if (target_length > window.size) {
          context.vm.Model().SetObjectElement(
              result, static_cast<JniSize>(window.size), VmObjectRef{});
        }
        return VmValue::Ref(result);
      },
      flags);
}

struct MapMethodDeclarer final {
  IntrinsicClassBuilder &target;
  bool overrides{};

  void VirtualMethod(std::string name, std::string descriptor,
                     IntrinsicHandler handler,
                     const std::uint32_t flags = kPublic) const {
    if (overrides) {
      target.OverrideMethod(std::move(name), std::move(descriptor),
                            std::move(handler), flags);
    } else {
      target.VirtualMethod(std::move(name), std::move(descriptor),
                           std::move(handler), flags);
    }
  }
};

void AddMapMethods(IntrinsicClassBuilder &raw_builder, const bool reject_null,
                   const bool linked, const std::uint32_t flags = kPublic,
                   const bool overrides = false) {
  const MapMethodDeclarer builder{raw_builder, overrides};
  builder.VirtualMethod(
      "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
      [reject_null, linked](IntrinsicContext &context) {
        const auto key = context.arguments[0].ref;
        const auto value = context.arguments[1].ref;
        if (reject_null && (!key.IsValid() || !value.IsValid())) {
          Null("Hashtable does not permit null keys or values");
        }
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, key);
        if (found.failed)
          return VmValue::Ref(VmObjectRef{});
        if (found.index != kNoIndex) {
          auto &node = map.entries[found.index];
          const auto previous = node.value;
          node.value = value;
          if (map.access_order && found.index + 1 != map.entries.size()) {
            const auto moved = node;
            map.entries.erase(map.entries.begin() +
                              static_cast<std::ptrdiff_t>(found.index));
            map.entries.push_back(moved);
            ++map.mod_count;
          }
          return VmValue::Ref(previous);
        }
        const auto hash = GuestHash(context, key);
        if (!hash.has_value())
          return VmValue::Ref(VmObjectRef{});
        map.entries.push_back({map.next_id++, key, value, *hash});
        ++map.mod_count;
        if (linked && map.entries.size() > 1) {
          const auto eldest =
              NewEntry(context.vm, context.receiver, map.entries.front().id);
          const auto remove =
              InvokeVirtual(context, context.receiver, "removeEldestEntry",
                            "(Ljava/util/Map$Entry;)Z", {VmValue::Ref(eldest)});
          if (!remove.has_value())
            return VmValue::Ref(VmObjectRef{});
          if (remove->AsInt() != 0 && !map.entries.empty()) {
            map.entries.erase(map.entries.begin());
            ++map.mod_count;
          }
        }
        return VmValue::Ref(VmObjectRef{});
      },
      flags);
  builder.VirtualMethod(
      "putAll", "(Ljava/util/Map;)V",
      [reject_null, linked](IntrinsicContext &context) {
        const auto incoming = SnapshotMap(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Void();
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        for (const auto &[key, value] : *incoming) {
          if (reject_null && (!key.IsValid() || !value.IsValid())) {
            Null("Hashtable does not permit null keys or values");
          }
          const auto found = MapFind(context, map, key);
          if (found.failed)
            return VmValue::Void();
          if (found.index != kNoIndex) {
            map.entries[found.index].value = value;
            continue;
          }
          const auto hash = GuestHash(context, key);
          if (!hash.has_value())
            return VmValue::Void();
          map.entries.push_back({map.next_id++, key, value, *hash});
          ++map.mod_count;
          if (linked && map.entries.size() > 1) {
            const auto eldest =
                NewEntry(context.vm, context.receiver, map.entries.front().id);
            const auto remove = InvokeVirtual(
                context, context.receiver, "removeEldestEntry",
                "(Ljava/util/Map$Entry;)Z", {VmValue::Ref(eldest)});
            if (!remove.has_value())
              return VmValue::Void();
            if (remove->AsInt() != 0 && !map.entries.empty()) {
              map.entries.erase(map.entries.begin());
              ++map.mod_count;
            }
          }
        }
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
      [reject_null](IntrinsicContext &context) {
        const auto key = context.arguments[0].ref;
        if (reject_null && !key.IsValid())
          Null("Hashtable key == null");
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, key);
        if (found.failed || found.index == kNoIndex) {
          return VmValue::Ref(VmObjectRef{});
        }
        const auto value = map.entries[found.index].value;
        if (map.access_order && found.index + 1 != map.entries.size()) {
          const auto moved = map.entries[found.index];
          map.entries.erase(map.entries.begin() +
                            static_cast<std::ptrdiff_t>(found.index));
          map.entries.push_back(moved);
          ++map.mod_count;
        }
        return VmValue::Ref(value);
      },
      flags);
  builder.VirtualMethod(
      "containsKey", "(Ljava/lang/Object;)Z",
      [reject_null](IntrinsicContext &context) {
        if (reject_null && !context.arguments[0].ref.IsValid()) {
          Null("Hashtable key == null");
        }
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, context.arguments[0].ref);
        return VmValue::Int(!found.failed && found.index != kNoIndex);
      },
      flags);
  builder.VirtualMethod(
      "containsValue", "(Ljava/lang/Object;)Z",
      [reject_null](IntrinsicContext &context) {
        const auto value = context.arguments[0].ref;
        if (reject_null && !value.IsValid())
          Null("Hashtable value == null");
        const auto &map = context.vm.Collections().EnsureMap(context.receiver);
        for (const auto &node : map.entries) {
          const auto equal = GuestEquals(context, value, node.value);
          if (!equal.has_value())
            return VmValue::Int(0);
          if (*equal)
            return VmValue::Int(1);
        }
        return VmValue::Int(0);
      },
      flags);
  builder.VirtualMethod(
      "remove", "(Ljava/lang/Object;)Ljava/lang/Object;",
      [reject_null](IntrinsicContext &context) {
        const auto key = context.arguments[0].ref;
        if (reject_null && !key.IsValid())
          Null("Hashtable key == null");
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, key);
        if (found.failed || found.index == kNoIndex) {
          return VmValue::Ref(VmObjectRef{});
        }
        const auto previous = map.entries[found.index].value;
        map.entries.erase(map.entries.begin() +
                          static_cast<std::ptrdiff_t>(found.index));
        ++map.mod_count;
        return VmValue::Ref(previous);
      },
      flags);
  builder.VirtualMethod(
      "size", "()I",
      [](IntrinsicContext &context) {
        return VmValue::Int(
            static_cast<std::int32_t>(context.vm.Collections()
                                          .EnsureMap(context.receiver)
                                          .entries.size()));
      },
      flags);
  builder.VirtualMethod(
      "isEmpty", "()Z",
      [](IntrinsicContext &context) {
        return VmValue::Int(context.vm.Collections()
                                .EnsureMap(context.receiver)
                                .entries.empty());
      },
      flags);
  builder.VirtualMethod(
      "clear", "()V",
      [](IntrinsicContext &context) {
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        if (!map.entries.empty()) {
          map.entries.clear();
          ++map.mod_count;
        }
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "keySet", "()Ljava/util/Set;",
      [](IntrinsicContext &context) {
        return VmValue::Ref(NewMapView(context.vm, context.receiver,
                                       CollectionRuntime::ViewKind::keys));
      },
      flags);
  builder.VirtualMethod(
      "values", "()Ljava/util/Collection;",
      [](IntrinsicContext &context) {
        return VmValue::Ref(NewMapView(context.vm, context.receiver,
                                       CollectionRuntime::ViewKind::values));
      },
      flags);
  builder.VirtualMethod(
      "entrySet", "()Ljava/util/Set;",
      [](IntrinsicContext &context) {
        return VmValue::Ref(NewMapView(context.vm, context.receiver,
                                       CollectionRuntime::ViewKind::entries));
      },
      flags);
}

void AddSetMethods(IntrinsicClassBuilder &builder) {
  builder.VirtualMethod(
      "add", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto key = context.arguments[0].ref;
        const auto found = MapFind(context, map, key);
        if (found.failed)
          return VmValue::Int(0);
        if (found.index != kNoIndex)
          return VmValue::Int(0);
        const auto hash = GuestHash(context, key);
        if (!hash.has_value())
          return VmValue::Int(0);
        map.entries.push_back({map.next_id++, key, VmObjectRef{}, *hash});
        ++map.mod_count;
        return VmValue::Int(1);
      });
  builder.VirtualMethod(
      "contains", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, context.arguments[0].ref);
        return VmValue::Int(!found.failed && found.index != kNoIndex);
      });
  builder.VirtualMethod(
      "remove", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        const auto found = MapFind(context, map, context.arguments[0].ref);
        if (found.failed || found.index == kNoIndex)
          return VmValue::Int(0);
        map.entries.erase(map.entries.begin() +
                          static_cast<std::ptrdiff_t>(found.index));
        ++map.mod_count;
        return VmValue::Int(1);
      });
  builder.VirtualMethod("size", "()I", [](IntrinsicContext &context) {
    return VmValue::Int(static_cast<std::int32_t>(
        context.vm.Collections().EnsureMap(context.receiver).entries.size()));
  });
  builder.VirtualMethod("isEmpty", "()Z", [](IntrinsicContext &context) {
    return VmValue::Int(
        context.vm.Collections().EnsureMap(context.receiver).entries.empty());
  });
  builder.VirtualMethod("clear", "()V", [](IntrinsicContext &context) {
    auto &map = context.vm.Collections().EnsureMap(context.receiver);
    if (!map.entries.empty()) {
      map.entries.clear();
      ++map.mod_count;
    }
    return VmValue::Void();
  });
  builder.VirtualMethod(
      "iterator", "()Ljava/util/Iterator;", [](IntrinsicContext &context) {
        const auto &map = context.vm.Collections().EnsureMap(context.receiver);
        return VmValue::Ref(NewIterator(
            context.vm, context.receiver,
            CollectionRuntime::IteratorKind::map_keys, 0, map.mod_count));
      });
  builder.VirtualMethod(
      "addAll", "(Ljava/util/Collection;)Z", [](IntrinsicContext &context) {
        const auto incoming =
            SnapshotCollection(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Int(0);
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        bool changed = false;
        for (const auto key : *incoming) {
          const auto found = MapFind(context, map, key);
          if (found.failed)
            return VmValue::Int(0);
          if (found.index != kNoIndex)
            continue;
          const auto hash = GuestHash(context, key);
          if (!hash.has_value())
            return VmValue::Int(0);
          map.entries.push_back({map.next_id++, key, VmObjectRef{}, *hash});
          ++map.mod_count;
          changed = true;
        }
        return VmValue::Int(changed);
      });
}

void AddDequeMethods(IntrinsicClassBuilder &builder, const bool reject_null) {
  builder.VirtualMethod("addFirst", "(Ljava/lang/Object;)V",
                        [reject_null](IntrinsicContext &context) {
                          if (reject_null &&
                              !context.arguments[0].ref.IsValid()) {
                            Null("ArrayDeque element == null");
                          }
                          auto window = Sequence(context, context.receiver);
                          window.state->elements.insert(
                              window.state->elements.begin() +
                                  static_cast<std::ptrdiff_t>(window.offset),
                              context.arguments[0].ref);
                          ++window.size;
                          SequenceChanged(window);
                          return VmValue::Void();
                        });
  builder.VirtualMethod(
      "addLast", "(Ljava/lang/Object;)V",
      [reject_null](IntrinsicContext &context) {
        if (reject_null && !context.arguments[0].ref.IsValid()) {
          Null("ArrayDeque element == null");
        }
        auto window = Sequence(context, context.receiver);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + window.size),
            context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Void();
      });
  const auto end_value = [](IntrinsicContext &context, const bool first,
                            const bool remove, const bool nullable) {
    auto window = Sequence(context, context.receiver);
    if (window.size == 0) {
      if (nullable)
        return VmValue::Ref(VmObjectRef{});
      NoElement();
    }
    const auto index = first ? window.offset : window.offset + window.size - 1;
    const auto value = window.state->elements[index];
    if (remove) {
      window.state->elements.erase(window.state->elements.begin() +
                                   static_cast<std::ptrdiff_t>(index));
      --window.size;
      SequenceChanged(window);
    }
    return VmValue::Ref(value);
  };
  builder.VirtualMethod("getFirst", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, false, false);
                        });
  builder.VirtualMethod("getLast", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, false, false, false);
                        });
  builder.VirtualMethod("removeFirst", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, true, false);
                        });
  builder.VirtualMethod("removeLast", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, false, true, false);
                        });
  builder.VirtualMethod("peekFirst", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, false, true);
                        });
  builder.VirtualMethod("peekLast", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, false, false, true);
                        });
  builder.VirtualMethod("pollFirst", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, true, true);
                        });
  builder.VirtualMethod("pollLast", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, false, true, true);
                        });
  builder.VirtualMethod("peek", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, false, true);
                        });
  builder.VirtualMethod("poll", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, true, true);
                        });
  builder.VirtualMethod("remove", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, true, false);
                        });
  builder.VirtualMethod("element", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, false, false);
                        });
  builder.VirtualMethod("push", "(Ljava/lang/Object;)V",
                        [reject_null](IntrinsicContext &context) {
                          if (reject_null &&
                              !context.arguments[0].ref.IsValid()) {
                            Null("ArrayDeque element == null");
                          }
                          auto window = Sequence(context, context.receiver);
                          window.state->elements.insert(
                              window.state->elements.begin() +
                                  static_cast<std::ptrdiff_t>(window.offset),
                              context.arguments[0].ref);
                          ++window.size;
                          SequenceChanged(window);
                          return VmValue::Void();
                        });
  builder.VirtualMethod("pop", "()Ljava/lang/Object;",
                        [end_value](IntrinsicContext &c) {
                          return end_value(c, true, true, false);
                        });
  builder.VirtualMethod(
      "offer", "(Ljava/lang/Object;)Z",
      [reject_null](IntrinsicContext &context) {
        if (reject_null && !context.arguments[0].ref.IsValid()) {
          Null("ArrayDeque element == null");
        }
        auto window = Sequence(context, context.receiver);
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + window.size),
            context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Int(1);
      });
  const auto offer_end = [reject_null](IntrinsicContext &context,
                                       const bool first) {
    if (reject_null && !context.arguments[0].ref.IsValid()) {
      Null("ArrayDeque element == null");
    }
    auto window = Sequence(context, context.receiver);
    const auto position = window.offset + (first ? 0 : window.size);
    window.state->elements.insert(window.state->elements.begin() +
                                      static_cast<std::ptrdiff_t>(position),
                                  context.arguments[0].ref);
    ++window.size;
    SequenceChanged(window);
    return VmValue::Int(1);
  };
  builder.VirtualMethod(
      "offerFirst", "(Ljava/lang/Object;)Z",
      [offer_end](IntrinsicContext &c) { return offer_end(c, true); });
  builder.VirtualMethod(
      "offerLast", "(Ljava/lang/Object;)Z",
      [offer_end](IntrinsicContext &c) { return offer_end(c, false); });
  const auto remove_occurrence = [](IntrinsicContext &context,
                                    const bool reverse) {
    auto window = Sequence(context, context.receiver);
    const auto found =
        SequenceFind(context, window, context.arguments[0].ref, reverse);
    if (found.failed || found.index == kNoIndex)
      return VmValue::Int(0);
    window.state->elements.erase(
        window.state->elements.begin() +
        static_cast<std::ptrdiff_t>(window.offset + found.index));
    --window.size;
    SequenceChanged(window);
    return VmValue::Int(1);
  };
  builder.VirtualMethod("removeFirstOccurrence", "(Ljava/lang/Object;)Z",
                        [remove_occurrence](IntrinsicContext &c) {
                          return remove_occurrence(c, false);
                        });
  builder.VirtualMethod("removeLastOccurrence", "(Ljava/lang/Object;)Z",
                        [remove_occurrence](IntrinsicContext &c) {
                          return remove_occurrence(c, true);
                        });
  builder.VirtualMethod("descendingIterator", "()Ljava/util/Iterator;",
                        [](IntrinsicContext &context) {
                          const auto window =
                              Sequence(context, context.receiver);
                          return VmValue::Ref(NewIterator(
                              context.vm, context.receiver,
                              CollectionRuntime::IteratorKind::sequence_reverse,
                              window.size, window.state->mod_count));
                        });
}

void AddVectorMethods(IntrinsicClassBuilder &builder,
                      const std::uint32_t flags) {
  builder.VirtualMethod(
      "addElement", "(Ljava/lang/Object;)V",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        window.state->elements.push_back(context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "elementAt", "(I)Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size);
        return VmValue::Ref(window.state->elements[window.offset + index]);
      },
      flags);
  builder.VirtualMethod(
      "lastElement", "()Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        if (window.size == 0)
          NoElement();
        return VmValue::Ref(
            window.state->elements[window.offset + window.size - 1]);
      },
      flags);
  builder.VirtualMethod(
      "removeElementAt", "(I)V",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[0].AsInt(), window.size);
        window.state->elements.erase(
            window.state->elements.begin() +
            static_cast<std::ptrdiff_t>(window.offset + index));
        --window.size;
        SequenceChanged(window);
        return VmValue::Void();
      },
      flags);
  builder.VirtualMethod(
      "setElementAt", "(Ljava/lang/Object;I)V",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto index =
            CheckedPosition(context.arguments[1].AsInt(), window.size);
        window.state->elements[window.offset + index] =
            context.arguments[0].ref;
        return VmValue::Void();
      },
      flags);
}

void AddStackMethods(IntrinsicClassBuilder &builder) {
  constexpr auto flags = kPublic | kSynchronized;
  builder.VirtualMethod(
      "push", "(Ljava/lang/Object;)Ljava/lang/Object;",
      [](IntrinsicContext &context) {
        auto window = Sequence(context, context.receiver);
        window.state->elements.push_back(context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        return VmValue::Ref(context.arguments[0].ref);
      },
      flags);
  const auto top = [](IntrinsicContext &context, const bool remove) {
    auto window = Sequence(context, context.receiver);
    if (window.size == 0) {
      throw VmJavaThrow{"Ljava/util/EmptyStackException;", {}};
    }
    const auto value = window.state->elements[window.offset + window.size - 1];
    if (remove) {
      window.state->elements.erase(
          window.state->elements.begin() +
          static_cast<std::ptrdiff_t>(window.offset + window.size - 1));
      --window.size;
      SequenceChanged(window);
    }
    return VmValue::Ref(value);
  };
  builder.VirtualMethod(
      "pop", "()Ljava/lang/Object;",
      [top](IntrinsicContext &context) { return top(context, true); }, flags);
  builder.VirtualMethod(
      "peek", "()Ljava/lang/Object;",
      [top](IntrinsicContext &context) { return top(context, false); }, flags);
  builder.VirtualMethod(
      "empty", "()Z",
      [](IntrinsicContext &context) {
        return VmValue::Int(Sequence(context, context.receiver).size == 0);
      },
      flags);
  builder.VirtualMethod(
      "search", "(Ljava/lang/Object;)I",
      [](IntrinsicContext &context) {
        const auto window = Sequence(context, context.receiver);
        const auto found =
            SequenceFind(context, window, context.arguments[0].ref, true);
        if (found.failed || found.index == kNoIndex)
          return VmValue::Int(-1);
        return VmValue::Int(
            static_cast<std::int32_t>(window.size - found.index));
      },
      flags);
}

void DeclareCollectionMethods(IntrinsicClassBuilder &builder) {
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"add", "(Ljava/lang/Object;)Z"},
           {"addAll", "(Ljava/util/Collection;)Z"},
           {"clear", "()V"},
           {"contains", "(Ljava/lang/Object;)Z"},
           {"containsAll", "(Ljava/util/Collection;)Z"},
           {"isEmpty", "()Z"},
           {"iterator", "()Ljava/util/Iterator;"},
           {"remove", "(Ljava/lang/Object;)Z"},
           {"removeAll", "(Ljava/util/Collection;)Z"},
           {"retainAll", "(Ljava/util/Collection;)Z"},
           {"size", "()I"},
           {"toArray", "()[Ljava/lang/Object;"},
           {"toArray", "([Ljava/lang/Object;)[Ljava/lang/Object;"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
}

IntrinsicClassDecl DeclareCollection() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Collection;",
                                                  {"Ljava/lang/Iterable;"});
  DeclareCollectionMethods(builder);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareList() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/List;",
                                                  {"Ljava/util/Collection;"});
  DeclareCollectionMethods(builder);
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"add", "(ILjava/lang/Object;)V"},
           {"get", "(I)Ljava/lang/Object;"},
           {"set", "(ILjava/lang/Object;)Ljava/lang/Object;"},
           {"remove", "(I)Ljava/lang/Object;"},
           {"indexOf", "(Ljava/lang/Object;)I"},
           {"lastIndexOf", "(Ljava/lang/Object;)I"},
           {"listIterator", "()Ljava/util/ListIterator;"},
           {"listIterator", "(I)Ljava/util/ListIterator;"},
           {"subList", "(II)Ljava/util/List;"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSet() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Set;",
                                                  {"Ljava/util/Collection;"});
  DeclareCollectionMethods(builder);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMapEntryInterface() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Map$Entry;");
  builder.UnimplementedVirtual("getKey", "()Ljava/lang/Object;",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("getValue", "()Ljava/lang/Object;",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("setValue",
                               "(Ljava/lang/Object;)Ljava/lang/Object;",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("equals", "(Ljava/lang/Object;)Z",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("hashCode", "()I", kPublic | kAbstract);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMap() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Map;");
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"clear", "()V"},
           {"containsKey", "(Ljava/lang/Object;)Z"},
           {"containsValue", "(Ljava/lang/Object;)Z"},
           {"entrySet", "()Ljava/util/Set;"},
           {"get", "(Ljava/lang/Object;)Ljava/lang/Object;"},
           {"isEmpty", "()Z"},
           {"keySet", "()Ljava/util/Set;"},
           {"put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"},
           {"putAll", "(Ljava/util/Map;)V"},
           {"remove", "(Ljava/lang/Object;)Ljava/lang/Object;"},
           {"size", "()I"},
           {"values", "()Ljava/util/Collection;"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareIteratorInterface() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Iterator;");
  builder.UnimplementedVirtual("hasNext", "()Z", kPublic | kAbstract);
  builder.UnimplementedVirtual("next", "()Ljava/lang/Object;",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("remove", "()V", kPublic | kAbstract);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareListIteratorInterface() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/ListIterator;",
                                                  {"Ljava/util/Iterator;"});
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"add", "(Ljava/lang/Object;)V"},
           {"hasNext", "()Z"},
           {"hasPrevious", "()Z"},
           {"next", "()Ljava/lang/Object;"},
           {"nextIndex", "()I"},
           {"previous", "()Ljava/lang/Object;"},
           {"previousIndex", "()I"},
           {"remove", "()V"},
           {"set", "(Ljava/lang/Object;)V"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMarker(std::string descriptor) {
  return std::move(IntrinsicClassBuilder::Interface(std::move(descriptor)))
      .Build();
}

IntrinsicClassDecl DeclareQueue() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Queue;",
                                                  {"Ljava/util/Collection;"});
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"add", "(Ljava/lang/Object;)Z"},
           {"element", "()Ljava/lang/Object;"},
           {"offer", "(Ljava/lang/Object;)Z"},
           {"peek", "()Ljava/lang/Object;"},
           {"poll", "()Ljava/lang/Object;"},
           {"remove", "()Ljava/lang/Object;"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDeque() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Deque;",
                                                  {"Ljava/util/Queue;"});
  for (const auto &[name, descriptor] :
       std::vector<std::pair<std::string, std::string>>{
           {"addFirst", "(Ljava/lang/Object;)V"},
           {"addLast", "(Ljava/lang/Object;)V"},
           {"getFirst", "()Ljava/lang/Object;"},
           {"getLast", "()Ljava/lang/Object;"},
           {"removeFirst", "()Ljava/lang/Object;"},
           {"removeLast", "()Ljava/lang/Object;"},
           {"offerFirst", "(Ljava/lang/Object;)Z"},
           {"offerLast", "(Ljava/lang/Object;)Z"},
           {"peekFirst", "()Ljava/lang/Object;"},
           {"peekLast", "()Ljava/lang/Object;"},
           {"pollFirst", "()Ljava/lang/Object;"},
           {"pollLast", "()Ljava/lang/Object;"},
           {"removeFirstOccurrence", "(Ljava/lang/Object;)Z"},
           {"removeLastOccurrence", "(Ljava/lang/Object;)Z"},
           {"descendingIterator", "()Ljava/util/Iterator;"},
           {"push", "(Ljava/lang/Object;)V"},
           {"pop", "()Ljava/lang/Object;"}}) {
    builder.UnimplementedVirtual(name, descriptor, kPublic | kAbstract);
  }
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareEnumeration() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Enumeration;");
  builder.UnimplementedVirtual("hasMoreElements", "()Z", kPublic | kAbstract);
  builder.UnimplementedVirtual("nextElement", "()Ljava/lang/Object;",
                               kPublic | kAbstract);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareComparator() {
  auto builder = IntrinsicClassBuilder::Interface("Ljava/util/Comparator;");
  builder.UnimplementedVirtual("compare",
                               "(Ljava/lang/Object;Ljava/lang/Object;)I",
                               kPublic | kAbstract);
  builder.UnimplementedVirtual("equals", "(Ljava/lang/Object;)Z",
                               kPublic | kAbstract);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareAbstract(std::string descriptor,
                                   std::string superclass,
                                   std::vector<std::string> interfaces) {
  auto builder =
      IntrinsicClassBuilder::Class(std::move(descriptor), std::move(superclass),
                                   std::move(interfaces), kPublic | kAbstract);
  builder.Constructor(
      "()V", [](IntrinsicContext &) { return VmValue::Void(); }, kProtected);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSequenceClass(
    std::string descriptor, std::string superclass,
    std::vector<std::string> interfaces, const bool deque = false,
    const bool synchronized = false, const bool vector_methods = false,
    const bool stack_methods = false, const bool collection_constructor = true,
    const bool capacity_constructor = true, const bool reject_null = false) {
  auto builder = IntrinsicClassBuilder::Class(
      std::move(descriptor), std::move(superclass), std::move(interfaces));
  builder.Constructor("()V", [](IntrinsicContext &context) {
    context.vm.Collections().EnsureSequence(context.receiver) = {};
    return VmValue::Void();
  });
  if (capacity_constructor) {
    builder.Constructor("(I)V", [](IntrinsicContext &context) {
      if (context.arguments[0].AsInt() < 0) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "capacity < 0"};
      }
      context.vm.Collections().EnsureSequence(context.receiver) = {};
      return VmValue::Void();
    });
  }
  if (collection_constructor) {
    builder.Constructor(
        "(Ljava/util/Collection;)V", [reject_null](IntrinsicContext &context) {
          const auto incoming =
              SnapshotCollection(context, context.arguments[0].ref);
          if (!incoming.has_value())
            return VmValue::Void();
          if (reject_null &&
              std::ranges::any_of(*incoming, [](const VmObjectRef value) {
                return !value.IsValid();
              })) {
            Null("ArrayDeque element == null");
          }
          auto &state =
              context.vm.Collections().EnsureSequence(context.receiver);
          state = {};
          state.elements = *incoming;
          return VmValue::Void();
        });
  }
  if (!stack_methods) {
    AddSequenceMethods(builder,
                       synchronized ? kPublic | kSynchronized : kPublic,
                       reject_null);
  }
  if (deque)
    AddDequeMethods(builder, reject_null);
  if (vector_methods && !stack_methods) {
    AddVectorMethods(builder, synchronized ? kPublic | kSynchronized : kPublic);
  }
  if (stack_methods)
    AddStackMethods(builder);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMapClass(std::string descriptor,
                                   std::string superclass,
                                   std::vector<std::string> interfaces,
                                   const bool linked, const bool reject_null,
                                   const bool synchronized = false) {
  auto builder = IntrinsicClassBuilder::Class(
      std::move(descriptor), std::move(superclass), std::move(interfaces));
  builder.Constructor("()V", [linked](IntrinsicContext &context) {
    auto &map = context.vm.Collections().EnsureMap(context.receiver);
    map = {};
    map.access_order = false;
    static_cast<void>(linked);
    return VmValue::Void();
  });
  builder.Constructor("(I)V", [](IntrinsicContext &context) {
    if (context.arguments[0].AsInt() < 0) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "capacity < 0"};
    }
    context.vm.Collections().EnsureMap(context.receiver) = {};
    return VmValue::Void();
  });
  builder.Constructor("(IF)V", [](IntrinsicContext &context) {
    if (context.arguments[0].AsInt() < 0) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "capacity < 0"};
    }
    const auto factor = context.arguments[1].AsFloat();
    if (!(factor > 0.0F) || factor != factor) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "invalid load factor"};
    }
    context.vm.Collections().EnsureMap(context.receiver) = {};
    return VmValue::Void();
  });
  if (linked) {
    builder.Constructor("(IFZ)V", [](IntrinsicContext &context) {
      if (context.arguments[0].AsInt() < 0) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "capacity < 0"};
      }
      const auto factor = context.arguments[1].AsFloat();
      if (!(factor > 0.0F) || factor != factor) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "invalid load factor"};
      }
      auto &map = context.vm.Collections().EnsureMap(context.receiver);
      map = {};
      map.access_order = context.arguments[2].AsInt() != 0;
      return VmValue::Void();
    });
  }
  builder.Constructor(
      "(Ljava/util/Map;)V", [reject_null](IntrinsicContext &context) {
        const auto incoming = SnapshotMap(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Void();
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        map = {};
        for (const auto &[key, value] : *incoming) {
          if (reject_null && (!key.IsValid() || !value.IsValid())) {
            Null("Hashtable does not permit null keys or values");
          }
          const auto hash = GuestHash(context, key);
          if (!hash.has_value())
            return VmValue::Void();
          map.entries.push_back({map.next_id++, key, value, *hash});
        }
        return VmValue::Void();
      });
  if (linked) {
    builder.VirtualMethod(
        "removeEldestEntry", "(Ljava/util/Map$Entry;)Z",
        [](IntrinsicContext &) { return VmValue::Int(0); }, kProtected);
  }
  AddMapMethods(builder, reject_null, linked,
                synchronized ? kPublic | kSynchronized : kPublic, linked);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSetClass(std::string descriptor,
                                   std::string superclass,
                                   std::vector<std::string> interfaces) {
  const bool inherits_set_methods = superclass == "Ljava/util/HashSet;";
  auto builder = IntrinsicClassBuilder::Class(
      std::move(descriptor), std::move(superclass), std::move(interfaces));
  builder.Constructor("()V", [](IntrinsicContext &context) {
    context.vm.Collections().EnsureMap(context.receiver) = {};
    return VmValue::Void();
  });
  builder.Constructor("(I)V", [](IntrinsicContext &context) {
    if (context.arguments[0].AsInt() < 0) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "capacity < 0"};
    }
    context.vm.Collections().EnsureMap(context.receiver) = {};
    return VmValue::Void();
  });
  builder.Constructor(
      "(Ljava/util/Collection;)V", [](IntrinsicContext &context) {
        const auto incoming =
            SnapshotCollection(context, context.arguments[0].ref);
        if (!incoming.has_value())
          return VmValue::Void();
        auto &map = context.vm.Collections().EnsureMap(context.receiver);
        map = {};
        for (const auto key : *incoming) {
          const auto hash = GuestHash(context, key);
          if (!hash.has_value())
            return VmValue::Void();
          const auto found = MapFind(context, map, key);
          if (found.failed)
            return VmValue::Void();
          if (found.index == kNoIndex) {
            map.entries.push_back({map.next_id++, key, VmObjectRef{}, *hash});
          }
        }
        return VmValue::Void();
      });
  if (!inherits_set_methods) AddSetMethods(builder);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSubList() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/util/SubList;",
                                              "Ljava/util/AbstractList;",
                                              {"Ljava/util/RandomAccess;"});
  AddSequenceMethods(builder);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareCollectionIterator() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/util/CollectionIterator;",
                                              "Ljava/lang/Object;",
                                              {"Ljava/util/ListIterator;"});
  builder.VirtualMethod("hasNext", "()Z", [](IntrinsicContext &context) {
    auto *iterator = context.vm.Collections().FindIterator(context.receiver);
    if (iterator == nullptr)
      IllegalIteratorState();
    if (iterator->kind == CollectionRuntime::IteratorKind::sequence ||
        iterator->kind == CollectionRuntime::IteratorKind::sequence_reverse) {
      const auto window = Sequence(context, iterator->owner);
      if (window.state->mod_count != iterator->expected_mod_count)
        ConcurrentModification();
      return VmValue::Int(iterator->kind ==
                                  CollectionRuntime::IteratorKind::sequence
                              ? iterator->cursor < window.size
                              : iterator->cursor > 0);
    }
    auto *map = context.vm.Collections().FindMap(iterator->owner);
    if (map == nullptr || map->mod_count != iterator->expected_mod_count)
      ConcurrentModification();
    return VmValue::Int(iterator->cursor < map->entries.size());
  });
  builder.VirtualMethod("hasPrevious", "()Z", [](IntrinsicContext &context) {
    auto *iterator = context.vm.Collections().FindIterator(context.receiver);
    if (iterator == nullptr ||
        iterator->kind != CollectionRuntime::IteratorKind::sequence) {
      return VmValue::Int(0);
    }
    const auto window = Sequence(context, iterator->owner);
    if (window.state->mod_count != iterator->expected_mod_count)
      ConcurrentModification();
    return VmValue::Int(iterator->cursor > 0);
  });
  builder.VirtualMethod(
      "next", "()Ljava/lang/Object;", [](IntrinsicContext &context) {
        auto *iterator =
            context.vm.Collections().FindIterator(context.receiver);
        if (iterator == nullptr)
          IllegalIteratorState();
        if (iterator->kind == CollectionRuntime::IteratorKind::sequence ||
            iterator->kind ==
                CollectionRuntime::IteratorKind::sequence_reverse) {
          const auto window = Sequence(context, iterator->owner);
          if (window.state->mod_count != iterator->expected_mod_count)
            ConcurrentModification();
          std::size_t index{};
          if (iterator->kind == CollectionRuntime::IteratorKind::sequence) {
            if (iterator->cursor >= window.size)
              NoElement();
            index = iterator->cursor++;
          } else {
            if (iterator->cursor == 0)
              NoElement();
            index = --iterator->cursor;
          }
          iterator->last_index = index;
          return VmValue::Ref(window.state->elements[window.offset + index]);
        }
        auto *map = context.vm.Collections().FindMap(iterator->owner);
        if (map == nullptr || map->mod_count != iterator->expected_mod_count)
          ConcurrentModification();
        if (iterator->cursor >= map->entries.size())
          NoElement();
        const auto &node = map->entries[iterator->cursor++];
        iterator->last_entry_id = node.id;
        switch (iterator->kind) {
        case CollectionRuntime::IteratorKind::map_keys:
          return VmValue::Ref(node.key);
        case CollectionRuntime::IteratorKind::map_values:
          return VmValue::Ref(node.value);
        case CollectionRuntime::IteratorKind::map_entries:
          return VmValue::Ref(NewEntry(context.vm, iterator->owner, node.id));
        default:
          break;
        }
        NoElement();
      });
  builder.VirtualMethod(
      "previous", "()Ljava/lang/Object;", [](IntrinsicContext &context) {
        auto *iterator =
            context.vm.Collections().FindIterator(context.receiver);
        if (iterator == nullptr ||
            iterator->kind != CollectionRuntime::IteratorKind::sequence)
          NoElement();
        const auto window = Sequence(context, iterator->owner);
        if (window.state->mod_count != iterator->expected_mod_count)
          ConcurrentModification();
        if (iterator->cursor == 0)
          NoElement();
        const auto index = --iterator->cursor;
        iterator->last_index = index;
        return VmValue::Ref(window.state->elements[window.offset + index]);
      });
  builder.VirtualMethod("nextIndex", "()I", [](IntrinsicContext &context) {
    auto *iterator = context.vm.Collections().FindIterator(context.receiver);
    if (iterator == nullptr)
      IllegalIteratorState();
    return VmValue::Int(static_cast<std::int32_t>(iterator->cursor));
  });
  builder.VirtualMethod("previousIndex", "()I", [](IntrinsicContext &context) {
    auto *iterator = context.vm.Collections().FindIterator(context.receiver);
    if (iterator == nullptr)
      IllegalIteratorState();
    return VmValue::Int(iterator->cursor == 0
                            ? -1
                            : static_cast<std::int32_t>(iterator->cursor - 1));
  });
  builder.VirtualMethod("remove", "()V", [](IntrinsicContext &context) {
    auto *iterator = context.vm.Collections().FindIterator(context.receiver);
    if (iterator == nullptr)
      IllegalIteratorState();
    if (iterator->last_index.has_value()) {
      auto window = Sequence(context, iterator->owner);
      if (window.state->mod_count != iterator->expected_mod_count)
        ConcurrentModification();
      const auto index = *iterator->last_index;
      window.state->elements.erase(
          window.state->elements.begin() +
          static_cast<std::ptrdiff_t>(window.offset + index));
      --window.size;
      SequenceChanged(window);
      if (index < iterator->cursor)
        --iterator->cursor;
      iterator->expected_mod_count = window.state->mod_count;
      iterator->last_index.reset();
      return VmValue::Void();
    }
    if (iterator->last_entry_id.has_value()) {
      auto *map = context.vm.Collections().FindMap(iterator->owner);
      if (map == nullptr || map->mod_count != iterator->expected_mod_count)
        ConcurrentModification();
      const auto found =
          std::ranges::find(map->entries, *iterator->last_entry_id,
                            &CollectionRuntime::MapNode::id);
      if (found == map->entries.end())
        IllegalIteratorState();
      const auto index = static_cast<std::size_t>(found - map->entries.begin());
      map->entries.erase(found);
      ++map->mod_count;
      if (index < iterator->cursor)
        --iterator->cursor;
      iterator->expected_mod_count = map->mod_count;
      iterator->last_entry_id.reset();
      return VmValue::Void();
    }
    IllegalIteratorState();
  });
  builder.VirtualMethod(
      "set", "(Ljava/lang/Object;)V", [](IntrinsicContext &context) {
        auto *iterator =
            context.vm.Collections().FindIterator(context.receiver);
        if (iterator == nullptr || !iterator->last_index.has_value())
          IllegalIteratorState();
        const auto window = Sequence(context, iterator->owner);
        if (window.state->mod_count != iterator->expected_mod_count)
          ConcurrentModification();
        window.state->elements[window.offset + *iterator->last_index] =
            context.arguments[0].ref;
        return VmValue::Void();
      });
  builder.VirtualMethod(
      "add", "(Ljava/lang/Object;)V", [](IntrinsicContext &context) {
        auto *iterator =
            context.vm.Collections().FindIterator(context.receiver);
        if (iterator == nullptr ||
            iterator->kind != CollectionRuntime::IteratorKind::sequence)
          IllegalIteratorState();
        auto window = Sequence(context, iterator->owner);
        if (window.state->mod_count != iterator->expected_mod_count)
          ConcurrentModification();
        window.state->elements.insert(
            window.state->elements.begin() +
                static_cast<std::ptrdiff_t>(window.offset + iterator->cursor),
            context.arguments[0].ref);
        ++window.size;
        SequenceChanged(window);
        ++iterator->cursor;
        iterator->expected_mod_count = window.state->mod_count;
        iterator->last_index.reset();
        return VmValue::Void();
      });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMapView(std::string descriptor,
                                  std::string interface_descriptor) {
  auto builder =
      IntrinsicClassBuilder::Class(std::move(descriptor), "Ljava/lang/Object;",
                                   {std::move(interface_descriptor)});
  builder.VirtualMethod("size", "()I", [](IntrinsicContext &context) {
    const auto *view = context.vm.Collections().FindMapView(context.receiver);
    if (view == nullptr)
      IllegalIteratorState();
    const auto *map = context.vm.Collections().FindMap(view->owner);
    return VmValue::Int(
        map == nullptr ? 0 : static_cast<std::int32_t>(map->entries.size()));
  });
  builder.VirtualMethod("isEmpty", "()Z", [](IntrinsicContext &context) {
    const auto *view = context.vm.Collections().FindMapView(context.receiver);
    if (view == nullptr)
      IllegalIteratorState();
    const auto *map = context.vm.Collections().FindMap(view->owner);
    return VmValue::Int(map == nullptr || map->entries.empty());
  });
  builder.VirtualMethod("clear", "()V", [](IntrinsicContext &context) {
    const auto *view = context.vm.Collections().FindMapView(context.receiver);
    if (view == nullptr)
      IllegalIteratorState();
    auto *map = context.vm.Collections().FindMap(view->owner);
    if (map != nullptr && !map->entries.empty()) {
      map->entries.clear();
      ++map->mod_count;
    }
    return VmValue::Void();
  });
  builder.VirtualMethod(
      "contains", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        const auto *view =
            context.vm.Collections().FindMapView(context.receiver);
        if (view == nullptr)
          IllegalIteratorState();
        auto *map = context.vm.Collections().FindMap(view->owner);
        if (map == nullptr)
          return VmValue::Int(0);
        const auto candidate = context.arguments[0].ref;
        if (view->kind == CollectionRuntime::ViewKind::keys) {
          const auto found = MapFind(context, *map, candidate);
          return VmValue::Int(!found.failed && found.index != kNoIndex);
        }
        if (view->kind == CollectionRuntime::ViewKind::values) {
          for (const auto &node : map->entries) {
            const auto equal = GuestEquals(context, candidate, node.value);
            if (!equal.has_value())
              return VmValue::Int(0);
            if (*equal)
              return VmValue::Int(1);
          }
          return VmValue::Int(0);
        }
        if (!candidate.IsValid())
          return VmValue::Int(0);
        const auto key =
            InvokeVirtual(context, candidate, "getKey", "()Ljava/lang/Object;");
        if (!key.has_value())
          return VmValue::Int(0);
        const auto value = InvokeVirtual(context, candidate, "getValue",
                                         "()Ljava/lang/Object;");
        if (!value.has_value())
          return VmValue::Int(0);
        const auto found = MapFind(context, *map, key->ref);
        if (found.failed || found.index == kNoIndex)
          return VmValue::Int(0);
        const auto equal =
            GuestEquals(context, value->ref, map->entries[found.index].value);
        return VmValue::Int(equal.has_value() && *equal);
      });
  builder.VirtualMethod(
      "remove", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        const auto *view =
            context.vm.Collections().FindMapView(context.receiver);
        if (view == nullptr)
          IllegalIteratorState();
        auto *map = context.vm.Collections().FindMap(view->owner);
        if (map == nullptr)
          return VmValue::Int(0);
        const auto candidate = context.arguments[0].ref;
        std::size_t index = kNoIndex;
        if (view->kind == CollectionRuntime::ViewKind::keys) {
          const auto found = MapFind(context, *map, candidate);
          if (found.failed)
            return VmValue::Int(0);
          index = found.index;
        } else if (view->kind == CollectionRuntime::ViewKind::values) {
          for (std::size_t current = 0; current < map->entries.size();
               ++current) {
            const auto equal =
                GuestEquals(context, candidate, map->entries[current].value);
            if (!equal.has_value())
              return VmValue::Int(0);
            if (*equal) {
              index = current;
              break;
            }
          }
        } else if (candidate.IsValid()) {
          const auto key = InvokeVirtual(context, candidate, "getKey",
                                         "()Ljava/lang/Object;");
          if (!key.has_value())
            return VmValue::Int(0);
          const auto value = InvokeVirtual(context, candidate, "getValue",
                                           "()Ljava/lang/Object;");
          if (!value.has_value())
            return VmValue::Int(0);
          const auto found = MapFind(context, *map, key->ref);
          if (found.failed || found.index == kNoIndex) {
            return VmValue::Int(0);
          }
          const auto equal =
              GuestEquals(context, value->ref, map->entries[found.index].value);
          if (!equal.has_value() || !*equal)
            return VmValue::Int(0);
          index = found.index;
        }
        if (index == kNoIndex)
          return VmValue::Int(0);
        map->entries.erase(map->entries.begin() +
                           static_cast<std::ptrdiff_t>(index));
        ++map->mod_count;
        return VmValue::Int(1);
      });
  builder.VirtualMethod(
      "iterator", "()Ljava/util/Iterator;", [](IntrinsicContext &context) {
        const auto *view =
            context.vm.Collections().FindMapView(context.receiver);
        if (view == nullptr)
          IllegalIteratorState();
        auto &map = context.vm.Collections().EnsureMap(view->owner);
        CollectionRuntime::IteratorKind kind{};
        switch (view->kind) {
        case CollectionRuntime::ViewKind::keys:
          kind = CollectionRuntime::IteratorKind::map_keys;
          break;
        case CollectionRuntime::ViewKind::values:
          kind = CollectionRuntime::IteratorKind::map_values;
          break;
        case CollectionRuntime::ViewKind::entries:
          kind = CollectionRuntime::IteratorKind::map_entries;
          break;
        }
        return VmValue::Ref(
            NewIterator(context.vm, view->owner, kind, 0, map.mod_count));
      });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareMapEntry() {
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/util/MapEntry;", "Ljava/lang/Object;", {"Ljava/util/Map$Entry;"});
  builder.VirtualMethod(
      "getKey", "()Ljava/lang/Object;", [](IntrinsicContext &context) {
        return VmValue::Ref(EntryNode(context, context.receiver)->key);
      });
  builder.VirtualMethod(
      "getValue", "()Ljava/lang/Object;", [](IntrinsicContext &context) {
        return VmValue::Ref(EntryNode(context, context.receiver)->value);
      });
  builder.VirtualMethod("setValue", "(Ljava/lang/Object;)Ljava/lang/Object;",
                        [](IntrinsicContext &context) {
                          auto *node = EntryNode(context, context.receiver);
                          const auto previous = node->value;
                          node->value = context.arguments[0].ref;
                          return VmValue::Ref(previous);
                        });
  builder.OverrideMethod(
      "equals", "(Ljava/lang/Object;)Z", [](IntrinsicContext &context) {
        const auto other = context.arguments[0].ref;
        if (!other.IsValid())
          return VmValue::Int(0);
        auto *node = EntryNode(context, context.receiver);
        const auto key =
            InvokeVirtual(context, other, "getKey", "()Ljava/lang/Object;");
        if (!key.has_value())
          return VmValue::Int(0);
        const auto value =
            InvokeVirtual(context, other, "getValue", "()Ljava/lang/Object;");
        if (!value.has_value())
          return VmValue::Int(0);
        const auto keys_equal = GuestEquals(context, node->key, key->ref);
        if (!keys_equal.has_value() || !*keys_equal) {
          return VmValue::Int(0);
        }
        const auto values_equal = GuestEquals(context, node->value, value->ref);
        return VmValue::Int(values_equal.has_value() && *values_equal);
      });
  builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext &context) {
    auto *node = EntryNode(context, context.receiver);
    const auto key_hash = GuestHash(context, node->key);
    if (!key_hash.has_value())
      return VmValue::Int(0);
    const auto value_hash = GuestHash(context, node->value);
    if (!value_hash.has_value())
      return VmValue::Int(0);
    return VmValue::Int(*key_hash ^ *value_hash);
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareConcurrentModificationException() {
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/util/ConcurrentModificationException;",
      "Ljava/lang/RuntimeException;");
  builder.Constructor("()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext &context) {
    context.vm.SetThrowableMessage(context.receiver, context.arguments[0].ref);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareNoSuchElementException() {
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/util/NoSuchElementException;", "Ljava/lang/RuntimeException;");
  builder.Constructor("()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext &context) {
    context.vm.SetThrowableMessage(context.receiver, context.arguments[0].ref);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareEmptyStackException() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/util/EmptyStackException;",
                                              "Ljava/lang/RuntimeException;");
  builder.Constructor("()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  return std::move(builder).Build();
}

} // namespace

void AppendJavaUtilCollections(std::vector<IntrinsicClassDecl> &catalog) {
  catalog.push_back(DeclareCollection());
  catalog.push_back(DeclareList());
  catalog.push_back(DeclareSet());
  catalog.push_back(DeclareMapEntryInterface());
  catalog.push_back(DeclareMap());
  catalog.push_back(DeclareIteratorInterface());
  catalog.push_back(DeclareListIteratorInterface());
  catalog.push_back(DeclareQueue());
  catalog.push_back(DeclareDeque());
  catalog.push_back(DeclareEnumeration());
  catalog.push_back(DeclareComparator());
  catalog.push_back(DeclareMarker("Ljava/util/RandomAccess;"));

  catalog.push_back(DeclareAbstract("Ljava/util/AbstractCollection;",
                                    "Ljava/lang/Object;",
                                    {"Ljava/util/Collection;"}));
  catalog.push_back(DeclareAbstract("Ljava/util/AbstractList;",
                                    "Ljava/util/AbstractCollection;",
                                    {"Ljava/util/List;"}));
  catalog.push_back(DeclareAbstract("Ljava/util/AbstractSequentialList;",
                                    "Ljava/util/AbstractList;", {}));
  catalog.push_back(DeclareAbstract("Ljava/util/AbstractSet;",
                                    "Ljava/util/AbstractCollection;",
                                    {"Ljava/util/Set;"}));
  catalog.push_back(DeclareAbstract("Ljava/util/AbstractMap;",
                                    "Ljava/lang/Object;", {"Ljava/util/Map;"}));
  catalog.push_back(DeclareAbstract("Ljava/util/AbstractQueue;",
                                    "Ljava/util/AbstractCollection;",
                                    {"Ljava/util/Queue;"}));

  catalog.push_back(
      DeclareSequenceClass("Ljava/util/ArrayList;", "Ljava/util/AbstractList;",
                           {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;",
                            "Ljava/util/RandomAccess;"}));
  catalog.push_back(DeclareSequenceClass(
      "Ljava/util/LinkedList;", "Ljava/util/AbstractSequentialList;",
      {"Ljava/util/Deque;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
      true));
  catalog.push_back(DeclareSequenceClass(
      "Ljava/util/ArrayDeque;", "Ljava/util/AbstractCollection;",
      {"Ljava/util/Deque;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
      true, false, false, false, true, true, true));
  catalog.push_back(
      DeclareSequenceClass("Ljava/util/Vector;", "Ljava/util/AbstractList;",
                           {"Ljava/util/List;", "Ljava/util/RandomAccess;",
                            "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
                           false, true, true));
  catalog.push_back(DeclareSequenceClass("Ljava/util/Stack;",
                                         "Ljava/util/Vector;", {}, false, true,
                                         false, true, false, false));
  catalog.push_back(DeclareSubList());

  catalog.push_back(DeclareMapClass(
      "Ljava/util/HashMap;", "Ljava/util/AbstractMap;",
      {"Ljava/util/Map;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
      false, false));
  catalog.push_back(DeclareMapClass("Ljava/util/LinkedHashMap;",
                                    "Ljava/util/HashMap;", {}, true, false));
  catalog.push_back(DeclareMapClass(
      "Ljava/util/Hashtable;", "Ljava/lang/Object;",
      {"Ljava/util/Map;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"},
      false, true, true));
  catalog.push_back(DeclareSetClass(
      "Ljava/util/HashSet;", "Ljava/util/AbstractSet;",
      {"Ljava/util/Set;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"}));
  catalog.push_back(DeclareSetClass(
      "Ljava/util/LinkedHashSet;", "Ljava/util/HashSet;",
      {"Ljava/util/Set;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"}));

  catalog.push_back(DeclareMapView("Ljava/util/MapKeySet;", "Ljava/util/Set;"));
  catalog.push_back(
      DeclareMapView("Ljava/util/MapValues;", "Ljava/util/Collection;"));
  catalog.push_back(
      DeclareMapView("Ljava/util/MapEntrySet;", "Ljava/util/Set;"));
  catalog.push_back(DeclareMapEntry());
  catalog.push_back(DeclareCollectionIterator());
  catalog.push_back(DeclareConcurrentModificationException());
  catalog.push_back(DeclareNoSuchElementException());
  catalog.push_back(DeclareEmptyStackException());
}

} // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
void AppendJavaUtilCollections(std::vector<IntrinsicClassDecl>& catalog) {
    dvm80_java_util_collections::AppendJavaUtilCollections(catalog);
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from dexvm_android java.util platform classes ----

namespace ogplay::runtime::dexvm::intrinsics {

namespace {

IntrinsicClassDecl DeclarePlatformLocale(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Locale;",
                                                "Ljava/lang/Object;");
    builder.StaticMethod(
        "getDefault", "()Ljava/util/Locale;",
        [services](IntrinsicContext& call) {
            if (services.singleton) {
                return VmValue::Ref(services.singleton(
                    call.vm, "locale", "Ljava/util/Locale;"));
            }
            return VmValue::Ref(
                call.vm.NewIntrinsicInstance("Ljava/util/Locale;"));
        });
    builder.FinalMethod(
        "getISO3Language", "()Ljava/lang/String;",
        [language = services.iso3_language](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(language));
        });
    builder.FinalMethod(
        "getISO3Country", "()Ljava/lang/String;",
        [country = services.iso3_country](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(country));
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclarePlatformTimer(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Timer;",
                                                "Ljava/lang/Object;");
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    builder.Constructor("(Z)V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V",
                        [](IntrinsicContext& call) {
                            if (!call.arguments[0].ref.IsValid()) {
                                throw VmJavaThrow{
                                    "Ljava/lang/NullPointerException;",
                                    "Timer name is null"};
                            }
                            return VmValue::Void();
                        });
    builder.Constructor("(Ljava/lang/String;Z)V",
                        [](IntrinsicContext& call) {
                            if (!call.arguments[0].ref.IsValid()) {
                                throw VmJavaThrow{
                                    "Ljava/lang/NullPointerException;",
                                    "Timer name is null"};
                            }
                            return VmValue::Void();
                        });
    builder.FinalMethod(
        "schedule", "(Ljava/util/TimerTask;J)V",
        [schedule = services.schedule_timer_task](IntrinsicContext& call) {
            const auto task = call.arguments[0].ref;
            if (!task.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "scheduled TimerTask is null"};
            }
            if (!schedule) {
                throw VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "Timer scheduling needs platform services"};
            }
            schedule(call.vm, call.receiver, task,
                     call.arguments[1].AsLong(), -1, false);
            return VmValue::Void();
        });
    builder.FinalMethod(
        "schedule", "(Ljava/util/TimerTask;JJ)V",
        [schedule = services.schedule_timer_task](IntrinsicContext& call) {
            const auto task = call.arguments[0].ref;
            if (!task.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "scheduled TimerTask is null"};
            }
            if (!schedule) {
                throw VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "Timer scheduling needs platform services"};
            }
            schedule(call.vm, call.receiver, task,
                     call.arguments[1].AsLong(),
                     call.arguments[2].AsLong(), false);
            return VmValue::Void();
        });
    builder.FinalMethod(
        "scheduleAtFixedRate", "(Ljava/util/TimerTask;JJ)V",
        [schedule = services.schedule_timer_task](IntrinsicContext& call) {
            const auto task = call.arguments[0].ref;
            if (!task.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "scheduled TimerTask is null"};
            }
            if (!schedule) {
                throw VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "Timer scheduling needs platform services"};
            }
            schedule(call.vm, call.receiver, task,
                     call.arguments[1].AsLong(),
                     call.arguments[2].AsLong(), true);
            return VmValue::Void();
        });
    builder.FinalMethod(
        "cancel", "()V",
        [cancel = services.cancel_timer](IntrinsicContext& call) {
            if (cancel) cancel(call.receiver);
            return VmValue::Void();
        });
    builder.FinalMethod("purge", "()I", [](IntrinsicContext&) {
        return VmValue::Int(0);
    });
    return std::move(builder).Build();
}

IntrinsicClassDecl DeclarePlatformTimerTask(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/TimerTask;", "Ljava/lang/Object;",
        {"Ljava/lang/Runnable;"});
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    builder.FinalMethod(
        "cancel", "()Z",
        [cancel = services.cancel_timer_task](IntrinsicContext& call) {
            return VmValue::Int(cancel && cancel(call.receiver) ? 1 : 0);
        });
    builder.FinalMethod(
        "scheduledExecutionTime", "()J",
        [scheduled = services.timer_task_scheduled_execution_time](
            IntrinsicContext& call) {
            return VmValue::Long(scheduled ? scheduled(call.receiver) : 0);
        });
    builder.UnimplementedVirtual("run", "()V", 0x0001U | 0x0400U);
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaUtilPlatform(std::vector<IntrinsicClassDecl>& catalog,
                            const CoreIntrinsicServices& services) {
    catalog.push_back(DeclarePlatformLocale(services));
    catalog.push_back(DeclarePlatformTimer(services));
    catalog.push_back(DeclarePlatformTimerTask(services));
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_util_Date.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_util_Date {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Date() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Date;", "Ljava/lang/Object;");
    builder.InstanceField("millis", "J");
    builder.UnimplementedConstructor("()V");
    builder.UnimplementedFinal("getTime", "()J");
    builder.UnimplementedFinal("getYear", "()I");
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_util_Date() {
    return dvm80_java_util_Date::Declare_java_util_Date();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_util_Random.cpp ----
#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics::dvm80_java_util_Random {
using namespace detail;

IntrinsicClassDecl Declare_java_util_Random() {
    const auto engines =
        std::make_shared<std::unordered_map<std::uint32_t, std::mt19937_64>>();
    const auto counter = std::make_shared<std::uint64_t>(0x5DEECE66DULL);
    const auto engine_of =
        [engines](IntrinsicContext& context) -> std::mt19937_64& {
        const auto found = engines->find(context.receiver.Value());
        if (found == engines->end()) {
            throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                              "Random instance was never constructed"};
        }
        return found->second;
    };
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Random;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [engines, counter](IntrinsicContext &context) {
                  (*engines)[context.receiver.Value()] = std::mt19937_64((*counter)++);
                    return VmValue::Void();
                });
    builder.Constructor("(J)V",
        [engines](IntrinsicContext &context) {
                    (*engines)[context.receiver.Value()] = std::mt19937_64(
                        static_cast<std::uint64_t>(context.arguments[0].AsLong()));
                    return VmValue::Void();
                });
    builder.FinalMethod("nextDouble", "()D",
        [engine_of](IntrinsicContext &context) {
                    auto& engine = engine_of(context);
                    VmValue out;
                    out.kind = VmValue::Kind::wide;
                  const double value = static_cast<double>(engine() >> 11U) * 0x1.0p-53;
                    out.wide = std::bit_cast<std::uint64_t>(value);
                    return out;
                });
    builder.FinalMethod("nextInt", "(I)I",
        [engine_of](IntrinsicContext &context) {
                    const auto bound = context.arguments[0].AsInt();
                    if (bound <= 0) {
                        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                          "bound must be positive"};
                    }
                    return VmValue::Int(static_cast<std::int32_t>(
                      engine_of(context)() % static_cast<std::uint64_t>(bound)));
                });
    builder.FinalMethod("nextInt", "()I",
        [engine_of](IntrinsicContext &context) {
                  return VmValue::Int(static_cast<std::int32_t>(engine_of(context)()));
                });
    builder.FinalMethod("nextFloat", "()F",
        [engine_of](IntrinsicContext &context) {
                    auto& engine = engine_of(context);
                  const float value = static_cast<float>(engine() >> 40U) * 0x1.0p-24F;
                    VmValue out = VmValue::Int(0);
                    out.cat1 = std::bit_cast<std::uint32_t>(value);
                    return out;
                });
    builder.FinalMethod("nextLong", "()J",
        [engine_of](IntrinsicContext &context) {
                  return VmValue::Long(static_cast<std::int64_t>(engine_of(context)()));
                });
    auto result = std::move(builder).Build();
    return result;
}

}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_util_Random() {
    return dvm80_java_util_Random::Declare_java_util_Random();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- DVM-87 API 19 Arrays/Collections algorithms ----

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[noreturn]] void Dvm87Null(const std::string_view what) {
    throw VmJavaThrow{"Ljava/lang/NullPointerException;", std::string(what)};
}

[[noreturn]] void Dvm87BadIndex(const std::int32_t index,
                                const std::size_t size) {
    throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                      "index " + std::to_string(index) + ", size " +
                          std::to_string(size)};
}

[[nodiscard]] std::optional<VmValue> Dvm87InvokeVirtual(
    IntrinsicContext& context, const VmObjectRef receiver,
    const std::string_view name, const std::string_view descriptor,
    std::vector<VmValue> arguments = {}) {
    if (!receiver.IsValid()) Dvm87Null("virtual receiver == null");
    auto& linker = context.vm.Linker();
    const auto java_class = context.vm.Model().ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(
        java_class, std::string(name), std::string(descriptor));
    if (!index.has_value()) {
        throw VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                          std::string(name) + std::string(descriptor)};
    }
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    const auto outcome = context.vm.Call(
        linker.Class(java_class).vtable[*index], arguments);
    if (outcome.exception.IsValid()) {
        context.vm.SetPendingException(outcome.exception);
        return std::nullopt;
    }
    return outcome.value;
}

[[nodiscard]] std::optional<bool> Dvm87GuestEquals(
    IntrinsicContext& context, const VmObjectRef left,
    const VmObjectRef right) {
    if (left == right) return true;
    if (!left.IsValid()) return false;
    const auto outcome = Dvm87InvokeVirtual(
        context, left, "equals", "(Ljava/lang/Object;)Z",
        {VmValue::Ref(right)});
    return outcome.has_value()
               ? std::optional<bool>(outcome->AsInt() != 0)
               : std::nullopt;
}

[[nodiscard]] std::optional<std::vector<VmObjectRef>>
Dvm87SnapshotCollection(IntrinsicContext& context,
                        const VmObjectRef collection) {
    const auto iterator = Dvm87InvokeVirtual(
        context, collection, "iterator", "()Ljava/util/Iterator;");
    if (!iterator.has_value()) return std::nullopt;
    std::vector<VmObjectRef> values;
    while (true) {
        const auto has_next = Dvm87InvokeVirtual(
            context, iterator->ref, "hasNext", "()Z");
        if (!has_next.has_value()) return std::nullopt;
        if (has_next->AsInt() == 0) break;
        const auto next = Dvm87InvokeVirtual(
            context, iterator->ref, "next", "()Ljava/lang/Object;");
        if (!next.has_value()) return std::nullopt;
        values.push_back(next->ref);
    }
    return values;
}

[[noreturn]] void Dvm87ArrayNull() {
    throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
}

void Dvm87CheckRange(const JavaObjectModel& model, const VmObjectRef array,
                     const std::int32_t from, const std::int32_t to) {
    if (!array.IsValid()) Dvm87ArrayNull();
    const auto length = static_cast<std::int32_t>(model.ArrayLength(array));
    if (from > to) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "fromIndex > toIndex"};
    }
    if (from < 0 || to > length) {
        throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                          "array range is out of bounds"};
    }
}

[[nodiscard]] std::optional<std::int32_t> Dvm87Compare(
    IntrinsicContext& context, const VmObjectRef left, const VmObjectRef right,
    const VmObjectRef comparator) {
    if (comparator.IsValid()) {
        const auto result = Dvm87InvokeVirtual(
            context, comparator, "compare",
            "(Ljava/lang/Object;Ljava/lang/Object;)I",
            {VmValue::Ref(left), VmValue::Ref(right)});
        return result.has_value() ? std::optional(result->AsInt()) : std::nullopt;
    }
    if (!left.IsValid()) Dvm87Null("comparable == null");
    const auto result = Dvm87InvokeVirtual(
        context, left, "compareTo", "(Ljava/lang/Object;)I",
        {VmValue::Ref(right)});
    return result.has_value() ? std::optional(result->AsInt()) : std::nullopt;
}

[[nodiscard]] bool Dvm87StableSort(IntrinsicContext& context,
                                   std::vector<VmObjectRef>& values,
                                   const VmObjectRef comparator) {
    for (std::size_t index = 1; index < values.size(); ++index) {
        const auto value = values[index];
        auto cursor = index;
        while (cursor > 0) {
            const auto order = Dvm87Compare(
                context, value, values[cursor - 1], comparator);
            if (!order.has_value()) return false;
            if (*order >= 0) break;
            values[cursor] = values[cursor - 1];
            --cursor;
        }
        values[cursor] = value;
    }
    return true;
}

template <typename T>
[[nodiscard]] T Dvm87Primitive(const JavaObjectModel& model,
                               const VmObjectRef array,
                               const std::int32_t index) {
    const auto bits = model.GetPrimitiveElement(array, index);
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    } else if constexpr (std::is_same_v<T, double>) {
        return std::bit_cast<double>(bits);
    } else {
        return static_cast<T>(bits);
    }
}

template <typename T>
[[nodiscard]] std::uint64_t Dvm87Bits(const T value) {
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<std::uint32_t>(value);
    } else if constexpr (std::is_same_v<T, double>) {
        return std::bit_cast<std::uint64_t>(value);
    } else {
        return static_cast<std::uint64_t>(value);
    }
}

template <typename T>
[[nodiscard]] int Dvm87PrimitiveCompare(const T left, const T right) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(left)) return std::isnan(right) ? 0 : 1;
        if (std::isnan(right)) return -1;
        if (left == right && left == T{}) {
            return std::signbit(left) == std::signbit(right)
                       ? 0 : std::signbit(left) ? -1 : 1;
        }
    }
    return left < right ? -1 : left > right ? 1 : 0;
}

template <typename T>
void Dvm87AddPrimitiveArrayMethods(IntrinsicClassBuilder& builder,
                                   const std::string& descriptor) {
    builder.StaticMethod("sort", "([" + descriptor + ")V",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            auto& model = context.vm.Model();
            const auto length = static_cast<std::int32_t>(model.ArrayLength(array));
            std::vector<T> values;
            values.reserve(length);
            for (std::int32_t i = 0; i < length; ++i)
                values.push_back(Dvm87Primitive<T>(model, array, i));
            std::sort(values.begin(), values.end(), [](const T a, const T b) {
                return Dvm87PrimitiveCompare(a, b) < 0;
            });
            for (std::int32_t i = 0; i < length; ++i)
                model.SetPrimitiveElement(array, i, Dvm87Bits(values[i]));
            return VmValue::Void();
        });
    builder.StaticMethod("sort", "([" + descriptor + "II)V",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            const auto from = context.arguments[1].AsInt();
            const auto to = context.arguments[2].AsInt();
            auto& model = context.vm.Model();
            Dvm87CheckRange(model, array, from, to);
            std::vector<T> values;
            for (auto i = from; i < to; ++i)
                values.push_back(Dvm87Primitive<T>(model, array, i));
            std::sort(values.begin(), values.end(), [](const T a, const T b) {
                return Dvm87PrimitiveCompare(a, b) < 0;
            });
            for (std::size_t i = 0; i < values.size(); ++i)
                model.SetPrimitiveElement(array,
                    from + static_cast<std::int32_t>(i), Dvm87Bits(values[i]));
            return VmValue::Void();
        });
    builder.StaticMethod("binarySearch", "([" + descriptor + descriptor + ")I",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            auto& model = context.vm.Model();
            const auto key = [&]() -> T {
                if constexpr (std::is_same_v<T, float>) return context.arguments[1].AsFloat();
                else if constexpr (std::is_same_v<T, double>) return context.arguments[1].AsDouble();
                else if constexpr (sizeof(T) == 8) return static_cast<T>(context.arguments[1].AsLong());
                else return static_cast<T>(context.arguments[1].AsInt());
            }();
            std::int32_t low = 0;
            std::int32_t high = model.ArrayLength(array) - 1;
            while (low <= high) {
                const auto mid = low + ((high - low) >> 1);
                const auto order = Dvm87PrimitiveCompare(
                    Dvm87Primitive<T>(model, array, mid), key);
                if (order < 0) low = mid + 1;
                else if (order > 0) high = mid - 1;
                else return VmValue::Int(mid);
            }
            return VmValue::Int(-low - 1);
        });
    builder.StaticMethod("fill", "([" + descriptor + descriptor + ")V",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            const auto value = [&]() -> T {
                if constexpr (std::is_same_v<T, float>) return context.arguments[1].AsFloat();
                else if constexpr (std::is_same_v<T, double>) return context.arguments[1].AsDouble();
                else if constexpr (sizeof(T) == 8) return static_cast<T>(context.arguments[1].AsLong());
                else return static_cast<T>(context.arguments[1].AsInt());
            }();
            auto& model = context.vm.Model();
            for (JniSize i = 0; i < model.ArrayLength(array); ++i)
                model.SetPrimitiveElement(array, i, Dvm87Bits(value));
            return VmValue::Void();
        });
    builder.StaticMethod("equals", "([" + descriptor + "[" + descriptor + ")Z",
        [](IntrinsicContext& context) {
            const auto left = context.arguments[0].ref;
            const auto right = context.arguments[1].ref;
            if (left == right) return VmValue::Int(1);
            if (!left.IsValid() || !right.IsValid()) return VmValue::Int(0);
            auto& model = context.vm.Model();
            const auto length = model.ArrayLength(left);
            if (length != model.ArrayLength(right)) return VmValue::Int(0);
            for (JniSize i = 0; i < length; ++i) {
                if (Dvm87PrimitiveCompare(Dvm87Primitive<T>(model, left, i),
                                          Dvm87Primitive<T>(model, right, i)) != 0)
                    return VmValue::Int(0);
            }
            return VmValue::Int(1);
        });
}

IntrinsicClassDecl Dvm87DeclareArraysArrayList() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/Arrays$ArrayList;", "Ljava/util/AbstractList;",
        {"Ljava/util/RandomAccess;", "Ljava/io/Serializable;"});
    const auto array = builder.BoundInstanceField("array", "[Ljava/lang/Object;");
    builder.Constructor("([Ljava/lang/Object;)V", [array](IntrinsicContext& context) {
        IntrinsicCall call(context);
        call.SetRef(array, call.NonNullRef(0, "array"));
        return VmValue::Void();
    });
    builder.FinalMethod("size", "()I", [array](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return VmValue::Int(call.Vm().Model().ArrayLength(call.GetRef(array)));
    });
    builder.FinalMethod("get", "(I)Ljava/lang/Object;", [array](IntrinsicContext& context) {
        IntrinsicCall call(context);
        return VmValue::Ref(call.Vm().Model().GetObjectElement(
            call.GetRef(array), call.Int(0)));
    });
    builder.FinalMethod("set", "(ILjava/lang/Object;)Ljava/lang/Object;",
        [array](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto backing = call.GetRef(array);
            const auto index = call.Int(0);
            const auto old = call.Vm().Model().GetObjectElement(backing, index);
            call.Vm().Model().SetObjectElement(backing, index, call.Ref(1));
            return VmValue::Ref(old);
        });
    return std::move(builder).Build();
}

IntrinsicClassDecl Dvm87DeclareArrays() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Arrays;");
    builder.StaticMethod("asList", "([Ljava/lang/Object;)Ljava/util/List;",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            const auto list = context.vm.NewIntrinsicInstance("Ljava/util/Arrays$ArrayList;");
            const auto owner = context.vm.Linker().ResolveDescriptor("Ljava/util/Arrays$ArrayList;");
            const auto constructor = context.vm.Linker().FindDirectMethod(
                owner, "<init>", "([Ljava/lang/Object;)V");
            const std::array args{VmValue::Ref(list), VmValue::Ref(array)};
            const auto outcome = context.vm.Call(*constructor, args);
            if (outcome.exception.IsValid()) context.vm.SetPendingException(outcome.exception);
            return VmValue::Ref(list);
        });
    const auto object_sort = [](IntrinsicContext& context, const VmObjectRef comparator) {
        const auto array = context.arguments[0].ref;
        if (!array.IsValid()) Dvm87ArrayNull();
        auto& model = context.vm.Model();
        std::vector<VmObjectRef> values;
        for (JniSize i = 0; i < model.ArrayLength(array); ++i)
            values.push_back(model.GetObjectElement(array, i));
        if (!Dvm87StableSort(context, values, comparator)) return VmValue::Void();
        for (JniSize i = 0; i < static_cast<JniSize>(values.size()); ++i)
            model.SetObjectElement(array, i, values[i]);
        return VmValue::Void();
    };
    builder.StaticMethod("sort", "([Ljava/lang/Object;)V",
        [object_sort](IntrinsicContext& context) {
            return object_sort(context, VmObjectRef{0});
        });
    builder.StaticMethod("sort", "([Ljava/lang/Object;Ljava/util/Comparator;)V",
        [object_sort](IntrinsicContext& context) {
            return object_sort(context, context.arguments[1].ref);
        });
    builder.StaticMethod("binarySearch",
        "([Ljava/lang/Object;Ljava/lang/Object;Ljava/util/Comparator;)I",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            std::int32_t low = 0, high = context.vm.Model().ArrayLength(array) - 1;
            while (low <= high) {
                const auto mid = low + ((high - low) >> 1);
                const auto order = Dvm87Compare(
                    context, context.vm.Model().GetObjectElement(array, mid),
                    context.arguments[1].ref, context.arguments[2].ref);
                if (!order.has_value()) return VmValue::Int(0);
                if (*order < 0) low = mid + 1;
                else if (*order > 0) high = mid - 1;
                else return VmValue::Int(mid);
            }
            return VmValue::Int(-low - 1);
        });
    builder.StaticMethod("fill", "([Ljava/lang/Object;Ljava/lang/Object;)V",
        [](IntrinsicContext& context) {
            const auto array = context.arguments[0].ref;
            if (!array.IsValid()) Dvm87ArrayNull();
            auto& model = context.vm.Model();
            for (JniSize i = 0; i < model.ArrayLength(array); ++i)
                model.SetObjectElement(array, i, context.arguments[1].ref);
            return VmValue::Void();
        });
    builder.StaticMethod("equals", "([Ljava/lang/Object;[Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            const auto left = context.arguments[0].ref;
            const auto right = context.arguments[1].ref;
            if (left == right) return VmValue::Int(1);
            if (!left.IsValid() || !right.IsValid()) return VmValue::Int(0);
            auto& model = context.vm.Model();
            const auto length = model.ArrayLength(left);
            if (length != model.ArrayLength(right)) return VmValue::Int(0);
            for (JniSize i = 0; i < length; ++i) {
                const auto equal = Dvm87GuestEquals(context,
                    model.GetObjectElement(left, i), model.GetObjectElement(right, i));
                if (!equal.has_value() || !*equal) return VmValue::Int(0);
            }
            return VmValue::Int(1);
        });
    Dvm87AddPrimitiveArrayMethods<std::int8_t>(builder, "B");
    Dvm87AddPrimitiveArrayMethods<char16_t>(builder, "C");
    Dvm87AddPrimitiveArrayMethods<std::int16_t>(builder, "S");
    Dvm87AddPrimitiveArrayMethods<std::int32_t>(builder, "I");
    Dvm87AddPrimitiveArrayMethods<std::int64_t>(builder, "J");
    Dvm87AddPrimitiveArrayMethods<float>(builder, "F");
    Dvm87AddPrimitiveArrayMethods<double>(builder, "D");
    return std::move(builder).Build();
}

[[nodiscard]] std::optional<std::vector<VmObjectRef>> Dvm87ListValues(
    IntrinsicContext& context, const VmObjectRef list) {
    const auto size = Dvm87InvokeVirtual(context, list, "size", "()I");
    if (!size.has_value()) return std::nullopt;
    std::vector<VmObjectRef> result;
    for (std::int32_t index = 0; index < size->AsInt(); ++index) {
        const auto value = Dvm87InvokeVirtual(context, list, "get",
            "(I)Ljava/lang/Object;", {VmValue::Int(index)});
        if (!value.has_value()) return std::nullopt;
        result.push_back(value->ref);
    }
    return result;
}

bool Dvm87SetList(IntrinsicContext& context, const VmObjectRef list,
                  const std::span<const VmObjectRef> values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto result = Dvm87InvokeVirtual(context, list, "set",
            "(ILjava/lang/Object;)Ljava/lang/Object;",
            {VmValue::Int(static_cast<std::int32_t>(index)),
             VmValue::Ref(values[index])});
        if (!result.has_value()) return false;
    }
    return true;
}

IntrinsicClassDecl Dvm87DeclareCollections() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/util/Collections;");
    const auto sort = [](IntrinsicContext& context, const VmObjectRef comparator) {
        const auto list = context.arguments[0].ref;
        auto values = Dvm87ListValues(context, list);
        if (!values.has_value() || !Dvm87StableSort(context, *values, comparator))
            return VmValue::Void();
        Dvm87SetList(context, list, *values);
        return VmValue::Void();
    };
    builder.StaticMethod("sort", "(Ljava/util/List;)V",
        [sort](IntrinsicContext& context) {
            return sort(context, VmObjectRef{0});
        });
    builder.StaticMethod("sort", "(Ljava/util/List;Ljava/util/Comparator;)V",
        [sort](IntrinsicContext& context) { return sort(context, context.arguments[1].ref); });
    builder.StaticMethod("reverse", "(Ljava/util/List;)V",
        [](IntrinsicContext& context) {
            auto values = Dvm87ListValues(context, context.arguments[0].ref);
            if (!values.has_value()) return VmValue::Void();
            std::reverse(values->begin(), values->end());
            Dvm87SetList(context, context.arguments[0].ref, *values);
            return VmValue::Void();
        });
    builder.StaticMethod("swap", "(Ljava/util/List;II)V",
        [](IntrinsicContext& context) {
            auto values = Dvm87ListValues(context, context.arguments[0].ref);
            if (!values.has_value()) return VmValue::Void();
            const auto left = context.arguments[1].AsInt();
            const auto right = context.arguments[2].AsInt();
            if (left < 0 || right < 0 ||
                static_cast<std::size_t>(left) >= values->size() ||
                static_cast<std::size_t>(right) >= values->size())
                Dvm87BadIndex(std::max(left, right), values->size());
            std::swap((*values)[left], (*values)[right]);
            Dvm87SetList(context, context.arguments[0].ref, *values);
            return VmValue::Void();
        });
    builder.StaticMethod("fill", "(Ljava/util/List;Ljava/lang/Object;)V",
        [](IntrinsicContext& context) {
            auto values = Dvm87ListValues(context, context.arguments[0].ref);
            if (!values.has_value()) return VmValue::Void();
            std::fill(values->begin(), values->end(), context.arguments[1].ref);
            Dvm87SetList(context, context.arguments[0].ref, *values);
            return VmValue::Void();
        });
    builder.StaticMethod("frequency", "(Ljava/util/Collection;Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
            const auto collection = context.arguments[0].ref;
            if (!collection.IsValid()) Dvm87Null("collection == null");
            const auto list_class = context.vm.Linker().ResolveDescriptor(
                "Ljava/util/List;");
            const auto actual = context.vm.Model().ObjectClass(collection);
            const auto values = context.vm.Linker().IsAssignable(
                                    list_class, actual)
                ? Dvm87ListValues(context, collection)
                : Dvm87SnapshotCollection(context, collection);
            if (!values.has_value()) return VmValue::Int(0);
            std::int32_t count{};
            for (const auto value : *values) {
                const auto equal = Dvm87GuestEquals(
                    context, context.arguments[1].ref, value);
                if (!equal.has_value()) return VmValue::Int(0);
                if (*equal) ++count;
            }
            return VmValue::Int(count);
        });
    builder.StaticMethod("binarySearch",
        "(Ljava/util/List;Ljava/lang/Object;Ljava/util/Comparator;)I",
        [](IntrinsicContext& context) {
            const auto values = Dvm87ListValues(context, context.arguments[0].ref);
            if (!values.has_value()) return VmValue::Int(0);
            std::int32_t low = 0, high = static_cast<std::int32_t>(values->size()) - 1;
            while (low <= high) {
                const auto mid = low + ((high - low) >> 1);
                const auto order = Dvm87Compare(context, (*values)[mid],
                    context.arguments[1].ref, context.arguments[2].ref);
                if (!order.has_value()) return VmValue::Int(0);
                if (*order < 0) low = mid + 1;
                else if (*order > 0) high = mid - 1;
                else return VmValue::Int(mid);
            }
            return VmValue::Int(-low - 1);
        });
    return std::move(builder).Build();
}

[[nodiscard]] constexpr std::int64_t Dvm87DaysFromCivil(
    std::int32_t year, std::uint32_t month, std::uint32_t day) noexcept {
    year -= month <= 2U;
    const auto era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<std::uint32_t>(year - era * 400);
    const auto adjusted = month > 2U ? month - 3U : month + 9U;
    const auto doy = (153U * adjusted + 2U) / 5U + day - 1U;
    const auto doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097LL + static_cast<std::int64_t>(doe) - 719468LL;
}

struct Dvm87CivilDate final {
    std::int32_t year{};
    std::uint32_t month{};
    std::uint32_t day{};
};

[[nodiscard]] constexpr Dvm87CivilDate Dvm87CivilFromDays(
    std::int64_t days) noexcept {
    days += 719468LL;
    const auto era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    const auto doe = static_cast<std::uint32_t>(days - era * 146097LL);
    const auto yoe =
        (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    auto year = static_cast<std::int32_t>(yoe) +
                static_cast<std::int32_t>(era * 400LL);
    const auto doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const auto mp = (5U * doy + 2U) / 153U;
    const auto day = doy - (153U * mp + 2U) / 5U + 1U;
    const auto month = mp < 10U ? mp + 3U : mp - 9U;
    year += month <= 2U;
    return {year, month, day};
}

struct Dvm87TimeZoneFields final {
    IntrinsicFieldHandle id;
    IntrinsicFieldHandle raw_offset;
};

struct Dvm87TimeZoneDeclaration final {
    IntrinsicClassDecl declaration;
    Dvm87TimeZoneFields fields;
};

Dvm87TimeZoneDeclaration Dvm87DeclareTimeZone() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/TimeZone;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/Cloneable;"});
    const Dvm87TimeZoneFields fields{
        builder.BoundInstanceField("id", "Ljava/lang/String;"),
        builder.BoundInstanceField("rawOffset", "I")};
    const auto make_zone = [fields](IntrinsicContext& context,
                                    const std::string& requested) {
        std::int32_t offset{};
        bool valid = requested == "GMT" || requested == "UTC";
        if (requested.size() == 9U && requested.starts_with("GMT") &&
            (requested[3] == '+' || requested[3] == '-') &&
            requested[6] == ':') {
            int hours{}, minutes{};
            const auto hour_result = std::from_chars(
                requested.data() + 4, requested.data() + 6, hours);
            const auto minute_result = std::from_chars(
                requested.data() + 7, requested.data() + 9, minutes);
            valid = hour_result.ec == std::errc{} &&
                    minute_result.ec == std::errc{} &&
                    hours <= 23 && minutes <= 59;
            if (valid) {
                offset = (hours * 60 + minutes) * 60 * 1000;
                if (requested[3] == '-') offset = -offset;
            }
        }
        IntrinsicCall call(context);
        const auto zone = call.Vm().NewIntrinsicInstance(
            "Ljava/util/SimpleTimeZone;");
        call.SetRef(fields.id, zone,
                    call.Vm().NewStringUtf8(valid ? requested : "GMT"));
        call.SetInt(fields.raw_offset, zone, valid ? offset : 0);
        return zone;
    };
    builder.Constructor("()V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        call.SetRef(fields.id, call.Vm().NewStringUtf8("GMT"));
        call.SetInt(fields.raw_offset, 0);
        return VmValue::Void();
    }, 0x0004U);
    builder.StaticMethod("getDefault", "()Ljava/util/TimeZone;",
        [make_zone](IntrinsicContext& context) {
            return VmValue::Ref(make_zone(context, "GMT"));
        });
    builder.StaticMethod("getTimeZone",
        "(Ljava/lang/String;)Ljava/util/TimeZone;",
        [make_zone](IntrinsicContext& context) {
            const auto id = IntrinsicCall(context).NonNullRef(0, "id");
            return VmValue::Ref(make_zone(context, context.vm.StringUtf8(id)));
        });
    builder.FinalMethod("getID", "()Ljava/lang/String;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.id));
        });
    builder.FinalMethod("setID", "(Ljava/lang/String;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetRef(fields.id, call.NonNullRef(0, "id"));
            return VmValue::Void();
        });
    builder.VirtualMethod("getRawOffset", "()I",
        [fields](IntrinsicContext& context) {
            return VmValue::Int(
                IntrinsicCall(context).GetInt(fields.raw_offset));
        });
    builder.VirtualMethod("setRawOffset", "(I)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetInt(fields.raw_offset, call.Int(0));
            return VmValue::Void();
        });
    builder.FinalMethod("getOffset", "(J)I",
        [fields](IntrinsicContext& context) {
            return VmValue::Int(
                IntrinsicCall(context).GetInt(fields.raw_offset));
        });
    builder.VirtualMethod("useDaylightTime", "()Z",
        [](IntrinsicContext&) { return VmValue::Int(0); });
    builder.FinalMethod("inDaylightTime", "(Ljava/util/Date;)Z",
        [](IntrinsicContext& context) {
            if (!context.arguments[0].ref.IsValid()) Dvm87Null("date == null");
            return VmValue::Int(0);
        });
    return {std::move(builder).Build(), fields};
}

IntrinsicClassDecl Dvm87DeclareSimpleTimeZone(
    const Dvm87TimeZoneFields fields) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/SimpleTimeZone;", "Ljava/util/TimeZone;");
    // TimeZone factories initialize inherited state through bound fields.
    builder.Constructor("(ILjava/lang/String;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetInt(fields.raw_offset, call.Int(0));
            call.SetRef(fields.id, call.NonNullRef(1, "id"));
            return VmValue::Void();
        });
    return std::move(builder).Build();
}

struct Dvm87CalendarFields final {
    IntrinsicFieldHandle millis;
    IntrinsicFieldHandle zone;
    IntrinsicFieldHandle lenient;
};

[[nodiscard]] std::int32_t Dvm87ZoneOffset(
    IntrinsicContext& context, const Dvm87CalendarFields& fields) {
    IntrinsicCall call(context);
    const auto zone = call.GetRef(fields.zone);
    if (!zone.IsValid()) return 0;
    const auto result = Dvm87InvokeVirtual(
        context, zone, "getRawOffset", "()I");
    return result.has_value() ? result->AsInt() : 0;
}

struct Dvm87CalendarDeclaration final {
    IntrinsicClassDecl declaration;
    Dvm87CalendarFields fields;
};

Dvm87CalendarDeclaration Dvm87DeclareCalendar(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/Calendar;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/Cloneable;"}, 0x0401U);
    const Dvm87CalendarFields fields{
        builder.BoundInstanceField("time", "J"),
        builder.BoundInstanceField("zone", "Ljava/util/TimeZone;"),
        builder.BoundInstanceField("lenient", "Z")};
    constexpr std::array constants{
        std::pair{"ERA", 0}, std::pair{"YEAR", 1},
        std::pair{"MONTH", 2}, std::pair{"DATE", 5},
        std::pair{"DAY_OF_MONTH", 5}, std::pair{"DAY_OF_WEEK", 7},
        std::pair{"AM_PM", 9}, std::pair{"HOUR", 10},
        std::pair{"HOUR_OF_DAY", 11}, std::pair{"MINUTE", 12},
        std::pair{"SECOND", 13}, std::pair{"MILLISECOND", 14},
        std::pair{"ZONE_OFFSET", 15}, std::pair{"DST_OFFSET", 16},
        std::pair{"JANUARY", 0}, std::pair{"FEBRUARY", 1},
        std::pair{"MARCH", 2}, std::pair{"APRIL", 3},
        std::pair{"MAY", 4}, std::pair{"JUNE", 5},
        std::pair{"JULY", 6}, std::pair{"AUGUST", 7},
        std::pair{"SEPTEMBER", 8}, std::pair{"OCTOBER", 9},
        std::pair{"NOVEMBER", 10}, std::pair{"DECEMBER", 11}};
    for (const auto& [name, value] : constants)
        builder.ConstantInt(name, "I", value);
    builder.Constructor("(Ljava/util/TimeZone;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetLong(fields.millis, 0);
            call.SetRef(fields.zone, call.NonNullRef(0, "zone"));
            call.SetInt(fields.lenient, 1);
            return VmValue::Void();
        }, 0x0004U);
    builder.StaticMethod("getInstance", "()Ljava/util/Calendar;",
        [fields, now = services.current_time_millis](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto zone_class =
                call.Vm().Linker().ResolveDescriptor("Ljava/util/TimeZone;");
            const auto get_default = call.Vm().Linker().FindDirectMethod(
                zone_class, "getDefault", "()Ljava/util/TimeZone;");
            const auto zone = call.Vm().Call(*get_default, {});
            const auto calendar = call.Vm().NewIntrinsicInstance(
                "Ljava/util/GregorianCalendar;");
            call.SetRef(fields.zone, calendar, zone.value.ref);
            call.SetLong(fields.millis, calendar, now ? now() : 0);
            call.SetInt(fields.lenient, calendar, 1);
            return VmValue::Ref(calendar);
        });
    builder.FinalMethod("getTimeInMillis", "()J",
        [fields](IntrinsicContext& context) {
            return VmValue::Long(IntrinsicCall(context).GetLong(fields.millis));
        });
    builder.FinalMethod("setTimeInMillis", "(J)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetLong(fields.millis, call.Long(0));
            return VmValue::Void();
        });
    builder.FinalMethod("getTimeZone", "()Ljava/util/TimeZone;",
        [fields](IntrinsicContext& context) {
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.zone));
        });
    builder.FinalMethod("setTimeZone", "(Ljava/util/TimeZone;)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetRef(fields.zone, call.NonNullRef(0, "zone"));
            return VmValue::Void();
        });
    builder.FinalMethod("setLenient", "(Z)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            call.SetInt(fields.lenient, call.Int(0));
            return VmValue::Void();
        });
    builder.FinalMethod("isLenient", "()Z",
        [fields](IntrinsicContext& context) {
            return VmValue::Int(IntrinsicCall(context).GetInt(fields.lenient));
        });
    builder.VirtualMethod("get", "(I)I",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto field = call.Int(0);
            const auto local = call.GetLong(fields.millis) +
                               Dvm87ZoneOffset(context, fields);
            auto days = local / 86400000LL;
            auto day_millis = local % 86400000LL;
            if (day_millis < 0) { day_millis += 86400000LL; --days; }
            const auto civil = Dvm87CivilFromDays(days);
            switch (field) {
                case 0: return VmValue::Int(civil.year <= 0 ? 0 : 1);
                case 1: return VmValue::Int(civil.year <= 0 ? 1 - civil.year : civil.year);
                case 2: return VmValue::Int(static_cast<std::int32_t>(civil.month) - 1);
                case 5: return VmValue::Int(civil.day);
                case 7: return VmValue::Int(
                    static_cast<std::int32_t>((days + 4LL) % 7LL + 7LL) % 7 + 1);
                case 9: return VmValue::Int(day_millis >= 43200000LL ? 1 : 0);
                case 10: return VmValue::Int((day_millis / 3600000LL) % 12);
                case 11: return VmValue::Int(static_cast<std::int32_t>(day_millis / 3600000LL));
                case 12: return VmValue::Int(static_cast<std::int32_t>((day_millis / 60000LL) % 60));
                case 13: return VmValue::Int(static_cast<std::int32_t>((day_millis / 1000LL) % 60));
                case 14: return VmValue::Int(static_cast<std::int32_t>(day_millis % 1000LL));
                case 15: return VmValue::Int(Dvm87ZoneOffset(context, fields));
                case 16: return VmValue::Int(0);
                default:
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unsupported Calendar field"};
            }
        });
    builder.VirtualMethod("set", "(IIIIII)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto millis = Dvm87DaysFromCivil(
                call.Int(0), static_cast<std::uint32_t>(call.Int(1) + 1),
                static_cast<std::uint32_t>(call.Int(2))) * 86400000LL +
                call.Int(3) * 3600000LL + call.Int(4) * 60000LL +
                call.Int(5) * 1000LL - Dvm87ZoneOffset(context, fields);
            call.SetLong(fields.millis, millis);
            return VmValue::Void();
        });
    builder.VirtualMethod("clear", "()V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall(context).SetLong(fields.millis, 0);
            return VmValue::Void();
        });
    builder.VirtualMethod("add", "(II)V",
        [fields](IntrinsicContext& context) {
            IntrinsicCall call(context);
            std::int64_t unit{};
            switch (call.Int(0)) {
                case 5: unit = 86400000LL; break;
                case 10: case 11: unit = 3600000LL; break;
                case 12: unit = 60000LL; break;
                case 13: unit = 1000LL; break;
                case 14: unit = 1; break;
                default:
                    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                      "Calendar.add field is not provided"};
            }
            call.SetLong(fields.millis,
                         call.GetLong(fields.millis) + unit * call.Int(1));
            return VmValue::Void();
        });
    return {std::move(builder).Build(), fields};
}

IntrinsicClassDecl Dvm87DeclareGregorianCalendar(
    const Dvm87CalendarFields fields,
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/util/GregorianCalendar;", "Ljava/util/Calendar;");
    const auto initialize = [fields, now = services.current_time_millis](
                                IntrinsicContext& context,
                                const VmObjectRef requested_zone) {
        IntrinsicCall call(context);
        auto zone = requested_zone;
        if (!zone.IsValid()) {
            const auto zone_class = call.Vm().Linker().ResolveDescriptor(
                "Ljava/util/TimeZone;");
            const auto get_default = call.Vm().Linker().FindDirectMethod(
                zone_class, "getDefault", "()Ljava/util/TimeZone;");
            const auto outcome = call.Vm().Call(*get_default, {});
            if (outcome.exception.IsValid()) {
                call.Vm().SetPendingException(outcome.exception);
                return VmValue::Void();
            }
            zone = outcome.value.ref;
        }
        call.SetRef(fields.zone, zone);
        call.SetLong(fields.millis, now ? now() : 0);
        call.SetInt(fields.lenient, 1);
        return VmValue::Void();
    };
    builder.Constructor("()V", [initialize](IntrinsicContext& context) {
        return initialize(context, VmObjectRef{0});
    });
    builder.Constructor("(Ljava/util/TimeZone;)V",
        [initialize](IntrinsicContext& context) {
            return initialize(context,
                              IntrinsicCall(context).NonNullRef(0, "zone"));
        });
    return std::move(builder).Build();
}

}  // namespace

void AppendJavaUtilAlgorithms(std::vector<IntrinsicClassDecl>& catalog,
                              const CoreIntrinsicServices& services) {
    catalog.push_back(Dvm87DeclareArraysArrayList());
    catalog.push_back(Dvm87DeclareArrays());
    catalog.push_back(Dvm87DeclareCollections());
    auto timezone = Dvm87DeclareTimeZone();
    catalog.push_back(std::move(timezone.declaration));
    catalog.push_back(Dvm87DeclareSimpleTimeZone(timezone.fields));
    auto calendar = Dvm87DeclareCalendar(services);
    catalog.push_back(std::move(calendar.declaration));
    catalog.push_back(Dvm87DeclareGregorianCalendar(
        calendar.fields, services));
}

}  // namespace ogplay::runtime::dexvm::intrinsics
