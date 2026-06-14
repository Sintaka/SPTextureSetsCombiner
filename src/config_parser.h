// src/config_parser.h
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <string>
#include <vector>
#include <cstdint>

struct TexturePattern {
    std::string glob_pattern;
    std::string output_filename;
    uint32_t default_pixel; // RGBA格式
};

struct Config {
    std::string source_dir;
    std::string destination_dir;
    std::string base_name;
    std::vector<TexturePattern> texture_patterns;
};

class ConfigParser {
public:
    static bool parse(const std::string& filename, Config& config);
    
private:
    static std::string trim(const std::string& str);
    static std::string unquote(const std::string& str);
    static bool parseHexColor(const std::string& hex, uint32_t& color);
    static bool parseTexturePatterns(const std::string& content, 
                                    size_t& pos, 
                                    std::vector<TexturePattern>& patterns);
};

#endif // CONFIG_PARSER_H
