#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/runtime/dexvm/collection_runtime.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
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

void AddMapMethods(IntrinsicClassBuilder &builder, const bool reject_null,
                   const bool linked, const std::uint32_t flags = kPublic) {
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
  AddSequenceMethods(builder, synchronized ? kPublic | kSynchronized : kPublic,
                     reject_null);
  if (deque)
    AddDequeMethods(builder, reject_null);
  if (vector_methods) {
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
                synchronized ? kPublic | kSynchronized : kPublic);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareSetClass(std::string descriptor,
                                   std::string superclass,
                                   std::vector<std::string> interfaces) {
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
  AddSetMethods(builder);
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
  builder.VirtualMethod(
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
  builder.VirtualMethod("hashCode", "()I", [](IntrinsicContext &context) {
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
