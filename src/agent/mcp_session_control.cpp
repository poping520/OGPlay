#include "ogplay/agent/mcp_session_control.h"

#include <limits>
#include <stdexcept>

namespace ogplay::agent {
namespace {

core::JsonWriter::Value ToolError(core::JsonWriter& writer,
                                  const std::string_view message) {
    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", message);
    writer.Append(content, text);
    writer.Add(result, "content", content);
    writer.AddBool(result, "isError", true);
    return result;
}

template <typename AddFields>
core::JsonWriter::Value ToolResult(core::JsonWriter& writer,
                                  AddFields&& add_fields) {
    core::JsonWriter text_writer;
    const auto text_value = text_writer.Object();
    add_fields(text_writer, text_value);
    const auto result = writer.Object();
    const auto content = writer.Array();
    const auto text = writer.Object();
    writer.AddString(text, "type", "text");
    writer.AddString(text, "text", text_writer.Serialize(text_value));
    writer.Append(content, text);
    writer.Add(result, "content", content);
    const auto structured = writer.Object();
    add_fields(writer, structured);
    writer.Add(result, "structuredContent", structured);
    writer.AddBool(result, "isError", false);
    return result;
}

void AddSnapshot(core::JsonWriter& writer, const core::JsonWriter::Value object,
                 const McpSessionSnapshot& snapshot) {
    writer.AddString(object, "lifecycle", McpLifecycleStateName(snapshot.lifecycle));
    writer.AddUnsignedInteger(object, "frame", snapshot.frame);
    writer.AddUnsignedInteger(object, "guestTicks", snapshot.guest_ticks);
    if (snapshot.presented_frame) {
        writer.AddUnsignedInteger(object, "presentedFrame", *snapshot.presented_frame);
    } else {
        writer.AddNull(object, "presentedFrame");
    }
    if (snapshot.movie_request) {
        const auto movie = writer.Object();
        writer.AddUnsignedInteger(movie, "sequence", snapshot.movie_request->sequence);
        writer.AddString(movie, "name", snapshot.movie_request->name);
        writer.Add(object, "movieRequest", movie);
    } else {
        writer.AddNull(object, "movieRequest");
    }
    writer.AddBool(object, "processExit", snapshot.process_exit);
    if (snapshot.guest_fault) writer.AddString(object, "guestFault", *snapshot.guest_fault);
    else writer.AddNull(object, "guestFault");
    writer.AddBool(object, "shutdownRequested", snapshot.shutdown_requested);
}

core::JsonWriter::Value InputSchema(core::JsonWriter& writer,
                                    const std::string_view name) {
    const auto schema = writer.Object();
    writer.AddString(schema, "type", "object");
    const auto properties = writer.Object();
    const auto required = writer.Array();
    if (name == "step") {
        const auto frames = writer.Object();
        writer.AddString(frames, "type", "integer");
        writer.AddInteger(frames, "minimum", 1);
        writer.AddUnsignedInteger(frames, "maximum",
                                  McpSessionControl::kMaximumStepFrames);
        writer.Add(properties, "frames", frames);
        writer.Append(required, writer.String("frames"));
    } else if (name == "lifecycle") {
        const auto action = writer.Object();
        writer.AddString(action, "type", "string");
        const auto values = writer.Array();
        writer.Append(values, writer.String("suspend"));
        writer.Append(values, writer.String("resume"));
        writer.Add(action, "enum", values);
        writer.Add(properties, "action", action);
        writer.Append(required, writer.String("action"));
    }
    writer.Add(schema, "properties", properties);
    if (name == "step" || name == "lifecycle") writer.Add(schema, "required", required);
    writer.AddBool(schema, "additionalProperties", false);
    return schema;
}

core::JsonWriter::Value OutputSchema(core::JsonWriter& writer,
                                     const std::string_view name) {
    const auto schema = writer.Object();
    writer.AddString(schema, "type", "object");
    const auto properties = writer.Object();
    const auto required = writer.Array();
    const auto integer = [&](const std::string_view field) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "integer");
        writer.Add(properties, field, property);
        writer.Append(required, writer.String(field));
    };
    const auto string = [&](const std::string_view field) {
        const auto property = writer.Object();
        writer.AddString(property, "type", "string");
        writer.Add(properties, field, property);
        writer.Append(required, writer.String(field));
    };
    if (name == "session_state") {
        const auto lifecycle = writer.Object();
        writer.AddString(lifecycle, "type", "string");
        const auto states = writer.Array();
        for (const std::string_view state :
             {"ready", "running", "suspended", "stopped", "failed"}) {
            writer.Append(states, writer.String(state));
        }
        writer.Add(lifecycle, "enum", states);
        writer.Add(properties, "lifecycle", lifecycle);
        writer.Append(required, writer.String("lifecycle"));
        integer("frame");
        integer("guestTicks");
        for (const std::string_view field :
             {"presentedFrame", "movieRequest", "guestFault"}) {
            const auto property = writer.Object();
            const auto types = writer.Array();
            writer.Append(types, writer.String(field == "movieRequest" ? "object" :
                                                field == "guestFault" ? "string" : "integer"));
            writer.Append(types, writer.String("null"));
            writer.Add(property, "type", types);
            if (field == "movieRequest") {
                const auto movie_properties = writer.Object();
                const auto sequence = writer.Object();
                writer.AddString(sequence, "type", "integer");
                writer.Add(movie_properties, "sequence", sequence);
                const auto movie_name = writer.Object();
                writer.AddString(movie_name, "type", "string");
                writer.Add(movie_properties, "name", movie_name);
                writer.Add(property, "properties", movie_properties);
                const auto movie_required = writer.Array();
                writer.Append(movie_required, writer.String("sequence"));
                writer.Append(movie_required, writer.String("name"));
                writer.Add(property, "required", movie_required);
                writer.AddBool(property, "additionalProperties", false);
            }
            writer.Add(properties, field, property);
            writer.Append(required, writer.String(field));
        }
        for (const std::string_view field : {"processExit", "shutdownRequested"}) {
            const auto property = writer.Object();
            writer.AddString(property, "type", "boolean");
            writer.Add(properties, field, property);
            writer.Append(required, writer.String(field));
        }
    } else {
        integer("requestSequence");
        integer("startingFrame");
        if (name == "step") {
            integer("targetFrame");
            integer("frames");
        } else {
            string("action");
        }
    }
    writer.Add(schema, "properties", properties);
    writer.Add(schema, "required", required);
    writer.AddBool(schema, "additionalProperties", false);
    return schema;
}

void AppendTool(core::JsonWriter& writer, const core::JsonWriter::Value tools,
                const std::string_view name, const std::string_view title,
                const std::string_view description, const bool read_only) {
    const auto tool = writer.Object();
    writer.AddString(tool, "name", name);
    writer.AddString(tool, "title", title);
    writer.AddString(tool, "description", description);
    writer.Add(tool, "inputSchema", InputSchema(writer, name));
    writer.Add(tool, "outputSchema", OutputSchema(writer, name));
    const auto annotations = writer.Object();
    writer.AddBool(annotations, "readOnlyHint", read_only);
    writer.AddBool(annotations, "destructiveHint", name == "shutdown");
    writer.AddBool(annotations, "idempotentHint", read_only);
    writer.AddBool(annotations, "openWorldHint", false);
    writer.Add(tool, "annotations", annotations);
    writer.Append(tools, tool);
}

bool EmptyArguments(const std::optional<core::JsonValue> arguments) {
    return !arguments || (arguments->IsObject() && arguments->Size() == 0U);
}

}  // namespace

void McpSessionControl::Publish(McpSessionSnapshot snapshot) {
    std::scoped_lock lock(mutex_);
    snapshot.shutdown_requested = snapshot.shutdown_requested ||
                                  snapshot_.shutdown_requested;
    snapshot_ = std::move(snapshot);
}

McpSessionSnapshot McpSessionControl::Snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

std::optional<McpSessionCommand> McpSessionControl::TryEnqueue(
    const McpSessionCommand::Type type, const std::uint32_t frames) {
    if ((type == McpSessionCommand::Type::step &&
         (frames == 0U || frames > kMaximumStepFrames)) ||
        (type != McpSessionCommand::Type::step && frames != 0U)) {
        throw std::invalid_argument("invalid MCP session command frame count");
    }
    std::scoped_lock lock(mutex_);
    if (commands_.size() >= kMaximumPendingCommands) return std::nullopt;
    if (type == McpSessionCommand::Type::step &&
        snapshot_.frame > (std::numeric_limits<std::uint64_t>::max)() - frames) {
        throw std::overflow_error("MCP step target frame overflow");
    }
    McpSessionCommand command{type, next_request_sequence_++, snapshot_.frame, frames};
    commands_.push_back(command);
    if (type == McpSessionCommand::Type::shutdown) snapshot_.shutdown_requested = true;
    return command;
}

std::optional<McpSessionCommand> McpSessionControl::TakeNextCommand() {
    std::scoped_lock lock(mutex_);
    if (commands_.empty()) return std::nullopt;
    auto result = commands_.front();
    commands_.pop_front();
    return result;
}

std::size_t McpSessionControl::PendingCommands() const {
    std::scoped_lock lock(mutex_);
    return commands_.size();
}

std::string_view McpLifecycleStateName(const McpLifecycleState state) {
    switch (state) {
        case McpLifecycleState::ready: return "ready";
        case McpLifecycleState::running: return "running";
        case McpLifecycleState::suspended: return "suspended";
        case McpLifecycleState::stopped: return "stopped";
        case McpLifecycleState::failed: return "failed";
    }
    throw std::invalid_argument("unknown MCP lifecycle state");
}

void AppendMcpSessionTools(core::JsonWriter& writer,
                           const core::JsonWriter::Value tools) {
    AppendTool(writer, tools, "session_state", "Read OGPlay session state",
               "Returns one atomic lifecycle, frame, guest clock, movie, fault and exit snapshot.",
               true);
    AppendTool(writer, tools, "step", "Advance OGPlay guest frames",
               "Queues a bounded number of deterministic guest frames without waiting.", false);
    AppendTool(writer, tools, "lifecycle", "Change OGPlay lifecycle",
               "Queues exactly one suspend or resume transition.", false);
    AppendTool(writer, tools, "shutdown", "Shut down OGPlay session",
               "Queues a normal guest session shutdown.", false);
}

bool IsMcpSessionTool(const std::string_view name) {
    return name == "session_state" || name == "step" || name == "lifecycle" ||
           name == "shutdown";
}

core::JsonWriter::Value CallMcpSessionTool(
    core::JsonWriter& writer, McpSessionControl* control,
    const std::string_view name, const std::optional<core::JsonValue> arguments) {
    if (control == nullptr) {
        return ToolError(writer, "Session control is unavailable for this MCP session.");
    }
    if (name == "session_state") {
        if (!EmptyArguments(arguments)) {
            return ToolError(writer, "session_state accepts no arguments");
        }
        const auto snapshot = control->Snapshot();
        return ToolResult(writer, [&](auto& output, const auto object) {
            AddSnapshot(output, object, snapshot);
        });
    }

    McpSessionCommand::Type type{};
    std::uint32_t frames{};
    if (name == "step") {
        if (!arguments || !arguments->IsObject() || arguments->Size() != 1U) {
            return ToolError(writer, "step requires exactly one integer frames argument");
        }
        const auto value = arguments->Member("frames");
        const auto count = value ? value->UnsignedInteger() : std::nullopt;
        if (!count || *count == 0U || *count > McpSessionControl::kMaximumStepFrames) {
            return ToolError(writer, "step frames must be an integer in 1..1000000");
        }
        type = McpSessionCommand::Type::step;
        frames = static_cast<std::uint32_t>(*count);
    } else if (name == "lifecycle") {
        if (!arguments || !arguments->IsObject() || arguments->Size() != 1U) {
            return ToolError(writer, "lifecycle requires exactly one action argument");
        }
        const auto value = arguments->Member("action");
        const auto action = value ? value->String() : std::nullopt;
        if (!action || (*action != "suspend" && *action != "resume")) {
            return ToolError(writer, "lifecycle action must be 'suspend' or 'resume'");
        }
        type = *action == "suspend" ? McpSessionCommand::Type::suspend
                                    : McpSessionCommand::Type::resume;
    } else {
        if (!EmptyArguments(arguments)) return ToolError(writer, "shutdown accepts no arguments");
        type = McpSessionCommand::Type::shutdown;
    }
    const auto command = control->TryEnqueue(type, frames);
    if (!command) return ToolError(writer, "session command queue is full");
    return ToolResult(writer, [&](auto& output, const auto object) {
        output.AddUnsignedInteger(object, "requestSequence", command->request_sequence);
        output.AddUnsignedInteger(object, "startingFrame", command->starting_frame);
        if (type == McpSessionCommand::Type::step) {
            output.AddUnsignedInteger(object, "targetFrame",
                                      command->starting_frame + command->frames);
            output.AddUnsignedInteger(object, "frames", command->frames);
        } else {
            const auto action = type == McpSessionCommand::Type::suspend ? "suspend" :
                                type == McpSessionCommand::Type::resume ? "resume" : "shutdown";
            output.AddString(object, "action", action);
        }
    });
}

}  // namespace ogplay::agent
