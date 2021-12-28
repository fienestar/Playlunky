#include "shader_merge.h"

#include "log.h"
#include "util/algorithms.h"
#include "util/image.h"
#include "util/regex.h"
#include "virtual_filesystem.h"

#include "spel2.h"

#include <fstream>
#include <unordered_map>

bool MergeShaders(const std::filesystem::path& source_folder, const std::filesystem::path& destination_folder, const std::filesystem::path& shader_file, VirtualFilesystem& vfs)
{

    const auto source_shader = vfs.GetFilePath(shader_file).value_or(source_folder / shader_file);
    std::string original_shader_code = [&source_shader]()
    {
        if (auto original_shader_file = std::ifstream{ source_shader })
        {
            return std::string((std::istreambuf_iterator<char>(original_shader_file)), std::istreambuf_iterator<char>());
        }
        return std::string{};
    }();

    if (original_shader_code.empty())
    {
        return false;
    }
    auto find_decl_in_original = [&original_shader_code](std::string_view decl)
    {
        return original_shader_code.find(decl) != std::string::npos;
    };

    const auto shader_mods = vfs.GetAllFilePaths("shaders_mod.hlsl");

    struct ModdedFunction
    {
        std::string Preamble;
        std::string Declaration;
        std::string Body;
    };
    std::vector<ModdedFunction> modded_functions;

    struct ExtendedFunction
    {
        std::string FunctionName;
        std::vector<ModdedFunction> Extensions;
    };
    std::vector<ExtendedFunction> extended_functions;

    for (const auto& shader_mod : shader_mods)
    {
        const std::string shader_mod_code = [&shader_mod]()
        {
            if (auto shader_mod_file = std::ifstream{ shader_mod })
            {
                return std::string((std::istreambuf_iterator<char>(shader_mod_file)), std::istreambuf_iterator<char>());
            }
            return std::string{};
        }();

        if (!shader_mod_code.empty())
        {
            auto parsing_index = size_t{ 0 };
            auto read_char = [&shader_mod_code, &parsing_index]() -> std::optional<char>
            {
                if (parsing_index < shader_mod_code.size())
                {
                    char c = shader_mod_code[parsing_index];
                    parsing_index++;
                    return c;
                }
                return std::nullopt;
            };
            auto peek_char = [&shader_mod_code, &parsing_index]() -> std::optional<char>
            {
                if (parsing_index < shader_mod_code.size())
                {
                    char c = shader_mod_code[parsing_index];
                    return c;
                }
                return std::nullopt;
            };

            enum class CommentState
            {
                None,
                SingleLine,
                MultiLine
            };
            CommentState comment_state = CommentState::None;

            std::string function_preamble;
            std::string current_line;
            std::string function_body;
            bool is_shader_extension{ false };
            std::size_t scope_depth = 0;
            while (auto c_opt = read_char())
            {
                char c = c_opt.value();

                if (comment_state == CommentState::None && c == '/')
                {
                    if (peek_char().value_or('?') == '/')
                    {
                        read_char();
                        comment_state = CommentState::SingleLine;
                        continue;
                    }
                    else if (peek_char().value_or('?') == '*')
                    {
                        read_char();
                        comment_state = CommentState::MultiLine;
                        continue;
                    }
                }
                else if (comment_state == CommentState::SingleLine)
                {
                    if (c == '\n')
                    {
                        comment_state = CommentState::None;
                    }
                    continue;
                }
                else if (comment_state == CommentState::MultiLine)
                {
                    if (c == '*' && peek_char().value_or('?') == '/')
                    {
                        read_char();
                        comment_state = CommentState::None;
                    }
                    continue;
                }

                if (c == '{')
                {
                    if (scope_depth == 0)
                    {
                        function_body.clear();
                    }
                    scope_depth++;
                }
                else if (c == '}')
                {
                    if (scope_depth == 0)
                    {
                        LogError("Shader {} contains syntax errors...", shader_mod.string());
                        break;
                    }
                    else
                    {
                        if (scope_depth == 1)
                        {
                            if (algo::trim(current_line).find("struct") == 0 || (!is_shader_extension && !find_decl_in_original(current_line)))
                            {
                                function_preamble += current_line + function_body;
                            }
                            else if (is_shader_extension)
                            {
                                function_body += '}';
                                const auto first_space_pos = current_line.find(' ');
                                const auto first_parens_pos = current_line.find('(');
                                if (first_space_pos != std::string::npos && first_parens_pos != std::string::npos)
                                {
                                    std::string function_name = algo::trim(current_line.substr(first_space_pos, first_parens_pos - first_space_pos));
                                    ExtendedFunction* extended_function = algo::find(extended_functions, &ExtendedFunction::FunctionName, function_name);
                                    if (extended_function == nullptr)
                                    {
                                        extended_functions.push_back(ExtendedFunction{
                                            .FunctionName{ std::move(function_name) } });
                                        extended_function = &extended_functions.back();
                                    }
                                    extended_function->Extensions.push_back(ModdedFunction{
                                        .Preamble = std::move(function_preamble),
                                        .Declaration = std::move(current_line),
                                        .Body = std::move(function_body) });
                                }
                                current_line.clear();
                                function_body.clear();
                                is_shader_extension = false;
                                scope_depth--;
                                continue;
                            }
                            else if (!algo::contains(modded_functions, &ModdedFunction::Declaration, current_line))
                            {
                                function_body += '}';
                                modded_functions.push_back(ModdedFunction{
                                    .Preamble = std::move(function_preamble),
                                    .Declaration = std::move(current_line),
                                    .Body = std::move(function_body) });
                                scope_depth--;
                                continue;
                            }
                            current_line.clear();
                            function_body.clear();
                            is_shader_extension = false;
                        }
                        scope_depth--;
                    }
                }
                else if (c == '\n' && scope_depth == 0)
                {
                    if (current_line == "#extends")
                    {
                        is_shader_extension = true;
                    }
                    else
                    {
                        function_preamble += current_line + '\n';
                        is_shader_extension = false;
                    }
                    current_line.clear();
                }

                if (scope_depth == 0)
                {
                    if (!current_line.empty() || !std::isspace(c))
                    {
                        current_line += c;
                    }
                }
                else
                {
                    function_body += c;
                }
            }
        }
    }

    for (const ModdedFunction& modded_function : modded_functions)
    {
        const auto decl_pos = original_shader_code.find(modded_function.Declaration);
        if (decl_pos != std::string::npos)
        {
            const auto opening_braces_pos = original_shader_code.find('{', decl_pos + modded_function.Declaration.size());
            if (opening_braces_pos != std::string::npos)
            {
                const auto closing_braces = [&original_shader_code, &opening_braces_pos]() -> std::size_t
                {
                    std::size_t current_depth{ 0 };
                    for (std::size_t i = opening_braces_pos; i < original_shader_code.size(); i++)
                    {
                        if (original_shader_code[i] == '{')
                        {
                            current_depth++;
                        }
                        else if (original_shader_code[i] == '}')
                        {
                            current_depth--;
                            if (current_depth == 0)
                            {
                                return i;
                            }
                        }
                    }
                    return std::string::npos;
                }();
                if (closing_braces != std::string::npos)
                {
                    original_shader_code.replace(opening_braces_pos, closing_braces - opening_braces_pos + 1, modded_function.Body);
                    original_shader_code.insert(decl_pos, modded_function.Preamble);
                }
            }
        }
        else
        {
            LogError("Could not place function with declaration '{}' into shaders. "
                     "If you are just using this mod report the issue to the mods creator. "
                     "If you developed this mod, make sure it's signature matches exactly the original function's signature...",
                     modded_function.Declaration);
        }
    }

    for (const ExtendedFunction& extended_function : extended_functions)
    {
        const auto name_pos = original_shader_code.find(extended_function.FunctionName);
        if (name_pos != std::string::npos)
        {
            const auto opening_braces_pos = original_shader_code.find('{', name_pos + extended_function.FunctionName.size());
            if (opening_braces_pos != std::string::npos)
            {
                const auto closing_braces = [&original_shader_code, &opening_braces_pos]() -> std::size_t
                {
                    std::size_t current_depth{ 0 };
                    for (std::size_t i = opening_braces_pos; i < original_shader_code.size(); i++)
                    {
                        if (original_shader_code[i] == '{')
                        {
                            current_depth++;
                        }
                        else if (original_shader_code[i] == '}')
                        {
                            current_depth--;
                            if (current_depth == 0)
                            {
                                return i;
                            }
                        }
                    }
                    return std::string::npos;
                }();
                if (closing_braces != std::string::npos)
                {
                    const auto newline_pos = original_shader_code.rfind('\n', name_pos);
                    if (newline_pos != std::string::npos)
                    {
                        const auto space_pos = original_shader_code.find(' ', newline_pos);
                        if (space_pos != std::string::npos)
                        {
                            const auto return_type = algo::trim(original_shader_code.substr(newline_pos + 1, name_pos - newline_pos - 1));
                            const auto arg_list = [&original_shader_code, &name_pos, &opening_braces_pos]() -> std::string
                            {
                                const auto opening_parens = original_shader_code.find('(', name_pos);
                                if (opening_parens != std::string::npos)
                                {
                                    const auto closing_parens = original_shader_code.rfind(')', opening_braces_pos);
                                    if (closing_parens != std::string::npos)
                                    {
                                        const auto param_list = original_shader_code.substr(opening_parens, closing_parens - opening_parens);
                                        std::string arg_list;
                                        for (const auto param : Tokenize<','>(param_list))
                                        {
                                            const auto tokens = algo::split<' '>(param);
                                            if (!tokens.empty())
                                            {
                                                arg_list += tokens.back();
                                                arg_list += ", ";
                                            }
                                        }
                                        return arg_list;
                                    }
                                }

                                return "";
                            }();

                            std::string additional_code = fmt::format("\n\t{} return_value;", return_type);
                            for (size_t i = 0; i < extended_function.Extensions.size(); i++)
                            {
                                const ModdedFunction& modded_function = extended_function.Extensions[i];

                                const auto real_name = extended_function.FunctionName + "_ext" + std::to_string(i);
                                const auto decl = std::string{ modded_function.Declaration }.replace(
                                    modded_function.Declaration.find(extended_function.FunctionName),
                                    extended_function.FunctionName.size(),
                                    real_name);

                                additional_code += fmt::format("\n\tif ({}({}return_value))\n\t\treturn return_value;", real_name, arg_list);
                            }

                            original_shader_code.insert(opening_braces_pos + 1, additional_code);
                            for (size_t i = 0; i < extended_function.Extensions.size(); i++)
                            {
                                const ModdedFunction& modded_function = extended_function.Extensions[i];

                                const auto real_name = extended_function.FunctionName + "_ext" + std::to_string(i);
                                const auto decl = std::string{ modded_function.Declaration }.replace(
                                    modded_function.Declaration.find(extended_function.FunctionName),
                                    extended_function.FunctionName.size(),
                                    real_name);

                                original_shader_code.insert(newline_pos, modded_function.Preamble);
                                original_shader_code.insert(newline_pos, fmt::format("{} {}", decl, modded_function.Body));
                            }
                        }
                    }
                }
            }
        }
        else
        {
            LogError("Could not extend function with name '{}' into shaders. "
                     "If you are just using this mod report the issue to the mods creator. "
                     "If you developed this mod, make sure it's name matches exactly the original function's signatunamere...",
                     extended_function.FunctionName);
        }
    }

    namespace fs = std::filesystem;

    if (!fs::exists(destination_folder))
    {
        fs::create_directories(destination_folder);
    }

    const auto destination_file = destination_folder / shader_file;
    if (auto merged_shader_file = std::ofstream{ destination_file, std::ios::trunc })
    {
        merged_shader_file.write(original_shader_code.data(), original_shader_code.size());
        return true;
    }

    return false;
}
