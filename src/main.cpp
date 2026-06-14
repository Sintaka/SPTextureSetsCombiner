// src/main.cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "config_parser.h"
#include "png_decoder.h"
#include "fnmatch.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <list>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#endif

// 获取系统总内存（字节）
size_t getSystemMemory() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    return (size_t)memInfo.ullTotalPhys;
#else
    struct sysinfo info;
    sysinfo(&info);
    return (size_t)info.totalram * info.mem_unit;
#endif
}

// 扫描目录，返回匹配的文件列表
std::vector<std::string> scanDirectory(const std::string& dir, const std::string& pattern) {
    std::vector<std::string> files;
    
#ifdef _WIN32
    std::string search_path = dir + "\\*";
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &find_data);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (fnmatch(pattern.c_str(), find_data.cFileName, 0) == 0) {
                    files.push_back(dir + "\\" + find_data.cFileName);
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
                    files.push_back(dir + "/" + entry->d_name);
                }
            }
        }
        closedir(dp);
    }
#endif
    
    return files;
}

// 创建目录
bool createDirectory(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// 估算IDAT大小（快速估计，不完全解析）
size_t estimateIDATSize(const std::string& filename) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) return 0;
    
    fseek(fp, 8, SEEK_SET); // 跳过PNG签名
    
    size_t total_idat = 0;
    while (!feof(fp)) {
        uint8_t chunk_header[8];
        if (fread(chunk_header, 1, 8, fp) != 8) break;
        
        uint32_t length = ((uint32_t)chunk_header[0] << 24) | 
                         ((uint32_t)chunk_header[1] << 16) | 
                         ((uint32_t)chunk_header[2] << 8) | 
                         (uint32_t)chunk_header[3];
        uint32_t type = ((uint32_t)chunk_header[4] << 24) | 
                       ((uint32_t)chunk_header[5] << 16) | 
                       ((uint32_t)chunk_header[6] << 8) | 
                       (uint32_t)chunk_header[7];
        
        if (type == 0x49444154) { // IDAT
            total_idat += length;
        } else if (type == 0x49454E44) { // IEND
            break;
        }
        
        fseek(fp, length + 4, SEEK_CUR); // 跳过数据和CRC
    }
    
    fclose(fp);
    return total_idat;
}

// 处理单个纹理模式
void processTexturePattern(const Config& config, const TexturePattern& pattern) {
    const uint32_t TARGET_SIZE = 8192;
    const size_t TARGET_BUFFER_SIZE = TARGET_SIZE * TARGET_SIZE * 4;
    
    printf("\n处理模式: %s -> %s\n", pattern.glob_pattern.c_str(), 
           pattern.output_filename.c_str());
    
    // 分配目标缓冲区
    std::vector<uint8_t> target_buffer(TARGET_BUFFER_SIZE);
    
    // 初始化为默认像素颜色
    uint8_t default_r = pattern.default_pixel & 0xFF;
    uint8_t default_g = (pattern.default_pixel >> 8) & 0xFF;
    uint8_t default_b = (pattern.default_pixel >> 16) & 0xFF;
    uint8_t default_a = (pattern.default_pixel >> 24) & 0xFF;
    
    for (size_t i = 0; i < TARGET_SIZE * TARGET_SIZE; i++) {
        target_buffer[i * 4 + 0] = default_r;
        target_buffer[i * 4 + 1] = default_g;
        target_buffer[i * 4 + 2] = default_b;
        target_buffer[i * 4 + 3] = default_a;
    }
    
    // 扫描匹配的文件
    std::vector<std::string> matching_files = scanDirectory(config.source_dir, 
                                                            pattern.glob_pattern);
    
    if (matching_files.empty()) {
        printf("警告: 未找到匹配的文件\n");
        return;
    }
    
    printf("找到 %zu 个匹配文件\n", matching_files.size());
    
    // 获取系统内存
    size_t system_memory = getSystemMemory();
    size_t max_memory = (size_t)(system_memory * 0.8);
    size_t current_batch_memory = TARGET_BUFFER_SIZE;
    
    printf("系统内存: %.2f GB, 最大使用: %.2f GB\n", 
           system_memory / (1024.0 * 1024.0 * 1024.0),
           max_memory / (1024.0 * 1024.0 * 1024.0));
    
    // 活跃解码器列表
    std::list<std::unique_ptr<PNGDecoder>> active_decoders;
    std::list<std::string> pending_files(matching_files.begin(), matching_files.end());
    
    size_t total_files = matching_files.size();
    size_t processed_files = 0;
    
    // 主处理循环
    while (!pending_files.empty() || !active_decoders.empty()) {
        // 尝试加载新文件
        while (!pending_files.empty() && current_batch_memory < max_memory) {
            std::string file = pending_files.front();
            
            // 估算IDAT大小
            size_t idat_size = estimateIDATSize(file);
            size_t decoder_memory = idat_size + 138 * 1024; // IDAT + 138KB开销
            
            if (current_batch_memory + decoder_memory > max_memory) {
                break; // 内存不足，等待现有解码器完成
            }
            
            // 创建解码器
            auto decoder = std::make_unique<PNGDecoder>();
            if (decoder->initialize(file)) {
                current_batch_memory += decoder->getMemoryUsage();
                active_decoders.push_back(std::move(decoder));
                printf("加载: %s (内存: %.2f MB, 总计: %.2f MB)\n", 
                       file.c_str(),
                       decoder_memory / (1024.0 * 1024.0),
                       current_batch_memory / (1024.0 * 1024.0));
            } else {
                fprintf(stderr, "警告: 无法初始化解码器: %s\n", file.c_str());
            }
            
            pending_files.pop_front();
        }
        
        // 轮询所有活跃解码器解压一行
        for (auto it = active_decoders.begin(); it != active_decoders.end(); ) {
            auto& decoder = *it;
            
            if (decoder->isFinished()) {
                ++it;
                continue;
            }
            
            // 解压下一行
            if (decoder->decompressNextRow()) {
                // 写入目标缓冲区
                decoder->writeRowToTarget(target_buffer.data(), TARGET_SIZE);
                
                // 检查是否完成
                if (decoder->isFinished()) {
                    processed_files++;
                    size_t released_memory = decoder->getMemoryUsage();
                    current_batch_memory -= released_memory;
                    
                    printf("完成: %s [%zu/%zu] (释放: %.2f MB)\n", 
                           decoder->getSourceFilename().c_str(),
                           processed_files, total_files,
                           released_memory / (1024.0 * 1024.0));
                }
            } else {
                fprintf(stderr, "警告: 解压失败: %s\n", 
                        decoder->getSourceFilename().c_str());
                processed_files++;
                size_t released_memory = decoder->getMemoryUsage();
                current_batch_memory -= released_memory;
            }
            
            ++it;
        }
        
        // 移除已完成的解码器
        active_decoders.remove_if([](const std::unique_ptr<PNGDecoder>& d) {
            return d->isFinished();
        });
    }
    
    // 写入输出文件
    std::string output_path = config.destination_dir + "\\" + pattern.output_filename;
    
    printf("写入输出: %s\n", output_path.c_str());
    
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
