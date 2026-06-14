// src/config_parser.cpp
#include "config_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

std::string ConfigParser::trim(const std::string& str) {
    size_t start = 0;
    while (start < str.length() && std::isspace(str[start])) start++;
    
    size_t end = str.length();
    while (end > start && std::isspace(str[end - 1])) end--;
    
    return str.substr(start, end - start);
}

std::string ConfigParser::unquote(const std::string& str) {
    std::string s = trim(str);
    
    // 移除 r"..." 或 "..."
    if (s.length() >= 3 && s[0] == 'r' && s[1] == '"' && s.back() == '"') {
        return s.substr(2, s.length() - 3);
    }
    if (s.length() >= 2 && s[0] == '"' && s.back() == '"') {
        return s.substr(1, s.length() - 2);
    }
    
    return s;
}

bool ConfigParser::parseHexColor(const std::string& hex, uint32_t& color) {
    std::string h = trim(hex);
    
    // 移除引号
    if (!h.empty() && h[0] == '"') {
        h = h.substr(1);
    }
    if (!h.empty() && h.back() == '"') {
        h = h.substr(0, h.length() - 1);
    }
    
    if (h.empty() || h[0] != '#') {
        return false;
    }
    
    h = h.substr(1); // 移除 #
    
    // #RRGGBB or #RRGGBBAA
    if (h.length() == 6) {
        h += "FF"; // 默认alpha=FF
    } else if (h.length() != 8) {
        return false;
    }
    
    // 转换为整数
    try {
        unsigned long value = std::stoul(h, nullptr, 16);
        
        // 转换为RGBA格式 (内存布局)
        uint8_t r = (value >> 24) & 0xFF;
        uint8_t g = (value >> 16) & 0xFF;
        uint8_t b = (value >> 8) & 0xFF;
        uint8_t a = value & 0xFF;
        
        color = (r << 0) | (g << 8) | (b << 16) | (a << 24);
        return true;
    } catch (...) {
        return false;
    }
}

// src/config_parser.cpp - 修复 BASE_NAME 替换逻辑
bool ConfigParser::parseTexturePatterns(const std::string& content, 
                                       size_t& pos, 
                                       std::vector<TexturePattern>& patterns) {
    // 找到 [
    size_t start = content.find('[', pos);
    if (start == std::string::npos) {
        return false;
    }
    
    // 找到对应的 ]
    size_t end = content.find(']', start);
    if (end == std::string::npos) {
        return false;
    }
    
    std::string list_content = content.substr(start + 1, end - start - 1);
    
    // 解析每个元组
    size_t tuple_start = 0;
    while (tuple_start < list_content.length()) {
        // 跳过空白和逗号
        while (tuple_start < list_content.length() && 
               (std::isspace(list_content[tuple_start]) || list_content[tuple_start] == ',')) {
            tuple_start++;
        }
        
        if (tuple_start >= list_content.length()) break;
        
        // 找到 (
        if (list_content[tuple_start] != '(') {
            tuple_start++;
            continue;
        }
        
        size_t tuple_end = list_content.find(')', tuple_start);
        if (tuple_end == std::string::npos) {
            break;
        }
        
        std::string tuple_content = list_content.substr(tuple_start + 1, tuple_end - tuple_start - 1);
        
        // 解析三个字段 - 改进：正确处理带引号的字符串
        std::vector<std::string> fields;
        size_t field_start = 0;
        bool in_quotes = false;
        std::string current_field;
        
        for (size_t i = 0; i < tuple_content.length(); i++) {
            char c = tuple_content[i];
            
            if (c == '"' && (i == 0 || tuple_content[i-1] != '\\')) {
                in_quotes = !in_quotes;
                current_field += c; // 保留引号
            } else if (c == ',' && !in_quotes) {
                fields.push_back(trim(current_field));
                current_field.clear();
            } else {
                current_field += c;
            }
        }
        
        if (!current_field.empty()) {
            fields.push_back(trim(current_field));
        }
        
        if (fields.size() == 3) {
            TexturePattern pattern;
            pattern.glob_pattern = unquote(fields[0]);
            pattern.output_filename = unquote(fields[1]);
            
            if (!parseHexColor(fields[2], pattern.default_pixel)) {
                fprintf(stderr, "警告: 无效的颜色格式 '%s', 使用默认值 #000000FF\n", 
                        fields[2].c_str());
                pattern.default_pixel = 0x000000FF;
            }
            
            patterns.push_back(pattern);
        }
        
        tuple_start = tuple_end + 1;
    }
    
    pos = end + 1;
    return true;
}

bool ConfigParser::parse(const std::string& filename, Config& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        fprintf(stderr, "无法打开配置文件: %s\n", filename.c_str());
        return false;
    }
    
    // 读取整个文件
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // 逐行解析
    std::istringstream stream(content);
    std::string line;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // 查找 =
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        
        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));
        
        if (key == "SOURCE_DIR") {
            config.source_dir = unquote(value);
        } else if (key == "DESTINATION_DIR") {
            config.destination_dir = unquote(value);
        } else if (key == "BASE_NAME") {
            config.base_name = unquote(value);
        } else if (key == "TEXTURE_PATTERNS") {
            size_t pos = 0;
            if (!parseTexturePatterns(content, pos, config.texture_patterns)) {
                fprintf(stderr, "解析TEXTURE_PATTERNS失败\n");
                return false;
            }
        }
    }
    
    // 替换 BASE_NAME - 修复：正确处理 Python 风格的字符串拼接
    for (auto& pattern : config.texture_patterns) {
        std::string& filename = pattern.output_filename;
        
        // 替换 "BASE_NAME" (带引号)
        size_t pos = 0;
        while ((pos = filename.find("BASE_NAME", pos)) != std::string::npos) {
            filename.replace(pos, 9, config.base_name);
            pos += config.base_name.length();
        }
        
        // 清理可能残留的 Python 字符串拼接符号 (+ 和多余的引号)
        // 例如：Friston-3 + "_Diffuse..." -> Friston-3_Diffuse...
        pos = 0;
        while ((pos = filename.find(" + \"", pos)) != std::string::npos) {
            filename.erase(pos, 4); // 删除 ' + "'
        }
        
        pos = 0;
        while ((pos = filename.find("\" + ", pos)) != std::string::npos) {
            filename.erase(pos, 4); // 删除 '" + '
        }
        
        // 移除多余的引号
        if (!filename.empty() && filename.front() == '"') {
            filename.erase(0, 1);
        }
        if (!filename.empty() && filename.back() == '"') {
            filename.pop_back();
        }
    }
    
    // 验证必需字段
    if (config.source_dir.empty()) {
        fprintf(stderr, "缺少SOURCE_DIR配置\n");
        return false;
    }
    if (config.destination_dir.empty()) {
        fprintf(stderr, "缺少DESTINATION_DIR配置\n");
        return false;
    }
    if (config.base_name.empty()) {
        fprintf(stderr, "缺少BASE_NAME配置\n");
        return false;
    }
    
    return true;
}
