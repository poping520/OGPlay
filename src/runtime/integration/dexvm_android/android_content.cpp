// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from android_content_BroadcastReceiver.cpp ----
// BroadcastReceiver is inert: the session never dispatches broadcasts, so
// the base onReceive stays a recorded no-op games may override.

#include "catalog.h"

#include "ogplay/core/encoding.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_BroadcastReceiver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/BroadcastReceiver;", "Ljava/lang/Object;");
    builder.Constructor("()V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onReceive",
        "(Landroid/content/Context;Landroid/content/Intent;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

// ---- DVM-88: ContentValues, Cursor and bounded SQLite-on-VFS ------------

#include <charconv>
#include <cctype>
#include <cstring>
#include <sstream>

namespace ogplay::runtime::android_intrinsics {
namespace {

using DbValue = DexVmAndroidContext::DatabaseValue;
using DbRow = DexVmAndroidContext::DatabaseRow;

[[noreturn]] void DbThrow(const std::string& message) {
    throw dx::VmJavaThrow{"Landroid/database/SQLException;", message};
}

std::string DbString(dx::IntrinsicContext& call, const dx::VmObjectRef ref) {
    if (!ref.IsValid())
        throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;", "null string"};
    return call.vm.StringUtf8(ref);
}

std::string Hex(const std::span<const std::byte> bytes) {
    return core::EncodeHex(bytes, core::HexCase::upper);
}

std::vector<std::byte> Unhex(const std::string_view text) {
    auto result = core::DecodeHex(text);
    if (!result.has_value()) DbThrow("damaged database hex value");
    return std::move(*result);
}

std::string HexText(const std::string_view text) {
    return Hex(std::as_bytes(std::span(text)));
}

std::string DbPath(const Context& context, const std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos || name == "." ||
        name == "..") {
        throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                              "invalid database name"};
    }
    return "/data/data/" + context->package_name + "/databases/" +
           std::string(name);
}

void EnsureDbDirectory(const Context& context) {
    if (context->vfs == nullptr) DbThrow("guest VFS is unavailable");
    const auto data = "/data/data/" + context->package_name;
    const std::array paths{data, data + "/databases"};
    for (const auto& path : paths) {
        try {
            if (context->vfs->Stat(path).is_directory) continue;
            DbThrow("database path is not a directory");
        } catch (const VfsError&) {
            try { context->vfs->CreateDirectory(path); }
            catch (const VfsError& error) {
                DbThrow("cannot create database directory: " +
                        std::to_string(error.ErrorNumber()));
            }
        }
    }
}

std::string EncodeValue(const DbValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return "N";
    if (const auto* integer = std::get_if<std::int64_t>(&value))
        return "I" + std::to_string(*integer);
    if (const auto* real = std::get_if<double>(&value)) {
        std::ostringstream out; out.precision(17); out << *real;
        return "R" + out.str();
    }
    if (const auto* text = std::get_if<std::string>(&value))
        return "T" + HexText(*text);
    return "B" + Hex(std::get<std::vector<std::byte>>(value));
}

DbValue DecodeValue(const std::string_view value) {
    if (value.empty()) DbThrow("damaged database value");
    if (value[0] == 'N') return std::monostate{};
    if (value[0] == 'T') {
        const auto bytes = Unhex(value.substr(1));
        return std::string(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
    }
    if (value[0] == 'B') return Unhex(value.substr(1));
    if (value[0] == 'I') {
        std::int64_t number{};
        const auto text = value.substr(1);
        const auto [end, error] = std::from_chars(text.data(),
                                                  text.data() + text.size(),
                                                  number);
        if (error != std::errc{} || end != text.data() + text.size())
            DbThrow("damaged database integer");
        return number;
    }
    if (value[0] == 'R') {
        try { return std::stod(std::string(value.substr(1))); }
        catch (...) { DbThrow("damaged database real"); }
    }
    DbThrow("unknown database value kind");
}

void PersistDatabase(const Context& context,
                     const DexVmAndroidContext::DatabaseState& database) {
    EnsureDbDirectory(context);
    std::string image = "OGDB1\nV\t" + std::to_string(database.version) + "\n";
    std::vector<std::string> tables;
    for (const auto& entry : database.tables) tables.push_back(entry.first);
    std::sort(tables.begin(), tables.end());
    for (const auto& name : tables) {
        const auto& table = database.tables.at(name);
        image += "T\t" + HexText(name) + "\t" +
                 std::to_string(table.next_row_id) + "\n";
        for (const auto& row : table.rows) {
            image += "R";
            std::vector<std::string> columns;
            for (const auto& entry : row) columns.push_back(entry.first);
            std::sort(columns.begin(), columns.end());
            for (const auto& column : columns) {
                image += "\t" + HexText(column) + "=" +
                         EncodeValue(row.at(column));
            }
            image += "\n";
        }
    }
    std::optional<std::int32_t> descriptor;
    try {
        descriptor = context->vfs->Open(
            database.path, {.write = true, .create = true, .truncate = true});
        const auto bytes = std::as_bytes(std::span(image));
        std::size_t offset{};
        while (offset < bytes.size())
            offset += context->vfs->Write(*descriptor, bytes.subspan(offset));
        context->vfs->Flush(*descriptor);
        context->vfs->Close(*descriptor);
    } catch (const VfsError& error) {
        if (descriptor.has_value()) {
            try { context->vfs->Close(*descriptor); } catch (...) {}
        }
        DbThrow("database persist failed: " +
                std::to_string(error.ErrorNumber()));
    }
}

void LoadDatabase(const Context& context,
                  DexVmAndroidContext::DatabaseState& database) {
    if (context->vfs == nullptr) DbThrow("guest VFS is unavailable");
    VfsFileInfo info;
    try {
        info = context->vfs->Stat(database.path);
    } catch (const VfsError& error) {
        if (error.ErrorNumber() == 2) return;
        DbThrow("database stat failed: " +
                std::to_string(error.ErrorNumber()));
    }
    std::optional<std::int32_t> descriptor;
    try {
        descriptor = context->vfs->Open(database.path, {.read = true});
        std::vector<std::byte> bytes(info.size);
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto read = context->vfs->Read(
                *descriptor, std::span(bytes).subspan(offset));
            if (read == 0U) break;
            offset += read;
        }
        context->vfs->Close(*descriptor);
        const std::string image(reinterpret_cast<const char*>(bytes.data()),
                                offset);
        if (!image.starts_with("OGDB1\n")) DbThrow("invalid database image");
        DexVmAndroidContext::DatabaseTable* table{};
        std::istringstream lines(image.substr(6));
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            std::vector<std::string> fields;
            std::size_t start{};
            while (true) {
                const auto split = line.find('\t', start);
                fields.push_back(line.substr(start, split - start));
                if (split == std::string::npos) break;
                start = split + 1U;
            }
            if (fields[0] == "V" && fields.size() == 2U) {
                std::int32_t version{};
                const auto text = std::string_view(fields[1]);
                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), version);
                if (error != std::errc{} || end != text.data() + text.size() ||
                    version < 0) {
                    DbThrow("damaged database version");
                }
                database.version = version;
            } else if (fields[0] == "T" && fields.size() == 3U) {
                const auto name_bytes = Unhex(fields[1]);
                const std::string name(
                    reinterpret_cast<const char*>(name_bytes.data()),
                    name_bytes.size());
                table = &database.tables[name];
                const auto text = std::string_view(fields[2]);
                const auto [end, error] = std::from_chars(
                    text.data(), text.data() + text.size(), table->next_row_id);
                if (error != std::errc{} || end != text.data() + text.size() ||
                    table->next_row_id < 1) {
                    DbThrow("damaged database row id");
                }
            } else if (fields[0] == "R" && table != nullptr) {
                DbRow row;
                for (std::size_t index = 1; index < fields.size(); ++index) {
                    const auto equal = fields[index].find('=');
                    if (equal == std::string::npos) DbThrow("damaged database row");
                    const auto name_bytes = Unhex(fields[index].substr(0, equal));
                    const std::string name(
                        reinterpret_cast<const char*>(name_bytes.data()),
                        name_bytes.size());
                    row[name] = DecodeValue(fields[index].substr(equal + 1U));
                    if (std::find(table->columns.begin(), table->columns.end(),
                                  name) == table->columns.end())
                        table->columns.push_back(name);
                }
                table->rows.push_back(std::move(row));
            } else {
                DbThrow("damaged database record");
            }
        }
    } catch (const VfsError& error) {
        if (descriptor.has_value()) {
            try { context->vfs->Close(*descriptor); } catch (...) {}
        }
        DbThrow("database load failed: " +
                std::to_string(error.ErrorNumber()));
    }
}

DexVmAndroidContext::DatabaseState& RequireDb(
    const Context& context, const dx::VmObjectRef owner) {
    const auto found = context->databases.find(owner.Value());
    if (found == context->databases.end() || !found->second.open)
        DbThrow("database is closed or unavailable");
    return found->second;
}

DexVmAndroidContext::CursorState& RequireCursor(
    const Context& context, const dx::VmObjectRef owner) {
    const auto found = context->database_cursors.find(owner.Value());
    if (found == context->database_cursors.end() || found->second.closed)
        DbThrow("cursor is closed or unavailable");
    return found->second;
}

DbValue ObjectValue(dx::IntrinsicContext& call, const dx::VmObjectRef value,
                    const std::string_view descriptor) {
    if (!value.IsValid()) return std::monostate{};
    if (descriptor == "Ljava/lang/String;") return call.vm.StringUtf8(value);
    if (descriptor == "[B") return call.vm.Model().ReadByteRegion(
        value, 0, call.vm.Model().ArrayLength(value));
    const auto slots = call.vm.Model().InstanceSlots(value);
    if (slots.empty()) DbThrow("boxed ContentValues value has no slot");
    std::uint64_t bits = slots[0].bits;
    if (descriptor == "Ljava/lang/Long;" && slots.size() > 1U)
        bits |= static_cast<std::uint64_t>(slots[1].bits) << 32U;
    return static_cast<std::int64_t>(bits);
}

std::string ValueString(const DbValue& value) {
    if (std::holds_alternative<std::monostate>(value)) return {};
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* integer = std::get_if<std::int64_t>(&value))
        return std::to_string(*integer);
    if (const auto* real = std::get_if<double>(&value))
        return std::to_string(*real);
    return {};
}

std::vector<std::string> StringArray(dx::IntrinsicContext& call,
                                     const dx::VmObjectRef array) {
    std::vector<std::string> result;
    if (!array.IsValid()) return result;
    const auto length = call.vm.Model().ArrayLength(array);
    result.reserve(length);
    for (std::int32_t index = 0; index < length; ++index) {
        const auto value = call.vm.Model().GetObjectElement(array, index);
        result.push_back(value.IsValid() ? call.vm.StringUtf8(value) : "");
    }
    return result;
}

bool RowMatches(const DbRow& row, std::string selection,
                const std::vector<std::string>& args) {
    if (selection.empty()) return true;
    selection.erase(std::remove_if(selection.begin(), selection.end(),
        [](const unsigned char ch) { return std::isspace(ch) != 0; }),
        selection.end());
    const auto equal = selection.find('=');
    if (equal == std::string::npos) DbThrow("only column=? selection is supported");
    const auto column = selection.substr(0, equal);
    auto expected = selection.substr(equal + 1U);
    if (expected == "?") {
        if (args.empty()) DbThrow("selection argument is missing");
        expected = args.front();
    } else if (expected.size() >= 2U && expected.front() == '\'' &&
               expected.back() == '\'') {
        expected = expected.substr(1, expected.size() - 2U);
    }
    const auto found = row.find(column);
    return found != row.end() && ValueString(found->second) == expected;
}

dx::VmObjectRef OpenDatabase(dx::IntrinsicContext& call,
                             const Context& context,
                             const std::string& path) {
    if (const auto found = context->database_by_path.find(path);
        found != context->database_by_path.end()) {
        auto& database = context->databases.at(found->second);
        database.open = true;
        return dx::VmObjectRef(found->second);
    }
    const auto object = call.vm.NewIntrinsicInstance(
        "Landroid/database/sqlite/SQLiteDatabase;");
    DexVmAndroidContext::DatabaseState state;
    state.path = path;
    LoadDatabase(context, state);
    context->databases.emplace(object.Value(), std::move(state));
    context->database_by_path.emplace(path, object.Value());
    return object;
}

}  // namespace

Decl Declare_android_content_ContentValues(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/ContentValues;", "Ljava/lang/Object;",
        {"Landroid/os/Parcelable;"});
    builder.Constructor("()V", [context](dx::IntrinsicContext& call) {
        context->content_values[call.receiver.Value()] = {};
        return dx::VmValue::Void();
    });
    builder.Constructor("(I)V", [context](dx::IntrinsicContext& call) {
        if (call.arguments[0].AsInt() < 0)
            throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "negative ContentValues size"};
        context->content_values[call.receiver.Value()] = {};
        return dx::VmValue::Void();
    });
    const auto put = [&](const char* descriptor) {
        builder.FinalMethod("put", std::string("(Ljava/lang/String;") +
            descriptor + ")V", [context, descriptor](dx::IntrinsicContext& call) {
            context->content_values[call.receiver.Value()][
                DbString(call, call.arguments[0].ref)] =
                ObjectValue(call, call.arguments[1].ref, descriptor);
            return dx::VmValue::Void();
        });
    };
    put("Ljava/lang/String;"); put("Ljava/lang/Integer;");
    put("Ljava/lang/Long;"); put("[B");
    builder.FinalMethod("putNull", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->content_values[call.receiver.Value()][
                DbString(call, call.arguments[0].ref)] = std::monostate{};
            return dx::VmValue::Void();
        });
    builder.FinalMethod("size", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            context->content_values[call.receiver.Value()].size()));
    });
    builder.FinalMethod("clear", "()V", [context](dx::IntrinsicContext& call) {
        context->content_values[call.receiver.Value()].clear();
        return dx::VmValue::Void();
    });
    builder.FinalMethod("containsKey", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(context->content_values[call.receiver.Value()]
                .contains(DbString(call, call.arguments[0].ref)) ? 1 : 0);
        });
    builder.FinalMethod("getAsString",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto& values = context->content_values[call.receiver.Value()];
            const auto found = values.find(DbString(call, call.arguments[0].ref));
            if (found == values.end() ||
                std::holds_alternative<std::monostate>(found->second))
                return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(call.vm.NewStringUtf8(
                ValueString(found->second)));
        });
    return std::move(builder).Build();
}

Decl Declare_android_database_Cursor(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Interface(
        "Landroid/database/Cursor;", {"Ljava/io/Closeable;"});
    return std::move(builder).Build();
}

Decl Declare_android_database_CursorImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/database/CursorImpl;", "Ljava/lang/Object;",
        {"Landroid/database/Cursor;"});
    builder.FinalMethod("getCount", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            RequireCursor(context, call.receiver).rows.size()));
    });
    builder.FinalMethod("getColumnCount", "()I", [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            RequireCursor(context, call.receiver).columns.size()));
    });
    builder.FinalMethod("getColumnIndex", "(Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto& columns = RequireCursor(context, call.receiver).columns;
            const auto found = std::find(columns.begin(), columns.end(),
                DbString(call, call.arguments[0].ref));
            return dx::VmValue::Int(found == columns.end() ? -1 :
                static_cast<std::int32_t>(found - columns.begin()));
        });
    builder.FinalMethod("moveToPosition", "(I)Z", [context](dx::IntrinsicContext& call) {
        auto& cursor = RequireCursor(context, call.receiver);
        const auto position = call.arguments[0].AsInt();
        cursor.position = position;
        return dx::VmValue::Int(position >= 0 &&
            static_cast<std::size_t>(position) < cursor.rows.size() ? 1 : 0);
    });
    builder.FinalMethod("moveToFirst", "()Z", [context](dx::IntrinsicContext& call) {
        auto& cursor = RequireCursor(context, call.receiver);
        cursor.position = 0;
        return dx::VmValue::Int(cursor.rows.empty() ? 0 : 1);
    });
    builder.FinalMethod("moveToNext", "()Z", [context](dx::IntrinsicContext& call) {
        auto& cursor = RequireCursor(context, call.receiver);
        ++cursor.position;
        return dx::VmValue::Int(cursor.position >= 0 &&
            static_cast<std::size_t>(cursor.position) < cursor.rows.size() ? 1 : 0);
    });
    const auto value = [context](dx::IntrinsicContext& call) -> const DbValue& {
        auto& cursor = RequireCursor(context, call.receiver);
        const auto column = call.arguments[0].AsInt();
        if (cursor.position < 0 ||
            static_cast<std::size_t>(cursor.position) >= cursor.rows.size() ||
            column < 0 || static_cast<std::size_t>(column) >= cursor.columns.size())
            throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                  "cursor position or column is invalid"};
        const auto found = cursor.rows[cursor.position].find(cursor.columns[column]);
        static const DbValue null{};
        return found == cursor.rows[cursor.position].end() ? null : found->second;
    };
    builder.FinalMethod("getString", "(I)Ljava/lang/String;",
        [value](dx::IntrinsicContext& call) {
            const auto& item = value(call);
            if (std::holds_alternative<std::monostate>(item))
                return dx::VmValue::Ref(dx::VmObjectRef{});
            return dx::VmValue::Ref(call.vm.NewStringUtf8(ValueString(item)));
        });
    builder.FinalMethod("getInt", "(I)I", [value](dx::IntrinsicContext& call) {
        const auto& item = value(call);
        if (const auto* number = std::get_if<std::int64_t>(&item))
            return dx::VmValue::Int(static_cast<std::int32_t>(*number));
        try { return dx::VmValue::Int(std::stoi(ValueString(item))); }
        catch (...) { return dx::VmValue::Int(0); }
    });
    builder.FinalMethod("getLong", "(I)J", [value](dx::IntrinsicContext& call) {
        const auto& item = value(call);
        if (const auto* number = std::get_if<std::int64_t>(&item))
            return dx::VmValue::Long(*number);
        try { return dx::VmValue::Long(std::stoll(ValueString(item))); }
        catch (...) { return dx::VmValue::Long(0); }
    });
    builder.FinalMethod("isNull", "(I)Z", [value](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(
            std::holds_alternative<std::monostate>(value(call)) ? 1 : 0);
    });
    builder.FinalMethod("close", "()V", [context](dx::IntrinsicContext& call) {
        RequireCursor(context, call.receiver).closed = true;
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteDatabase_CursorFactory(const Context&) {
    return std::move(dx::IntrinsicClassBuilder::Interface(
        "Landroid/database/sqlite/SQLiteDatabase$CursorFactory;")).Build();
}

Decl Declare_android_database_SQLiteException(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/database/SQLException;", "Ljava/lang/RuntimeException;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext& call) {
        call.vm.SetThrowableMessage(call.receiver, call.arguments[0].ref);
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteDatabase(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/database/sqlite/SQLiteDatabase;", "Ljava/lang/Object;");
    builder.StaticMethod("openOrCreateDatabase",
        "(Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(OpenDatabase(
                call, context, DbString(call, call.arguments[0].ref)));
        });
    builder.FinalMethod("isOpen", "()Z", [context](dx::IntrinsicContext& call) {
        const auto found = context->databases.find(call.receiver.Value());
        return dx::VmValue::Int(found != context->databases.end() &&
                               found->second.open ? 1 : 0);
    });
    builder.FinalMethod("getPath", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewStringUtf8(
                RequireDb(context, call.receiver).path));
        });
    builder.FinalMethod("close", "()V", [context](dx::IntrinsicContext& call) {
        auto& database = RequireDb(context, call.receiver);
        PersistDatabase(context, database);
        database.open = false;
        return dx::VmValue::Void();
    });
    builder.FinalMethod("execSQL", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            auto& database = RequireDb(context, call.receiver);
            auto sql = DbString(call, call.arguments[0].ref);
            const auto upper = [&] { auto out = sql; std::transform(out.begin(), out.end(),
                out.begin(), [](const unsigned char ch) { return std::toupper(ch); }); return out; }();
            if (upper.starts_with("CREATE TABLE")) {
                auto start = sql.find_first_not_of(" \t", 12);
                if (upper.find("IF NOT EXISTS", start) == start)
                    start = sql.find_first_not_of(" \t", start + 13U);
                const auto paren = sql.find('(', start);
                if (start == std::string::npos || paren == std::string::npos)
                    DbThrow("unsupported CREATE TABLE statement");
                auto name = sql.substr(start, paren - start);
                while (!name.empty() && std::isspace(
                    static_cast<unsigned char>(name.back()))) name.pop_back();
                auto& table = database.tables[name];
                const auto end = sql.rfind(')');
                if (end == std::string::npos) DbThrow("malformed CREATE TABLE");
                std::istringstream columns(sql.substr(paren + 1U, end - paren - 1U));
                std::string definition;
                while (std::getline(columns, definition, ',')) {
                    std::istringstream tokens(definition);
                    std::string column; tokens >> column;
                    if (!column.empty() && std::find(table.columns.begin(),
                        table.columns.end(), column) == table.columns.end())
                        table.columns.push_back(column);
                }
            } else if (upper.starts_with("DROP TABLE")) {
                const auto start = sql.find_last_of(" \t");
                if (start == std::string::npos) DbThrow("malformed DROP TABLE");
                database.tables.erase(sql.substr(start + 1U));
            } else {
                DbThrow("only CREATE TABLE and DROP TABLE execSQL are supported");
            }
            PersistDatabase(context, database);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("insert",
        "(Ljava/lang/String;Ljava/lang/String;Landroid/content/ContentValues;)J",
        [context](dx::IntrinsicContext& call) {
            auto& database = RequireDb(context, call.receiver);
            auto& table = database.tables[DbString(call, call.arguments[0].ref)];
            auto row = context->content_values[call.arguments[2].ref.Value()];
            const auto id = table.next_row_id++;
            if (!row.contains("_id")) row["_id"] = id;
            for (const auto& entry : row)
                if (std::find(table.columns.begin(), table.columns.end(),
                              entry.first) == table.columns.end())
                    table.columns.push_back(entry.first);
            table.rows.push_back(std::move(row));
            PersistDatabase(context, database);
            return dx::VmValue::Long(id);
        });
    builder.FinalMethod("delete",
        "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            auto& database = RequireDb(context, call.receiver);
            const auto found = database.tables.find(DbString(call, call.arguments[0].ref));
            if (found == database.tables.end()) return dx::VmValue::Int(0);
            const auto selection = call.arguments[1].ref.IsValid() ?
                call.vm.StringUtf8(call.arguments[1].ref) : std::string{};
            const auto args = StringArray(call, call.arguments[2].ref);
            auto& rows = found->second.rows;
            const auto old = rows.size();
            std::erase_if(rows, [&](const DbRow& row) {
                return RowMatches(row, selection, args);
            });
            PersistDatabase(context, database);
            return dx::VmValue::Int(static_cast<std::int32_t>(old - rows.size()));
        });
    builder.FinalMethod("update",
        "(Ljava/lang/String;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            auto& database = RequireDb(context, call.receiver);
            const auto found = database.tables.find(DbString(call, call.arguments[0].ref));
            if (found == database.tables.end()) return dx::VmValue::Int(0);
            const auto values = context->content_values[call.arguments[1].ref.Value()];
            const auto selection = call.arguments[2].ref.IsValid() ?
                call.vm.StringUtf8(call.arguments[2].ref) : std::string{};
            const auto args = StringArray(call, call.arguments[3].ref);
            std::int32_t changed{};
            for (auto& row : found->second.rows) {
                if (!RowMatches(row, selection, args)) continue;
                for (const auto& entry : values) row[entry.first] = entry.second;
                ++changed;
            }
            PersistDatabase(context, database);
            return dx::VmValue::Int(changed);
        });
    builder.FinalMethod("query",
        "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        [context](dx::IntrinsicContext& call) {
            auto& database = RequireDb(context, call.receiver);
            const auto table_name = DbString(call, call.arguments[0].ref);
            const auto found = database.tables.find(table_name);
            if (found == database.tables.end()) DbThrow("table not found: " + table_name);
            auto columns = StringArray(call, call.arguments[1].ref);
            if (columns.empty()) columns = found->second.columns;
            const auto selection = call.arguments[2].ref.IsValid() ?
                call.vm.StringUtf8(call.arguments[2].ref) : std::string{};
            const auto args = StringArray(call, call.arguments[3].ref);
            std::vector<DbRow> rows;
            for (const auto& row : found->second.rows)
                if (RowMatches(row, selection, args)) rows.push_back(row);
            const auto cursor = call.vm.NewIntrinsicInstance(
                "Landroid/database/CursorImpl;");
            context->database_cursors[cursor.Value()] =
                {std::move(columns), std::move(rows), -1, false};
            return dx::VmValue::Ref(cursor);
        });
    return std::move(builder).Build();
}

Decl Declare_android_database_sqlite_SQLiteOpenHelper(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/database/sqlite/SQLiteOpenHelper;", "Ljava/lang/Object;");
    builder.Constructor(
        "(Landroid/content/Context;Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;I)V",
        [context](dx::IntrinsicContext& call) {
            if (call.arguments[3].AsInt() < 1)
                throw dx::VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "database version must be positive"};
            context->sqlite_helpers[call.receiver.Value()] =
                DexVmAndroidContext::SQLiteHelperState{
                DbString(call, call.arguments[1].ref),
                call.arguments[3].AsInt(), dx::VmObjectRef(0)};
            return dx::VmValue::Void();
        });
    const auto open = [context](dx::IntrinsicContext& call) {
        auto& helper = context->sqlite_helpers[call.receiver.Value()];
        if (!helper.database.IsValid())
            helper.database = OpenDatabase(call, context,
                                            DbPath(context, helper.name));
        auto& database = RequireDb(context, helper.database);
        const auto invoke = [&](const char* name, const char* descriptor,
                                std::vector<dx::VmValue> arguments) {
            const auto owner = call.vm.Model().ObjectClass(call.receiver);
            const auto index = call.vm.Linker().FindVtableIndex(
                owner, name, descriptor);
            if (!index.has_value()) {
                throw dx::VmJavaThrow{"Ljava/lang/AbstractMethodError;",
                                      std::string(name) + descriptor};
            }
            arguments.insert(arguments.begin(), dx::VmValue::Ref(call.receiver));
            return call.vm.Call(call.vm.Linker().Class(owner).vtable[*index],
                                arguments);
        };
        const auto previous = database.version;
        dx::VmCallOutcome outcome{dx::VmValue::Void(), dx::VmObjectRef{},
                                  dx::DexClassId{}, {}, {}};
        if (previous == 0) {
            outcome = invoke("onCreate",
                "(Landroid/database/sqlite/SQLiteDatabase;)V",
                {dx::VmValue::Ref(helper.database)});
        } else if (previous < helper.version) {
            outcome = invoke("onUpgrade",
                "(Landroid/database/sqlite/SQLiteDatabase;II)V",
                {dx::VmValue::Ref(helper.database), dx::VmValue::Int(previous),
                 dx::VmValue::Int(helper.version)});
        } else if (previous > helper.version) {
            DbThrow("database downgrade is not supported");
        }
        if (outcome.exception.IsValid()) {
            call.vm.SetPendingException(outcome.exception);
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        if (previous != helper.version) {
            database.version = helper.version;
            try {
                PersistDatabase(context, database);
            } catch (...) {
                database.version = previous;
                throw;
            }
        }
        return dx::VmValue::Ref(helper.database);
    };
    builder.FinalMethod("getWritableDatabase",
        "()Landroid/database/sqlite/SQLiteDatabase;", open);
    builder.FinalMethod("getReadableDatabase",
        "()Landroid/database/sqlite/SQLiteDatabase;", open);
    builder.FinalMethod("getDatabaseName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(call.vm.NewStringUtf8(
                context->sqlite_helpers[call.receiver.Value()].name));
        });
    builder.FinalMethod("close", "()V", [context](dx::IntrinsicContext& call) {
        auto& helper = context->sqlite_helpers[call.receiver.Value()];
        if (helper.database.IsValid()) {
            auto& database = RequireDb(context, helper.database);
            PersistDatabase(context, database);
            database.open = false;
        }
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("onCreate",
        "(Landroid/database/sqlite/SQLiteDatabase;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.VirtualMethod("onUpgrade",
        "(Landroid/database/sqlite/SQLiteDatabase;II)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics

namespace ogplay::runtime {

void RegisterAndroidDatabaseStateTables(
    dexvm::Interpreter& vm,
    const std::shared_ptr<DexVmAndroidContext>& context) {
    if (context == nullptr) return;
    vm.RegisterIntrinsicStateTable({
        "android.database",
        [context](const dexvm::VmObjectRef owner,
                  const dexvm::VmRootVisitor& visit) {
            if (const auto found = context->sqlite_helpers.find(owner.Value());
                found != context->sqlite_helpers.end() &&
                found->second.database.IsValid()) {
                visit(found->second.database);
            }
        },
        [context](const dexvm::VmObjectRef owner) {
            context->content_values.erase(owner.Value());
            context->database_cursors.erase(owner.Value());
            context->sqlite_helpers.erase(owner.Value());
            if (const auto found = context->databases.find(owner.Value());
                found != context->databases.end()) {
                context->database_by_path.erase(found->second.path);
                context->databases.erase(found);
            }
        }, {}});
}

}  // namespace ogplay::runtime

namespace ogplay::runtime::android_intrinsics {
Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context& context);
}



// ---- migrated from android_content_ContentResolver.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_ContentResolver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/ContentResolver;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_Context.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

[[nodiscard]] bool EnsureDirectory(const Context& context,
                                   const std::string& path) {
    if (context->vfs == nullptr) return false;
    for (std::size_t cursor = 1; cursor <= path.size(); ++cursor) {
        if (cursor != path.size() && path[cursor] != '/') continue;
        const auto prefix = path.substr(0, cursor);
        try {
            const auto info = context->vfs->Stat(prefix);
            if (!info.is_directory) return false;
            continue;
        } catch (const VfsError&) {
        }
        try {
            context->vfs->CreateDirectory(prefix);
        } catch (const VfsError&) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] dx::VmObjectRef ContextDirectory(
    dx::IntrinsicContext& call, const Context& context,
    const std::string& path, const std::string& singleton_key) {
    if (!EnsureDirectory(context, path)) return dx::VmObjectRef{};
    const auto found = context->singletons.find(singleton_key);
    if (found != context->singletons.end()) return found->second;
    const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
    const auto slots = call.vm.Model().InstanceSlots(file);
    slots[0] = {call.vm.NewStringUtf8(path).Value(), dx::SlotTag::ref};
    context->singletons.emplace(singleton_key, file);
    return file;
}

// Reads a preferences file once per name. Damaged XML is a real failure.
void LoadPreferencesOnce(const Context& context, const std::string& name) {
    if (context->preferences_loaded[name]) return;
    context->preferences_loaded[name] = true;
    if (context->vfs == nullptr) return;
    try {
        context->preferences[name] =
            LoadPreferences(*context->vfs, PreferencesPathOf(context, name));
    } catch (const PreferencesXmlError& error) {
        throw dx::VmJavaThrow{
            "Ljava/lang/IllegalStateException;",
            std::string("SharedPreferences file is not readable: ") +
                error.what()};
    }
}

}  // namespace

Decl Declare_android_content_Context(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/Context;", "Ljava/lang/Object;");
    builder.ConstantString(
               "POWER_SERVICE", "power",
               dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantString(
            "VIBRATOR_SERVICE", "vibrator",
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.VirtualMethod("getAssets", "()Landroid/content/res/AssetManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(
                call, context, "assets", "Landroid/content/res/AssetManager;"));
        });
    builder.VirtualMethod("getPackageName", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return MakeString(call, context->package_name);
        });
    builder.VirtualMethod("getPackageResourcePath", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return MakeString(call, context->package_resource_path);
        });
    // 返回当前应用 APK 的代码与资源文件路径。
    builder.VirtualMethod("getPackageCodePath", "()Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            return MakeString(call, context->package_resource_path);
        });
    // 返回描述当前应用包、进程和安装路径等信息的 ApplicationInfo。
    builder.VirtualMethod(
        "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;",
        [context](dx::IntrinsicContext& call) {
            constexpr auto key = "context_application_info";
            const auto found = context->singletons.find(key);
            if (found != context->singletons.end()) {
                return dx::VmValue::Ref(found->second);
            }
            const auto info = MakeApplicationInfo(call, context, false);
            context->singletons.emplace(key, info);
            return dx::VmValue::Ref(info);
        });
    builder.VirtualMethod("getPackageManager",
        "()Landroid/content/pm/PackageManager;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(
                call, context, "package_manager",
                "Landroid/content/pm/PackageManager;"));
        });
    builder.VirtualMethod("getApplicationContext", "()Landroid/content/Context;",
        [context](dx::IntrinsicContext& call) {
            // One guest process owns one application Context; Activity
            // wrappers may come and go without changing this identity.
            if (context->application.IsValid()) {
                return dx::VmValue::Ref(context->application);
            }
            return dx::VmValue::Ref(Singleton(
                call, context, "application_context",
                "Landroid/content/Context;"));
        });
    builder.VirtualMethod("getFilesDir", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "/data/data/" + context->package_name +
                              "/files";
            return dx::VmValue::Ref(ContextDirectory(
                call, context, path, "context_files_directory"));
        });
    // 返回当前应用存放可清理缓存文件的内部目录。
    builder.VirtualMethod("getCacheDir", "()Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            const auto path = "/data/data/" + context->package_name +
                              "/cache";
            return dx::VmValue::Ref(ContextDirectory(
                call, context, path, "context_cache_directory"));
        });
    builder.VirtualMethod("getResources", "()Landroid/content/res/Resources;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Singleton(call, context, "resources",
                "Landroid/content/res/Resources;"));
        });
    builder.VirtualMethod("getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            if (name == "phone") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "phone",
                    "Landroid/telephony/TelephonyManager;"));
            }
            if (name == "audio") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "audio", "Landroid/media/AudioManager;"));
            }
            if (name == "wifi") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "wifi", "Landroid/net/wifi/WifiManager;"));
            }
            if (name == "sensor") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "sensor",
                    "Landroid/hardware/SensorManager;"));
            }
            if (name == "connectivity") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "connectivity",
                    "Landroid/net/ConnectivityManager;"));
            }
            if (name == "input_method") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "input_method",
                    "Landroid/view/inputmethod/InputMethodManager;"));
            }
            if (name == "window") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "window_manager",
                    "Landroid/view/WindowManagerImpl;"));
            }
            if (name == "power") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "power", "Landroid/os/PowerManager;"));
            }
            if (name == "vibrator") {
                return dx::VmValue::Ref(Singleton(
                    call, context, "vibrator", "Landroid/os/Vibrator;"));
            }
            throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                  "system service is not provided: " + name};
        });
    builder.VirtualMethod("registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref;
            if (receiver.IsValid()) {
                context->broadcast_receivers[call.receiver.Value()].insert(
                    receiver.Value());
            }
            // Sticky broadcast lookup: nothing pending on this platform.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.VirtualMethod("unregisterReceiver",
        "(Landroid/content/BroadcastReceiver;)V",
        [context](dx::IntrinsicContext& call) {
            const auto receiver = call.arguments[0].ref;
            const auto owner = context->broadcast_receivers.find(
                call.receiver.Value());
            if (!receiver.IsValid() ||
                owner == context->broadcast_receivers.end() ||
                owner->second.erase(receiver.Value()) == 0U) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "Receiver not registered"};
            }
            if (owner->second.empty()) {
                context->broadcast_receivers.erase(owner);
            }
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("startActivity", "(Landroid/content/Intent;)V",
        [context](dx::IntrinsicContext& call) -> dx::VmValue {
            // In-process activity switch: only intents with an explicit
            // component that resolves to a dex activity are supported;
            // anything else (external apps, market links, ...) stays an
            // explicit failure.
            const auto intent = call.arguments[0].ref;
            const auto component =
                context->intent_components.find(intent.Value());
            if (component == context->intent_components.end()) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/UnsupportedOperationException;",
                    "startActivity without an in-package component is outside "
                    "the compatibility scope"};
            }
            context->pending_activity_descriptor = component->second;
            context->activity_switch_pending = true;
            context->current_intent = intent;
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;",
        [context](dx::IntrinsicContext& call) {
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto instance = Singleton(
                call, context, "prefs:" + name,
                "Landroid/content/SharedPreferencesImpl;");
            context->preference_names[instance.Value()] = name;
            LoadPreferencesOnce(context, name);
            return dx::VmValue::Ref(instance);
        });
    builder.VirtualMethod("getContentResolver", "()Landroid/content/ContentResolver;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "content_resolver",
                          "Landroid/content/ContentResolver;"));
        });
    builder.VirtualMethod("sendBroadcast", "(Landroid/content/Intent;)V",
        [](dx::IntrinsicContext& call) {
            // No other process exists; the broadcast truthfully has no
            // audience. Logged so silent drops stay visible.
            GuestLog(call, core::LogLevel::debug,
                     "sendBroadcast dropped: no receivers on this platform");
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;",
        [context](dx::IntrinsicContext& call) {
            // Platform layout under the external mount; a null type argument
            // answers the package files root.
            auto path = context->external_storage_root + "/Android/data/" +
                        context->package_name + "/files";
            const auto type = call.arguments[0].ref;
            if (type.IsValid()) {
                path += "/" + call.vm.StringUtf8(type);
            }
            const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
            const auto slots = call.vm.Model().InstanceSlots(file);
            slots[0] = {call.vm.NewStringUtf8(path).Value(), dx::SlotTag::ref};
            return dx::VmValue::Ref(file);
        });
    builder.VirtualMethod("startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::debug,
                     "startService answered null: no services on this "
                     "platform");
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    return std::move(builder).Build();
}

namespace {

dx::IntrinsicHandler DelegateContextMethod(
    const dx::IntrinsicFieldHandle base_field, std::string name,
    std::string descriptor) {
    return [base_field, name = std::move(name),
            descriptor = std::move(descriptor)](dx::IntrinsicContext& context) {
        dx::IntrinsicCall call(context);
        const auto base = call.GetRef(base_field);
        if (!base.IsValid()) {
            throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "ContextWrapper base context is null"};
        }
        auto& vm = context.vm;
        auto& linker = vm.Linker();
        const auto base_class = vm.Model().ObjectClass(base);
        const auto index = linker.FindVtableIndex(base_class, name, descriptor);
        if (!index.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/AbstractMethodError;",
                "base Context has no " + name + descriptor};
        }
        std::vector<dx::VmValue> arguments{dx::VmValue::Ref(base)};
        arguments.insert(arguments.end(), context.arguments.begin(),
                         context.arguments.end());
        const auto outcome = vm.Call(
            linker.Class(base_class).vtable[*index], arguments);
        if (outcome.exception.IsValid()) {
            vm.SetPendingException(outcome.exception);
        }
        return outcome.value;
    };
}

}  // namespace

Decl Declare_android_content_ContextWrapper(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/ContextWrapper;", "Landroid/content/Context;");
    const auto base = builder.BoundInstanceField(
        "mBase", "Landroid/content/Context;", 0U);
    builder.Constructor("(Landroid/content/Context;)V",
        [base](dx::IntrinsicContext& context) {
            dx::IntrinsicCall(context).SetRef(base, context.arguments[0].ref);
            return dx::VmValue::Void();
        });
    builder.VirtualMethod("attachBaseContext",
        "(Landroid/content/Context;)V",
        [base](dx::IntrinsicContext& context) {
            dx::IntrinsicCall call(context);
            if (call.GetRef(base).IsValid()) {
                throw dx::VmJavaThrow{
                    "Ljava/lang/IllegalStateException;",
                    "Base context already set"};
            }
            call.SetRef(base, context.arguments[0].ref);
            return dx::VmValue::Void();
        }, dx::kAccProtected);
    builder.VirtualMethod("getBaseContext", "()Landroid/content/Context;",
        [base](dx::IntrinsicContext& context) {
            return dx::VmValue::Ref(dx::IntrinsicCall(context).GetRef(base));
        });
    const auto delegate = [&](const char* name, const char* descriptor) {
        builder.OverrideMethod(name, descriptor,
                               DelegateContextMethod(base, name, descriptor));
    };
    delegate("getAssets", "()Landroid/content/res/AssetManager;");
    delegate("getPackageName", "()Ljava/lang/String;");
    delegate("getPackageResourcePath", "()Ljava/lang/String;");
    delegate("getPackageCodePath", "()Ljava/lang/String;");
    delegate("getApplicationInfo",
             "()Landroid/content/pm/ApplicationInfo;");
    delegate("getPackageManager", "()Landroid/content/pm/PackageManager;");
    delegate("getApplicationContext", "()Landroid/content/Context;");
    delegate("getFilesDir", "()Ljava/io/File;");
    delegate("getCacheDir", "()Ljava/io/File;");
    delegate("getResources", "()Landroid/content/res/Resources;");
    delegate("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    delegate("registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)"
        "Landroid/content/Intent;");
    delegate("unregisterReceiver", "(Landroid/content/BroadcastReceiver;)V");
    delegate("startActivity", "(Landroid/content/Intent;)V");
    delegate("getSharedPreferences",
        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
    delegate("getContentResolver", "()Landroid/content/ContentResolver;");
    delegate("sendBroadcast", "(Landroid/content/Intent;)V");
    delegate("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");
    delegate("startService",
        "(Landroid/content/Intent;)Landroid/content/ComponentName;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_DialogInterface_OnCancelListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnCancelListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnCancelListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_DialogInterface_OnClickListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnClickListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_DialogInterface_OnDismissListener.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnDismissListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnDismissListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_Intent.cpp ----
// Intent handlers keep component targets and typed extras in the session
// context maps; flag/category setters are fluent no-ops.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_Intent(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/Intent;", "Ljava/lang/Object;");
    const auto intent_init = [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    };
    const auto set_flags = [](dx::IntrinsicContext& call) {
        return Self(call);
    };
    builder.Constructor("(Ljava/lang/String;)V", intent_init);
    builder.Constructor("()V", intent_init);
    builder.Constructor("(Ljava/lang/String;Landroid/net/Uri;)V",
        intent_init);
    builder.Constructor("(Landroid/content/Context;Ljava/lang/Class;)V",
        [context](dx::IntrinsicContext& call) {
            const auto class_object = call.arguments[1].ref;
            const auto target = call.vm.Model().ClassOfClassObject(class_object);
            context->intent_components[call.receiver.Value()] =
                call.vm.Linker().Class(target).descriptor;
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setClassName",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            auto dotted = call.vm.StringUtf8(call.arguments[1].ref);
            std::string descriptor = "L";
            for (const auto unit : dotted) {
                descriptor.push_back(unit == '.' ? '/' : unit);
            }
            descriptor.push_back(';');
            context->intent_components[call.receiver.Value()] =
                std::move(descriptor);
            return Self(call);
        });
    builder.FinalMethod("addFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("putExtra",
        "(Ljava/lang/String;I)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_int_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.arguments[1].AsInt();
            return Self(call);
        });
    builder.FinalMethod("putExtra",
        "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
        [context](dx::IntrinsicContext& call) {
            context->intent_string_extras[call.receiver.Value()]
                [call.vm.StringUtf8(call.arguments[0].ref)] =
                    call.vm.StringUtf8(call.arguments[1].ref);
            return Self(call);
        });
    builder.FinalMethod("getStringExtra",
        "(Ljava/lang/String;)Ljava/lang/String;",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_string_extras.find(call.receiver.Value());
            if (extras != context->intent_string_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return MakeString(call, found->second);
                }
            }
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getIntExtra", "(Ljava/lang/String;I)I",
        [context](dx::IntrinsicContext& call) {
            const auto extras =
                context->intent_int_extras.find(call.receiver.Value());
            if (extras != context->intent_int_extras.end()) {
                const auto found = extras->second.find(
                    call.vm.StringUtf8(call.arguments[0].ref));
                if (found != extras->second.end()) {
                    return dx::VmValue::Int(found->second);
                }
            }
            return dx::VmValue::Int(call.arguments[1].AsInt());
        });
    builder.FinalMethod("removeExtra", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            const auto intent = call.receiver.Value();
            const auto name = call.vm.StringUtf8(call.arguments[0].ref);
            const auto remove = [intent, &name](auto& extras) {
                const auto values = extras.find(intent);
                if (values == extras.end()) return;
                values->second.erase(name);
                if (values->second.empty()) extras.erase(values);
            };
            remove(context->intent_string_extras);
            remove(context->intent_int_extras);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addCategory",
        "(Ljava/lang/String;)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("getAction", "()Ljava/lang/String;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("getExtras", "()Landroid/os/Bundle;",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Ref(dx::VmObjectRef{});
        });
    builder.FinalMethod("setFlags", "(I)Landroid/content/Intent;", set_flags);
    builder.FinalMethod("setDataAndType",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
        [](dx::IntrinsicContext& call) { return Self(call); });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_IntentFilter.cpp ----
#include "catalog.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

std::int32_t ParseJavaInt(const std::string_view text) {
    const auto fail = [&]() -> void {
        throw ogplay::runtime::dexvm::VmJavaThrow{
            "Ljava/lang/NumberFormatException;",
            "invalid IntentFilter authority port: " + std::string(text)};
    };
    if (text.empty()) fail();
    std::size_t cursor{};
    bool negative{};
    if (text[cursor] == '+' || text[cursor] == '-') {
        negative = text[cursor++] == '-';
        if (cursor == text.size()) fail();
    }
    constexpr std::uint64_t kPositiveLimit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    constexpr std::uint64_t kNegativeLimit = kPositiveLimit + 1U;
    const auto limit = negative ? kNegativeLimit : kPositiveLimit;
    std::uint64_t value{};
    for (; cursor < text.size(); ++cursor) {
        const auto ch = text[cursor];
        if (ch < '0' || ch > '9') fail();
        const auto digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (limit - digit) / 10U) fail();
        value = value * 10U + digit;
    }
    const auto signed_value = negative ? -static_cast<std::int64_t>(value)
                                       : static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(signed_value);
}

}  // namespace

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_IntentFilter(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/IntentFilter;", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.Constructor("()V",
        [context](dx::IntrinsicContext& call) {
            context->intent_filter_schemes.erase(call.receiver.Value());
            context->intent_filter_authorities.erase(call.receiver.Value());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addAction", "(Ljava/lang/String;)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("addDataScheme", "(Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter data scheme is null"};
            }
            const auto scheme = call.vm.StringUtf8(call.arguments[0].ref);
            auto& schemes =
                context->intent_filter_schemes[call.receiver.Value()];
            if (std::find(schemes.begin(), schemes.end(), scheme) ==
                schemes.end()) {
                schemes.push_back(scheme);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addDataAuthority",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        [context](dx::IntrinsicContext& call) {
            if (!call.arguments[0].ref.IsValid()) {
                throw dx::VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "IntentFilter authority host is null"};
            }
            auto host = call.vm.StringUtf8(call.arguments[0].ref);
            const auto port = call.arguments[1].ref.IsValid()
                ? ParseJavaInt(call.vm.StringUtf8(call.arguments[1].ref))
                : -1;
            const auto wildcard = !host.empty() && host.front() == '*';
            context->intent_filter_authorities[call.receiver.Value()].push_back(
                DexVmAndroidContext::IntentFilterAuthority{
                    .original_host = host,
                    .match_host = wildcard ? host.substr(1) : std::move(host),
                    .wildcard = wildcard,
                    .port = port,
                });
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_pm_PackageManager.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

namespace {

constexpr std::int32_t kGetMetaData = 0x00000080;
constexpr std::int32_t kGetPermissions = 0x00001000;
constexpr std::int32_t kPermissionGranted = 0;
constexpr std::int32_t kPermissionDenied = -1;

[[nodiscard]] const dx::LinkedField& Field(dx::IntrinsicContext& call,
                                           const dx::VmObjectRef object,
                                           const std::string_view name,
                                           const std::string_view descriptor) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), std::string(name),
        std::string(descriptor));
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "PackageManager field is not linked: " +
                                 std::string(name));
    }
    return call.vm.Linker().Field(*field);
}

void SetInt(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::int32_t value) {
    const auto& field = Field(call, object, name, "I");
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

void SetRef(dx::IntrinsicContext& call, const dx::VmObjectRef object,
            const std::string_view name, const std::string_view descriptor,
            const dx::VmObjectRef value) {
    const auto& field = Field(call, object, name, descriptor);
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        value.Value(), dx::SlotTag::ref};
}

[[nodiscard]] dx::VmObjectRef String(dx::IntrinsicContext& call,
                                     const std::string& value) {
    return call.vm.NewStringUtf8(value);
}

[[nodiscard]] std::string RequiredString(dx::IntrinsicContext& call,
                                         const std::size_t argument,
                                         const std::string_view name) {
    dx::IntrinsicCall typed(call);
    return call.vm.StringUtf8(typed.NonNullRef(argument, name));
}

void RequireCurrentPackage(const Context& context,
                           const std::string_view package_name) {
    if (package_name != context->package_name) {
        throw dx::VmJavaThrow{
            "Landroid/content/pm/PackageManager$NameNotFoundException;",
            std::string(package_name)};
    }
}

void RequireFlags(const std::int32_t flags, const std::int32_t supported,
                  const std::string_view method) {
    if ((flags & ~supported) != 0) {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            std::string(method) + " flags are outside the bounded API19 " +
                "PackageManager surface: " + std::to_string(flags)};
    }
}

[[nodiscard]] dx::VmObjectRef MakeStringArray(
    dx::IntrinsicContext& call, const std::vector<std::string>& values) {
    const auto array_class =
        call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
    const auto string_class =
        call.vm.Linker().ResolveDescriptor("Ljava/lang/String;");
    const auto array = call.vm.Model().NewObjectArray(
        array_class, string_class, static_cast<JniSize>(values.size()));
    JniSize index{};
    for (const auto& value : values) {
        call.vm.Model().SetObjectElement(array, index++, String(call, value));
    }
    return array;
}

[[nodiscard]] std::string ApplicationPackageName(
    dx::IntrinsicContext& call, const dx::VmObjectRef info) {
    const auto& field = Field(call, info, "packageName", "Ljava/lang/String;");
    const auto slot = call.vm.Model().InstanceSlots(info)[field.slot];
    if (slot.tag != dx::SlotTag::ref || slot.bits == 0U) return {};
    return call.vm.StringUtf8(dx::VmObjectRef{static_cast<std::uint32_t>(slot.bits)});
}

}  // namespace

Decl Declare_android_content_pm_PackageItemInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageItemInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("name", "Ljava/lang/String;")
        .InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("labelRes", "I")
        .InstanceField("nonLocalizedLabel", "Ljava/lang/CharSequence;")
        .InstanceField("icon", "I")
        .InstanceField("logo", "I")
        .InstanceField("metaData", "Landroid/os/Bundle;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_ApplicationInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/ApplicationInfo;",
        "Landroid/content/pm/PackageItemInfo;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("taskAffinity", "Ljava/lang/String;")
        .InstanceField("permission", "Ljava/lang/String;")
        .InstanceField("processName", "Ljava/lang/String;")
        .InstanceField("className", "Ljava/lang/String;")
        .InstanceField("descriptionRes", "I")
        .InstanceField("theme", "I")
        .InstanceField("flags", "I")
        .InstanceField("sourceDir", "Ljava/lang/String;")
        .InstanceField("publicSourceDir", "Ljava/lang/String;")
        .InstanceField("dataDir", "Ljava/lang/String;")
        .InstanceField("nativeLibraryDir", "Ljava/lang/String;")
        .InstanceField("uid", "I")
        .InstanceField("targetSdkVersion", "I")
        .InstanceField("enabled", "Z");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageInfo(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageInfo;", "Ljava/lang/Object;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.InstanceField("packageName", "Ljava/lang/String;")
        .InstanceField("versionCode", "I")
        .InstanceField("versionName", "Ljava/lang/String;")
        .InstanceField("applicationInfo", "Landroid/content/pm/ApplicationInfo;")
        .InstanceField("requestedPermissions", "[Ljava/lang/String;");
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager_NameNotFoundException(
    const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager$NameNotFoundException;",
        "Ljava/lang/Exception;");
    builder.Constructor("()V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

Decl Declare_android_content_pm_PackageManager(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class(
        "Landroid/content/pm/PackageManager;", "Ljava/lang/Object;", {},
        dx::kAccPublic | dx::kAccAbstract);
    builder.ConstantInt(
               "GET_META_DATA", "I", kGetMetaData,
               dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantInt(
            "GET_PERMISSIONS", "I", kGetPermissions,
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantInt(
            "PERMISSION_GRANTED", "I", kPermissionGranted,
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantInt(
            "PERMISSION_DENIED", "I", kPermissionDenied,
            dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantString("FEATURE_TOUCHSCREEN", "android.hardware.touchscreen",
                        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantString("FEATURE_SCREEN_LANDSCAPE",
                        "android.hardware.screen.landscape",
                        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal)
        .ConstantString("FEATURE_SCREEN_PORTRAIT",
                        "android.hardware.screen.portrait",
                        dx::kAccPublic | dx::kAccStatic | dx::kAccFinal);
    builder.VirtualMethod(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData, "getApplicationInfo");
            return dx::VmValue::Ref(MakeApplicationInfo(
                call, context, (flags & kGetMetaData) != 0));
        });
    builder.VirtualMethod(
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
        [context](dx::IntrinsicContext& call) {
            const auto package = RequiredString(call, 0U, "packageName");
            const auto flags = call.arguments[1].AsInt();
            RequireCurrentPackage(context, package);
            RequireFlags(flags, kGetMetaData | kGetPermissions,
                         "getPackageInfo");
            const auto info = call.vm.NewIntrinsicInstance(
                "Landroid/content/pm/PackageInfo;");
            SetRef(call, info, "packageName", "Ljava/lang/String;",
                   String(call, context->package_name));
            SetInt(call, info, "versionCode",
                   static_cast<std::int32_t>(context->package_version_code));
            SetRef(call, info, "versionName", "Ljava/lang/String;",
                   String(call, context->package_version_name));
            SetRef(call, info, "applicationInfo",
                   "Landroid/content/pm/ApplicationInfo;",
                   MakeApplicationInfo(
                       call, context, (flags & kGetMetaData) != 0));
            if ((flags & kGetPermissions) != 0) {
                SetRef(call, info, "requestedPermissions", "[Ljava/lang/String;",
                       MakeStringArray(call, context->requested_permissions));
            }
            return dx::VmValue::Ref(info);
        });
    builder.VirtualMethod(
        "getApplicationLabel",
        "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/CharSequence;",
        [context](dx::IntrinsicContext& call) {
            dx::IntrinsicCall typed(call);
            const auto info = typed.NonNullRef(0U, "info");
            RequireCurrentPackage(context, ApplicationPackageName(call, info));
            if (context->application_label.has_value()) {
                if (const auto* literal = std::get_if<std::string>(
                        &*context->application_label)) {
                    return MakeString(call, *literal);
                }
                return dx::VmValue::Ref(call.vm.Model().NewString(
                    ResolveUiString(
                        *context,
                        std::get<std::uint32_t>(*context->application_label))));
            }
            return MakeString(call, context->package_name);
        });
    builder.VirtualMethod(
        "checkPermission", "(Ljava/lang/String;Ljava/lang/String;)I",
        [context](dx::IntrinsicContext& call) {
            const auto permission = RequiredString(call, 0U, "permissionName");
            const auto package = RequiredString(call, 1U, "packageName");
            return dx::VmValue::Int(
                package == context->package_name &&
                        context->granted_permissions.contains(permission)
                    ? kPermissionGranted
                    : kPermissionDenied);
        });
    builder.VirtualMethod(
        "hasSystemFeature", "(Ljava/lang/String;)Z",
        [context](dx::IntrinsicContext& call) {
            const auto feature = RequiredString(call, 0U, "name");
            return dx::VmValue::Int(
                context->system_features.contains(feature) ? 1 : 0);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_SharedPreferences_Editor.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences_Editor(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/SharedPreferences$Editor;");
    builder.FinalMethod("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.FinalMethod("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_SharedPreferences.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferences(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/SharedPreferences;");
    builder.FinalMethod("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.FinalMethod("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.FinalMethod("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.FinalMethod("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.FinalMethod("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_SharedPreferencesEditorImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesEditorImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/SharedPreferencesEditorImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences$Editor;"});
    builder.FinalMethod("putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutBooleanHandler(context));
    builder.FinalMethod("putInt", "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutIntHandler(context));
    builder.FinalMethod("putLong", "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutLongHandler(context));
    builder.FinalMethod("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", PrefsEditorPutStringHandler(context));
    builder.FinalMethod("commit", "()Z", PrefsEditorCommitHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics


// ---- migrated from android_content_SharedPreferencesImpl.cpp ----
#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_SharedPreferencesImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/SharedPreferencesImpl;", "Ljava/lang/Object;", {"Landroid/content/SharedPreferences;"});
    builder.FinalMethod("edit", "()Landroid/content/SharedPreferences$Editor;", PrefsEditHandler(context));
    builder.FinalMethod("getBoolean", "(Ljava/lang/String;Z)Z", PrefsGetBooleanHandler(context));
    builder.FinalMethod("getInt", "(Ljava/lang/String;I)I", PrefsGetIntHandler(context));
    builder.FinalMethod("getLong", "(Ljava/lang/String;J)J", PrefsGetLongHandler(context));
    builder.FinalMethod("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", PrefsGetStringHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
