// Minimal POSIX shell for `sh -c`: words with quoting and $ expansion,
// simple commands, redirections, pipelines and ; && || lists. Compound
// commands, substitutions, globbing and here-documents are reported as
// unsupported syntax.

#include "ogplay/runtime/process/virtual_process.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEnoent = 2;
constexpr std::int32_t kEnotdir = 20;
constexpr std::int32_t kEisdir = 21;
constexpr std::int32_t kEacces = 13;
constexpr std::int32_t kErofs = 30;

class ShellSyntaxError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ShellRedirectError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string ErrorText(const std::int32_t error) {
    switch (error) {
    case kEnoent: return "No such file or directory";
    case kEnotdir: return "Not a directory";
    case kEisdir: return "Is a directory";
    case kEacces: return "Permission denied";
    case kErofs: return "Read-only file system";
    default: return "I/O error";
    }
}

// One piece of a word: literal text or a parameter to expand.
struct Piece final {
    bool parameter{};
    std::string text;
};

using Word = std::vector<Piece>;

enum class RedirectKind : std::uint8_t { input, output, append, duplicate_output, duplicate_input };

struct Redirect final {
    std::int32_t descriptor{};
    RedirectKind kind{};
    Word target;
};

struct SimpleCommand final {
    std::vector<Word> words;
    std::vector<Redirect> redirects;
};

struct Pipeline final {
    bool negate{};
    std::vector<SimpleCommand> commands;
};

enum class Connector : std::uint8_t { always, on_success, on_failure };

struct ListItem final {
    Connector connector{Connector::always};
    Pipeline pipeline;
};

enum class TokenKind : std::uint8_t { word, op, io_number, end };

struct Token final {
    TokenKind kind{TokenKind::end};
    Word word;
    std::string op;
    std::int32_t io_number{};
};

[[nodiscard]] bool IsNameStart(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool IsNameChar(const char c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

class Lexer final {
public:
    explicit Lexer(std::string_view script) : script_(script) {}

    [[nodiscard]] std::vector<Token> Tokens() {
        std::vector<Token> tokens;
        for (;;) {
            auto token = Next();
            const auto done = token.kind == TokenKind::end;
            tokens.push_back(std::move(token));
            if (done) return tokens;
        }
    }

private:
    [[nodiscard]] Token Next() {
        SkipBlanks();
        if (position_ >= script_.size()) return {};
        const auto c = script_[position_];
        if (c == '\n') {
            ++position_;
            return Operator(";");
        }
        if (c >= '0' && c <= '9' && position_ + 1U < script_.size() &&
            (script_[position_ + 1U] == '<' || script_[position_ + 1U] == '>')) {
            Token token;
            token.kind = TokenKind::io_number;
            token.io_number = c - '0';
            ++position_;
            return token;
        }
        for (const std::string_view op : {"&&", "||", ">>", ">&", "<&", ">|", "<<", "|", ";", "&", "<", ">"}) {
            if (script_.substr(position_).starts_with(op)) {
                position_ += op.size();
                if (op == "<<") throw ShellSyntaxError("here-documents are not supported");
                return Operator(std::string(op));
            }
        }
        if (c == '(' || c == ')') throw ShellSyntaxError("subshells are not supported");
        Token token;
        token.kind = TokenKind::word;
        token.word = Word();
        while (position_ < script_.size()) {
            const auto current = script_[position_];
            if (current == ' ' || current == '\t' || current == '\n' || current == ';' || current == '&' ||
                current == '|' || current == '<' || current == '>' || current == '(' || current == ')')
                break;
            if (current == '\'') {
                const auto close = script_.find('\'', position_ + 1U);
                if (close == std::string_view::npos) throw ShellSyntaxError("unterminated quote");
                Literal(token.word, script_.substr(position_ + 1U, close - position_ - 1U));
                position_ = close + 1U;
            } else if (current == '"') {
                ++position_;
                DoubleQuoted(token.word);
            } else if (current == '\\') {
                ++position_;
                if (position_ < script_.size()) {
                    if (script_[position_] != '\n') Literal(token.word, script_.substr(position_, 1));
                    ++position_;
                }
            } else if (current == '$') {
                Parameter(token.word);
            } else if (current == '`') {
                throw ShellSyntaxError("command substitution is not supported");
            } else if (current == '#' && token.word.empty()) {
                while (position_ < script_.size() && script_[position_] != '\n')
                    ++position_;
                return Next();
            } else if (current == '*' || current == '?' || current == '[') {
                // Globbing is not supported; the pattern stays literal.
                Literal(token.word, script_.substr(position_++, 1));
            } else {
                Literal(token.word, script_.substr(position_++, 1));
            }
        }
        return token;
    }

    void SkipBlanks() {
        while (position_ < script_.size() && (script_[position_] == ' ' || script_[position_] == '\t'))
            ++position_;
        if (script_.substr(position_).starts_with("\\\n")) {
            position_ += 2U;
            SkipBlanks();
        }
    }

    [[nodiscard]] static Token Operator(std::string op) {
        Token token;
        token.kind = TokenKind::op;
        token.op = std::move(op);
        return token;
    }

    static void Literal(Word& word, const std::string_view text) {
        if (!word.empty() && !word.back().parameter) {
            word.back().text += text;
        } else {
            word.push_back({false, std::string(text)});
        }
    }

    void DoubleQuoted(Word& word) {
        // An empty "" still makes a (possibly empty) word.
        Literal(word, "");
        while (position_ < script_.size() && script_[position_] != '"') {
            const auto c = script_[position_];
            if (c == '\\' && position_ + 1U < script_.size()) {
                const auto next = script_[position_ + 1U];
                if (next == '"' || next == '\\' || next == '$' || next == '`') {
                    Literal(word, script_.substr(position_ + 1U, 1));
                    position_ += 2U;
                } else if (next == '\n') {
                    position_ += 2U;
                } else {
                    Literal(word, script_.substr(position_++, 1));
                }
            } else if (c == '$') {
                Parameter(word);
            } else if (c == '`') {
                throw ShellSyntaxError("command substitution is not supported");
            } else {
                Literal(word, script_.substr(position_++, 1));
            }
        }
        if (position_ >= script_.size()) throw ShellSyntaxError("unterminated quote");
        ++position_;
    }

    void Parameter(Word& word) {
        ++position_;
        if (position_ >= script_.size()) {
            Literal(word, "$");
            return;
        }
        const auto c = script_[position_];
        if (c == '(') throw ShellSyntaxError("command substitution is not supported");
        if (c == '{') {
            const auto close = script_.find('}', position_);
            if (close == std::string_view::npos) throw ShellSyntaxError("unterminated ${");
            const auto name = script_.substr(position_ + 1U, close - position_ - 1U);
            if (name.empty() || !std::ranges::all_of(name, IsNameChar))
                throw ShellSyntaxError("parameter operators are not supported");
            word.push_back({true, std::string(name)});
            position_ = close + 1U;
            return;
        }
        if (c == '?' || c == '$' || c == '#' || (c >= '0' && c <= '9')) {
            word.push_back({true, std::string(1, c)});
            ++position_;
            return;
        }
        if (!IsNameStart(c)) {
            Literal(word, "$");
            return;
        }
        const auto start = position_;
        while (position_ < script_.size() && IsNameChar(script_[position_]))
            ++position_;
        word.push_back({true, std::string(script_.substr(start, position_ - start))});
    }

    std::string_view script_;
    std::size_t position_{};
};

class Parser final {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    [[nodiscard]] std::vector<ListItem> List(bool& background) {
        std::vector<ListItem> items;
        auto connector = Connector::always;
        for (;;) {
            while (IsOp(";"))
                ++index_;
            if (Peek().kind == TokenKind::end) break;
            items.push_back({connector, ParsePipeline()});
            if (Peek().kind == TokenKind::end) break;
            if (IsOp("&&")) {
                connector = Connector::on_success;
            } else if (IsOp("||")) {
                connector = Connector::on_failure;
            } else if (IsOp(";") || IsOp("&")) {
                background = background || IsOp("&");
                connector = Connector::always;
            } else {
                throw ShellSyntaxError("unexpected '" + Peek().op + "'");
            }
            ++index_;
            if (connector != Connector::always && Peek().kind == TokenKind::end)
                throw ShellSyntaxError("unexpected end of file");
        }
        return items;
    }

private:
    [[nodiscard]] const Token& Peek() const { return tokens_[index_]; }

    [[nodiscard]] bool IsOp(const std::string_view op) const { return Peek().kind == TokenKind::op && Peek().op == op; }

    [[nodiscard]] Pipeline ParsePipeline() {
        Pipeline pipeline;
        if (Peek().kind == TokenKind::word && Peek().word.size() == 1U && !Peek().word[0].parameter &&
            Peek().word[0].text == "!") {
            pipeline.negate = true;
            ++index_;
        }
        pipeline.commands.push_back(ParseCommand());
        while (IsOp("|")) {
            ++index_;
            pipeline.commands.push_back(ParseCommand());
        }
        return pipeline;
    }

    [[nodiscard]] SimpleCommand ParseCommand() {
        SimpleCommand command;
        for (;;) {
            const auto& token = Peek();
            if (token.kind == TokenKind::word) {
                if (command.words.empty() && token.word.size() == 1U && !token.word[0].parameter) {
                    static constexpr std::array<std::string_view, 12> kKeywords{
                        "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "{"};
                    if (std::ranges::find(kKeywords, token.word[0].text) != kKeywords.end())
                        throw ShellSyntaxError("compound commands are not supported");
                }
                command.words.push_back(token.word);
                ++index_;
                continue;
            }
            std::optional<std::int32_t> descriptor;
            if (token.kind == TokenKind::io_number) {
                descriptor = token.io_number;
                ++index_;
            }
            const auto& op = Peek();
            if (op.kind != TokenKind::op) {
                if (descriptor) throw ShellSyntaxError("redirection without operator");
                break;
            }
            Redirect redirect;
            if (op.op == ">" || op.op == ">|") {
                redirect = {descriptor.value_or(1), RedirectKind::output, {}};
            } else if (op.op == ">>") {
                redirect = {descriptor.value_or(1), RedirectKind::append, {}};
            } else if (op.op == "<") {
                redirect = {descriptor.value_or(0), RedirectKind::input, {}};
            } else if (op.op == ">&") {
                redirect = {descriptor.value_or(1), RedirectKind::duplicate_output, {}};
            } else if (op.op == "<&") {
                redirect = {descriptor.value_or(0), RedirectKind::duplicate_input, {}};
            } else {
                if (descriptor) throw ShellSyntaxError("redirection without operator");
                break;
            }
            ++index_;
            if (Peek().kind != TokenKind::word) throw ShellSyntaxError("missing redirection target");
            redirect.target = Peek().word;
            ++index_;
            command.redirects.push_back(std::move(redirect));
        }
        if (command.words.empty() && command.redirects.empty())
            throw ShellSyntaxError(Peek().kind == TokenKind::end ? "unexpected end of file"
                                                                 : "unexpected '" + Peek().op + "'");
        return command;
    }

    std::vector<Token> tokens_;
    std::size_t index_{};
};

class Shell final {
public:
    explicit Shell(VirtualCommandContext& context) : context_(context), cwd_(context.WorkingDirectory()) {
        for (const auto& entry : context.Environment()) {
            const auto equals = entry.find('=');
            if (equals != std::string::npos) Set(entry.substr(0, equals), entry.substr(equals + 1U));
        }
        if (!Get("PATH")) Set("PATH", std::string(kVirtualDefaultPath));
        name_ = context.Arguments().empty() ? "sh" : context.Arguments().front();
    }

    [[nodiscard]] int Run(const std::string_view script) {
        std::vector<ListItem> items;
        bool background{};
        try {
            items = Parser(Lexer(script).Tokens()).List(background);
        } catch (const ShellSyntaxError& error) {
            context_.Kernel().RecordUnsupported("process.sh.syntax");
            context_.Write(2, name_ + ": syntax error: " + error.what() + "\n");
            return 2;
        }
        if (background) context_.Kernel().RecordUnsupported("process.sh.background");
        for (const auto& item : items) {
            if (context_.StopToken().stop_requested()) break;
            if (item.connector == Connector::on_success && status_ != 0) continue;
            if (item.connector == Connector::on_failure && status_ == 0) continue;
            status_ = RunPipeline(item.pipeline);
            if (exit_requested_) break;
        }
        return status_;
    }

    void SetPositional(std::vector<std::string> positional) { positional_ = std::move(positional); }

private:
    [[nodiscard]] std::optional<std::string> Get(const std::string_view name) const {
        for (const auto& [key, value] : variables_)
            if (key == name) return value;
        return std::nullopt;
    }

    void Set(std::string name, std::string value) {
        for (auto& [key, existing] : variables_) {
            if (key == name) {
                existing = std::move(value);
                return;
            }
        }
        variables_.emplace_back(std::move(name), std::move(value));
    }

    [[nodiscard]] std::string Expand(const Word& word) const {
        std::string result;
        for (const auto& piece : word) {
            if (!piece.parameter) {
                result += piece.text;
            } else if (piece.text == "?") {
                result += std::to_string(status_);
            } else if (piece.text == "$") {
                result += std::to_string(context_.Pid());
            } else if (piece.text == "#") {
                result += std::to_string(positional_.empty() ? 0 : positional_.size() - 1U);
            } else if (piece.text.size() == 1U && piece.text[0] >= '0' && piece.text[0] <= '9') {
                const auto index = static_cast<std::size_t>(piece.text[0] - '0');
                if (index < positional_.size()) result += positional_[index];
            } else {
                result += Get(piece.text).value_or("");
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> Environment() const {
        std::vector<std::string> environment;
        environment.reserve(variables_.size());
        for (const auto& [key, value] : variables_)
            environment.push_back(key + "=" + value);
        return environment;
    }

    // Applies redirections to the bound (child) descriptor table.
    void ApplyRedirects(const std::vector<Redirect>& redirects) const {
        auto& vfs = context_.Vfs();
        for (const auto& redirect : redirects) {
            const auto target = Expand(redirect.target);
            if (redirect.kind == RedirectKind::duplicate_output || redirect.kind == RedirectKind::duplicate_input) {
                if (target == "-") {
                    try {
                        vfs.Close(redirect.descriptor);
                    } catch (const VfsError&) {
                    }
                    continue;
                }
                const auto source =
                    !target.empty() && std::ranges::all_of(target, [](const char c) { return c >= '0' && c <= '9'; })
                        ? std::stoi(target)
                        : -1;
                try {
                    vfs.DuplicateTo(source, redirect.descriptor);
                } catch (const VfsError&) {
                    throw ShellRedirectError(target + ": bad file descriptor");
                }
                continue;
            }
            VfsOpenOptions options;
            if (redirect.kind == RedirectKind::input) {
                options.read = true;
            } else {
                options.write = true;
                options.create = true;
                options.truncate = redirect.kind == RedirectKind::output;
                options.append = redirect.kind == RedirectKind::append;
            }
            std::int32_t opened{};
            try {
                opened = vfs.Open(Resolve(target), options);
            } catch (const VfsError& error) {
                const auto verb = redirect.kind == RedirectKind::input ? "can't open " : "can't create ";
                throw ShellRedirectError(verb + target + ": " + ErrorText(error.ErrorNumber()));
            }
            if (opened != redirect.descriptor) {
                vfs.DuplicateTo(opened, redirect.descriptor);
                vfs.Close(opened);
            }
        }
    }

    [[nodiscard]] std::string Resolve(const std::string& path) const {
        if (path.starts_with('/')) return path;
        return (cwd_.ends_with('/') ? cwd_ : cwd_ + "/") + path;
    }

    [[nodiscard]] int StatusOf(const std::int32_t status) const {
        if ((status & 0x7f) != 0) return 128 + (status & 0x7f);
        return (status >> 8) & 0xff;
    }

    [[nodiscard]] static bool IsBuiltin(const std::string_view name) {
        return name == "echo" || name == "true" || name == "false" || name == ":";
    }

    [[nodiscard]] static VirtualCommand Builtin(const std::string_view name) {
        if (name == "false") return [](VirtualCommandContext&) { return 1; };
        if (name == "echo") {
            return [](VirtualCommandContext& context) {
                const auto& arguments = context.Arguments();
                std::size_t first = 1;
                bool newline = true;
                if (arguments.size() > 1U && arguments[1] == "-n") {
                    newline = false;
                    first = 2;
                }
                std::string text;
                for (std::size_t index = first; index < arguments.size(); ++index) {
                    if (index != first) text += ' ';
                    text += arguments[index];
                }
                if (newline) text += '\n';
                return context.Write(1, text) ? 0 : 1;
            };
        }
        return [](VirtualCommandContext&) { return 0; };
    }

    // Starts one pipeline element; returns its pid, or nullopt with
    // `immediate` set when it finished without a process.
    [[nodiscard]] std::optional<std::int32_t> Start(const SimpleCommand& command,
                                                    const std::vector<std::pair<std::int32_t, std::int32_t>>& pipes,
                                                    const std::size_t index, int& immediate) {
        std::vector<std::string> arguments;
        std::vector<std::pair<std::string, std::string>> assignments;
        for (const auto& word : command.words) {
            auto value = Expand(word);
            const auto equals = value.find('=');
            if (arguments.empty() && equals != std::string::npos && equals != 0 && IsNameStart(value[0]) &&
                std::ranges::all_of(std::string_view(value).substr(0, equals), IsNameChar)) {
                assignments.emplace_back(value.substr(0, equals), value.substr(equals + 1U));
                continue;
            }
            arguments.push_back(std::move(value));
        }
        const auto arrange = [&, index] {
            auto& vfs = context_.Vfs();
            if (index > 0U) vfs.DuplicateTo(pipes[index - 1U].first, 0);
            if (index < pipes.size()) vfs.DuplicateTo(pipes[index].second, 1);
            for (const auto& [read_end, write_end] : pipes) {
                vfs.Close(read_end);
                vfs.Close(write_end);
            }
            ApplyRedirects(command.redirects);
        };
        const auto single = pipes.empty();
        if (arguments.empty()) {
            // Assignments only, or a bare redirection: no process runs.
            if (single)
                for (auto& [key, value] : assignments)
                    Set(std::move(key), std::move(value));
            immediate = 0;
            return std::nullopt;
        }
        const auto& name = arguments.front();
        if (single && name == "exit") {
            exit_requested_ = true;
            immediate = arguments.size() > 1U ? std::atoi(arguments[1].c_str()) & 0xff : status_;
            return std::nullopt;
        }
        if (single && name == "cd") {
            const auto target = arguments.size() > 1U ? arguments[1] : Get("HOME").value_or("/");
            const auto path = Resolve(target);
            try {
                if (!context_.Vfs().Stat(path).is_directory) {
                    context_.Write(2, name_ + ": cd: " + target + ": Not a directory\n");
                    immediate = 1;
                    return std::nullopt;
                }
            } catch (const VfsError& error) {
                context_.Write(2, name_ + ": cd: " + target + ": " + ErrorText(error.ErrorNumber()) + "\n");
                immediate = 1;
                return std::nullopt;
            }
            cwd_ = path;
            immediate = 0;
            return std::nullopt;
        }
        auto environment = Environment();
        for (const auto& [key, value] : assignments)
            environment.push_back(key + "=" + value);
        auto& kernel = context_.Kernel();
        const auto table = context_.Vfs().CurrentDescriptorTable();
        try {
            if (IsBuiltin(name))
                return kernel.SpawnCommand(context_.Pid(), table, Builtin(name), std::move(arguments),
                                           std::move(environment), cwd_, arrange);
            const auto path = kernel.ResolveExecutable(name, Get("PATH").value_or(""));
            if (!path) {
                const auto message = name_ + ": " + name + ": not found\n";
                return kernel.SpawnCommand(
                    context_.Pid(), table,
                    [message](VirtualCommandContext& child) {
                        child.Write(2, message);
                        return 127;
                    },
                    {name}, std::move(environment), cwd_, arrange);
            }
            return kernel.Spawn(context_.Pid(), table, *path, std::move(arguments), std::move(environment), cwd_,
                                arrange);
        } catch (const ShellRedirectError& error) {
            context_.Write(2, name_ + ": " + error.what() + "\n");
            immediate = 1;
            return std::nullopt;
        }
    }

    [[nodiscard]] int RunPipeline(const Pipeline& pipeline) {
        auto& vfs = context_.Vfs();
        std::vector<std::pair<std::int32_t, std::int32_t>> pipes;
        for (std::size_t index = 1; index < pipeline.commands.size(); ++index) {
            const auto pipe = vfs.CreatePipe();
            pipes.emplace_back(pipe.read_descriptor, pipe.write_descriptor);
        }
        std::vector<std::int32_t> started;
        std::optional<std::int32_t> last;
        int status{};
        for (std::size_t index = 0; index < pipeline.commands.size(); ++index) {
            int immediate{};
            last = Start(pipeline.commands[index], pipes, index, immediate);
            if (last) {
                started.push_back(*last);
            } else {
                status = immediate;
            }
        }
        for (const auto& [read_end, write_end] : pipes) {
            vfs.Close(read_end);
            vfs.Close(write_end);
        }
        const auto stop = context_.StopToken();
        for (const auto pid : started) {
            try {
                const auto result =
                    context_.Kernel().Wait(context_.Pid(), pid, false, [stop] { return stop.stop_requested(); });
                // The last element decides the pipeline status.
                if (last && pid == *last) status = StatusOf(result.status);
            } catch (const VirtualProcessError&) {
                if (last && pid == *last) status = 1;
            }
        }
        return pipeline.negate ? (status == 0 ? 1 : 0) : status;
    }

    VirtualCommandContext& context_;
    std::string cwd_;
    std::string name_;
    std::vector<std::pair<std::string, std::string>> variables_;
    std::vector<std::string> positional_;
    int status_{};
    bool exit_requested_{};
};

[[nodiscard]] std::optional<std::string> ReadAll(VirtualCommandContext& context, const std::int32_t descriptor) {
    std::string script;
    std::array<std::byte, 4096> buffer{};
    for (;;) {
        const auto count = context.Read(descriptor, buffer);
        if (count == 0) return script;
        script.append(reinterpret_cast<const char*>(buffer.data()), count);
        if (script.size() > 1024U * 1024U) return std::nullopt;
    }
}

}  // namespace

int RunShellCommand(VirtualCommandContext& context) {
    const auto& arguments = context.Arguments();
    Shell shell(context);
    const auto name = arguments.empty() ? std::string("sh") : arguments.front();
    if (arguments.size() >= 2U && arguments[1] == "-c") {
        if (arguments.size() < 3U) {
            context.Write(2, name + ": -c: option requires an argument\n");
            return 2;
        }
        // sh -c SCRIPT [NAME [ARGS...]]: NAME becomes $0.
        std::vector<std::string> positional(arguments.begin() + 3, arguments.end());
        if (positional.empty()) positional.push_back(name);
        shell.SetPositional(std::move(positional));
        return shell.Run(arguments[2]);
    }
    if (arguments.size() >= 2U && arguments[1].starts_with('-')) {
        context.Kernel().RecordUnsupported("process.sh.option");
        context.Write(2, name + ": " + arguments[1] + ": unknown option\n");
        return 2;
    }
    std::optional<std::string> script;
    if (arguments.size() >= 2U) {
        // sh FILE: run the script file.
        try {
            const auto descriptor = context.Vfs().Open(context.Absolute(arguments[1]), {.read = true});
            script = ReadAll(context, descriptor);
            context.Vfs().Close(descriptor);
        } catch (const VfsError&) {
            context.Write(2, name + ": " + arguments[1] + ": can't open\n");
            return 127;
        }
        shell.SetPositional(std::vector<std::string>(arguments.begin() + 1, arguments.end()));
    } else {
        script = ReadAll(context, 0);
        shell.SetPositional({name});
    }
    if (!script) {
        context.Write(2, name + ": script is too large\n");
        return 2;
    }
    return shell.Run(*script);
}

}  // namespace ogplay::runtime
