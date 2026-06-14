// src/main.cpp - 重写处理逻辑
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <list>
#include <memory>
#include <algorithm>
#include "png_decoder.h"
#include "config_parser.h"
#include <fnmatch.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEPARATOR '\\'
#else
#include <sys/stat.h>
#include <dirent.h>
#define PATH_SEPARATOR '/'
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// 目标纹理尺寸 8192x8192 RGBA
constexpr uint32_t TARGET_SIZE = 8192;
constexpr size_t TARGET_BUFFER_SIZE = TARGET_SIZE * TARGET_SIZE * 4;

// 内存预算：系统内存的80%
size_t getMemoryBudget() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return static_cast<size_t>(status.ullTotalPhys * 0.8);
#else
    return 8ULL * 1024 * 1024 * 1024; // 默认8GB
#endif
}

bool createDirectory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

std::vector<std::string> findMatchingFiles(const std::string& dir, const std::string& pattern) {
    std::vector<std::string> results;
    
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    std::string search_path = dir + "\\*";
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string filename = find_data.cFileName;
                if (fnmatch(pattern.c_str(), filename.c_str(), 0) == 0) {
                    results.push_back(dir + PATH_SEPARATOR + filename);
                }
            }
        } while (FindNextFileA(hFind, &find_data));
        FindClose(hFind);
    }
#else
    DIR* dp = opendir(dir.c_str());
    if (dp) {
        struct dirent* entry;
        while ((entry = readdir(dp)) != nullptr) {
            if (entry->d_type == DT_REG) {
                if (fnmatch(pattern.c_str(), entry->d_name, 0) == 0) {
                    results.push_back(dir + PATH_SEPARATOR + entry->d_name);
                }
            }
        }
        closedir(dp);
    }
#endif
    
    return results;
}

// 单个解码器的状态
struct DecoderState {
    std::unique_ptr<PNGDecoder> decoder;
    uint32_t target_x;  // 在目标缓冲区中的起始X坐标
    uint32_t target_y;  // 在目标缓冲区中的起始Y坐标
    std::string filename;
};

void processTexturePattern(const Config& config, const TexturePattern& pattern) {
    printf("\n处理纹理模式: %s -> %s\n", 
           pattern.glob_pattern.c_str(), 
           pattern.output_filename.c_str());
    
    // 查找匹配的文件
    std::vector<std::string> files = findMatchingFiles(config.source_dir, pattern.glob_pattern);
    
    if (files.empty()) {
        fprintf(stderr, "警告: 没有找到匹配的文件: %s\n", pattern.glob_pattern.c_str());
        return;
    }
    
    printf("找到 %zu 个匹配的文件\n", files.size());
    
    // 解析文件名，提取UDIM坐标
    struct TileInfo {
        std::string filepath;
        uint32_t u;
        uint32_t v;
    };
    
    std::vector<TileInfo> tiles;
    
    for (const auto& file : files) {
        size_t last_sep = file.find_last_of("/\\");
        std::string filename = (last_sep != std::string::npos) ? 
                               file.substr(last_sep + 1) : file;
        
        size_t dot_pos = filename.rfind('.');
        if (dot_pos == std::string::npos) continue;
        
        std::string before_ext = filename.substr(0, dot_pos);
        size_t udim_pos = before_ext.rfind(".1");
        
        if (udim_pos != std::string::npos && udim_pos + 6 <= before_ext.length()) {
            std::string udim_str = before_ext.substr(udim_pos + 1, 4);
            if (udim_str.length() == 4 && udim_str[0] == '1' && 
                isdigit(udim_str[1]) && isdigit(udim_str[2]) && isdigit(udim_str[3])) {
                
                int udim = atoi(udim_str.c_str());
                uint32_t u = (udim - 1001) % 10;
                uint32_t v = (udim - 1001) / 10;
                
                tiles.push_back({file, u, v});
                printf("  %s -> UDIM %d -> UV(%u, %u)\n", 
                       filename.c_str(), udim, u, v);
            }
        }
    }
    
    if (tiles.empty()) {
        fprintf(stderr, "警告: 没有找到有效的UDIM文件\n");
        return;
    }
    
    // 创建目标缓冲区并用默认颜色填充
    std::vector<uint8_t> target_buffer(TARGET_BUFFER_SIZE);
    uint32_t default_color = pattern.default_pixel;
    
    // 修正：0xRRGGBBAA 格式
    uint8_t a = (default_color >> 24) & 0xFF;
    uint8_t b = (default_color >> 16) & 0xFF;
    uint8_t g = (default_color >> 8) & 0xFF;
    uint8_t r = default_color & 0xFF;
    
    printf("默认颜色: 0x%08X -> R=%u G=%u B=%u A=%u\n", default_color, r, g, b, a);
    
    for (size_t i = 0; i < TARGET_SIZE * TARGET_SIZE; i++) {
        target_buffer[i * 4 + 0] = r;
        target_buffer[i * 4 + 1] = g;
        target_buffer[i * 4 + 2] = b;
        target_buffer[i * 4 + 3] = a;
    }
    
    printf("初始化目标缓冲区: %.2f MB\n", 
           TARGET_BUFFER_SIZE / (1024.0 * 1024.0));
    
    // 逐个处理每个tile
    for (size_t tile_idx = 0; tile_idx < tiles.size(); tile_idx++) {
        const auto& tile = tiles[tile_idx];
        
        printf("\n[%zu/%zu] 处理: %s (UV: %u,%u)\n", 
               tile_idx + 1, tiles.size(), tile.filepath.c_str(), tile.u, tile.v);
        
        // 创建解码器
        auto decoder = std::make_unique<PNGDecoder>();
        if (!decoder->initialize(tile.filepath)) {
            fprintf(stderr, "错误: 无法初始化解码器\n");
            continue;
        }
        
        uint32_t tile_width = decoder->getWidth();
        uint32_t tile_height = decoder->getHeight();
        
        // 计算在目标缓冲区中的位置
        uint32_t target_x = tile.u * 1024;
        uint32_t target_y = tile.v * 1024;
        
        printf("  尺寸: %ux%u, 目标位置: (%u, %u)\n",
               tile_width, tile_height, target_x, target_y);
        
        // 逐行解压并写入
        uint32_t rows_processed = 0;
        uint32_t pixels_written = 0;
        
        while (!decoder->isFinished()) {
            if (!decoder->decompressNextRow()) {
                fprintf(stderr, "错误: 解压失败于行 %u\n", rows_processed);
                break;
            }
            
            // 获取当前行数据
            const uint8_t* row_data;
            if (decoder->getBitDepth() == 16) {
                row_data = decoder->getOutputRow();
            } else {
                row_data = decoder->getRowBuffer() + 1;
            }
            
            // 写入目标缓冲区 - 只写入非透明像素
            uint32_t y = target_y + rows_processed;
            if (y >= TARGET_SIZE) break;
            
            for (uint32_t x = 0; x < tile_width && (target_x + x) < TARGET_SIZE; x++) {
                uint32_t src_offset = x * 4;
                uint8_t src_alpha = row_data[src_offset + 3];
                
                // 只有当源像素的 Alpha > 0 时才写入
                if (src_alpha > 0) {
                    uint32_t target_offset = (y * TARGET_SIZE + (target_x + x)) * 4;
                    
                    target_buffer[target_offset + 0] = row_data[src_offset + 0];
                    target_buffer[target_offset + 1] = row_data[src_offset + 1];
                    target_buffer[target_offset + 2] = row_data[src_offset + 2];
                    target_buffer[target_offset + 3] = row_data[src_offset + 3];
                    
                    pixels_written++;
                }
            }
            
            rows_processed++;
        }
        
        printf("  完成: %u 行, 写入 %u 个非透明像素\n", rows_processed, pixels_written);
        
        // 验证写入：查找第一个非透明像素
        bool found_pixel = false;
        for (uint32_t check_y = target_y; check_y < target_y + tile_height && check_y < TARGET_SIZE; check_y++) {
            for (uint32_t check_x = target_x; check_x < target_x + tile_width && check_x < TARGET_SIZE; check_x++) {
                uint32_t check_offset = (check_y * TARGET_SIZE + check_x) * 4;
                if (target_buffer[check_offset + 3] > 0) {
                    printf("  验证: 找到第一个非透明像素在 (%u,%u), RGBA=(%u,%u,%u,%u)\n",
                           check_x, check_y,
                           target_buffer[check_offset + 0],
                           target_buffer[check_offset + 1],
                           target_buffer[check_offset + 2],
                           target_buffer[check_offset + 3]);
                    found_pixel = true;
                    break;
                }
            }
            if (found_pixel) break;
        }
        
        if (!found_pixel) {
            printf("  警告: 在目标区域未找到非透明像素\n");
        }
    }
    
    // 最终统计
    printf("\n最终统计:\n");
    uint32_t total_non_transparent = 0;
    for (size_t i = 0; i < TARGET_SIZE * TARGET_SIZE; i++) {
        if (target_buffer[i * 4 + 3] > 0) {
            total_non_transparent++;
        }
    }
    printf("  总非透明像素数: %u / %u (%.2f%%)\n", 
           total_non_transparent, TARGET_SIZE * TARGET_SIZE,
           100.0 * total_non_transparent / (TARGET_SIZE * TARGET_SIZE));
    
    // 写入输出文件
    std::string output_path = config.destination_dir + PATH_SEPARATOR + pattern.output_filename;
    
    printf("\n写入输出: %s\n", output_path.c_str());
    
    if (!stbi_write_png(output_path.c_str(), TARGET_SIZE, TARGET_SIZE, 4, 
                        target_buffer.data(), TARGET_SIZE * 4)) {
        fprintf(stderr, "错误: 无法写入PNG文件: %s\n", output_path.c_str());
    } else {
        printf("成功写入: %s (%.2f MB)\n", output_path.c_str(), 
               TARGET_BUFFER_SIZE / (1024.0 * 1024.0));
    }
}

int main(int argc, char* argv[]) {
    std::string config_file = "config.ini";
    
    if (argc > 1) {
        config_file = argv[1];
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  PNG 并发材质合并程序\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    // 解析配置文件
    Config config;
    if (!ConfigParser::parse(config_file, config)) {
        fprintf(stderr, "错误: 配置文件解析失败\n");
        return 1;
    }
    
    printf("配置加载成功:\n");
    printf("  源目录: %s\n", config.source_dir.c_str());
    printf("  目标目录: %s\n", config.destination_dir.c_str());
    printf("  基础名称: %s\n", config.base_name.c_str());
    printf("  纹理模式数量: %zu\n", config.texture_patterns.size());
    
    // 创建目标目录
    if (!createDirectory(config.destination_dir)) {
        fprintf(stderr, "警告: 无法创建目标目录: %s\n", 
                config.destination_dir.c_str());
    }
    
    // 处理每个纹理模式
    for (const auto& pattern : config.texture_patterns) {
        processTexturePattern(config, pattern);
    }
    
    printf("\n═══════════════════════════════════════════════════════════════\n");
    printf("  所有纹理处理完成\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    return 0;
}
