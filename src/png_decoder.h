// src/png_decoder.h
#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <string>
#include <vector>
#include <cstdint>
#include <zlib.h>

class PNGDecoder {
public:
    PNGDecoder();
    ~PNGDecoder();
    
    // 初始化解码器，解析PNG文件
    bool initialize(const std::string& filename);
    
    // 解压一行数据
    bool decompressNextRow();
    
    // 将当前行写入目标缓冲区
    void writeRowToTarget(uint8_t* target, uint32_t target_width);
    
    // 获取解码器状态
    bool isFinished() const { return finished_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getCurrentRow() const { return current_row_; }
    size_t getMemoryUsage() const { return memory_usage_; }
    const std::string& getSourceFilename() const { return source_filename_; }
    
private:
    bool parseIHDR(const uint8_t* data);
    bool extractIDATs(FILE* fp);
    void applyPNGFilter(uint8_t filter_type);
    uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c);
    void downsample16to8(); // 新增
    
    z_stream zstream_;
    std::vector<uint8_t> idat_data_;
    uint32_t width_;
    uint32_t height_;
    uint8_t bit_depth_; // 新增：记录位深度
    std::string source_filename_;
    std::vector<uint8_t> row_buffer_;
    std::vector<uint8_t> prev_row_;
    uint32_t current_row_;
    bool finished_;
    size_t memory_usage_;
    uint32_t bytes_per_pixel_; // 现在用于输出（始终是4）
};

#endif // PNG_DECODER_H
