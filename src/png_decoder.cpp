// src/png_decoder.cpp - 完全重写
#include "png_decoder.h"
#include <png.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// PNG文件签名
static const uint8_t PNG_SIGNATURE[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// Chunk类型
static const uint32_t CHUNK_IHDR = 0x49484452;
static const uint32_t CHUNK_IDAT = 0x49444154;
static const uint32_t CHUNK_IEND = 0x49454E44;

// 大端序转换
static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

PNGDecoder::PNGDecoder() 
    : width_(0), height_(0), bit_depth_(8), current_row_(0), finished_(false), 
      memory_usage_(0), bytes_per_pixel_(4) {
    memset(&zstream_, 0, sizeof(zstream_));
}

PNGDecoder::~PNGDecoder() {
    if (zstream_.state) {
        inflateEnd(&zstream_);
    }
}

bool PNGDecoder::initialize(const std::string& filename) {
    source_filename_ = filename;
    
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "无法打开文件: %s\n", filename.c_str());
        return false;
    }
    
    // 验证PNG签名
    uint8_t signature[8];
    if (fread(signature, 1, 8, fp) != 8 || memcmp(signature, PNG_SIGNATURE, 8) != 0) {
        fprintf(stderr, "无效的PNG签名: %s\n", filename.c_str());
        fclose(fp);
        return false;
    }
    
    // 提取IHDR和IDAT块
    if (!extractIDATs(fp)) {
        fclose(fp);
        return false;
    }
    
    fclose(fp);
    
    if (idat_data_.empty()) {
        fprintf(stderr, "未找到IDAT数据: %s\n", filename.c_str());
        return false;
    }
    
    // 初始化zlib
    memset(&zstream_, 0, sizeof(zstream_));
    if (inflateInit(&zstream_) != Z_OK) {
        fprintf(stderr, "inflateInit失败: %s\n", filename.c_str());
        return false;
    }
    
    zstream_.avail_in = idat_data_.size();
    zstream_.next_in = idat_data_.data();
    
    // 计算每行的原始字节数（解压后，filter前）
    // 16-bit RGBA: width * 4 channels * 2 bytes + 1 filter byte
    // 8-bit RGBA: width * 4 channels * 1 byte + 1 filter byte
    size_t bytes_per_channel = (bit_depth_ == 16) ? 2 : 1;
    size_t raw_row_bytes = width_ * 4 * bytes_per_channel + 1;
    
    row_buffer_.resize(raw_row_bytes);
    prev_row_.resize(raw_row_bytes);
    memset(prev_row_.data(), 0, raw_row_bytes);
    
    // 如果是16-bit，还需要一个8-bit的输出缓冲区
    if (bit_depth_ == 16) {
        output_row_.resize(width_ * 4);
    }
    
    // 计算内存占用
    memory_usage_ = idat_data_.size() + row_buffer_.size() + prev_row_.size() + 
                    output_row_.size() + sizeof(z_stream) + 10240;
    
    printf("  初始化解码器: %s (%dx%d, %d-bit)\n", 
           filename.c_str(), width_, height_, bit_depth_);
    
    return true;
}

bool PNGDecoder::extractIDATs(FILE* fp) {
    bool found_ihdr = false;
    
    while (!feof(fp)) {
        uint8_t chunk_header[8];
        if (fread(chunk_header, 1, 8, fp) != 8) {
            break;
        }
        
        uint32_t length = read_be32(chunk_header);
        uint32_t type = read_be32(chunk_header + 4);
        
        if (type == CHUNK_IHDR) {
            if (length < 13) {
                fprintf(stderr, "IHDR长度无效\n");
                return false;
            }
            
            std::vector<uint8_t> ihdr_data(length);
            if (fread(ihdr_data.data(), 1, length, fp) != length) {
                fprintf(stderr, "读取IHDR失败\n");
                return false;
            }
            fseek(fp, 4, SEEK_CUR); // 跳过CRC
            
            if (!parseIHDR(ihdr_data.data())) {
                return false;
            }
            found_ihdr = true;
            
        } else if (type == CHUNK_IDAT) {
            size_t old_size = idat_data_.size();
            idat_data_.resize(old_size + length);
            if (fread(idat_data_.data() + old_size, 1, length, fp) != length) {
                fprintf(stderr, "读取IDAT失败\n");
                return false;
            }
            fseek(fp, 4, SEEK_CUR); // 跳过CRC
            
        } else if (type == CHUNK_IEND) {
            break;
            
        } else {
            // 跳过其他chunk
            fseek(fp, length + 4, SEEK_CUR);
        }
    }
    
    if (!found_ihdr) {
        fprintf(stderr, "未找到IHDR块\n");
        return false;
    }
    
    return true;
}

bool PNGDecoder::parseIHDR(const uint8_t* data) {
    width_ = read_be32(data);
    height_ = read_be32(data + 4);
    bit_depth_ = data[8];
    uint8_t color_type = data[9];
    
    // 只支持 RGBA (color_type = 6)
    if (color_type != 6) {
        fprintf(stderr, "不支持的PNG颜色类型 %d (仅支持RGBA)\n", color_type);
        return false;
    }
    
    // 支持 8-bit 和 16-bit
    if (bit_depth_ != 8 && bit_depth_ != 16) {
        fprintf(stderr, "不支持的PNG位深度 %d (仅支持8或16)\n", bit_depth_);
        return false;
    }
    
    return true;
}

bool PNGDecoder::decompressNextRow() {
    if (finished_) {
        return false;
    }
    
    // 解压一行原始数据
    zstream_.next_out = row_buffer_.data();
    zstream_.avail_out = row_buffer_.size();
    
    int ret = inflate(&zstream_, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        fprintf(stderr, "inflate失败 (ret=%d): %s\n", ret, source_filename_.c_str());
        finished_ = true;
        return false;
    }
    
    // 检查是否解压了完整的一行
    if (zstream_.avail_out != 0) {
        fprintf(stderr, "行数据不完整 (缺少%u字节): %s\n", 
                zstream_.avail_out, source_filename_.c_str());
        finished_ = true;
        return false;
    }
    
    // 应用PNG filter还原
    uint8_t filter_type = row_buffer_[0];
    
    // 调试输出
    if (current_row_ == 0) {
        printf("    解码器: filter_type=%u, row_buffer前10字节: ", filter_type);
        for (int i = 0; i < 10 && i < (int)row_buffer_.size(); i++) {
            printf("%02X ", row_buffer_[i]);
        }
        printf("\n");
    }
    
    applyPNGFilter(filter_type);
    
    // 16-bit降采样到8-bit
    if (bit_depth_ == 16) {
        downsample16to8();
    }
    
    // 保存当前行供下一行filter使用 - 必须在递增current_row_之前
    memcpy(prev_row_.data(), row_buffer_.data(), row_buffer_.size());
    
    current_row_++;
    
    // 检查是否完成
    if (current_row_ >= height_) {
        finished_ = true;
        inflateEnd(&zstream_);
        zstream_.state = nullptr;
        
        idat_data_.clear();
        idat_data_.shrink_to_fit();
        memory_usage_ = row_buffer_.size() + prev_row_.size() + output_row_.size();
    }
    
    return true;
}

void PNGDecoder::applyPNGFilter(uint8_t filter_type) {
    uint8_t* current = row_buffer_.data() + 1; // 跳过filter byte
    const uint8_t* prev = prev_row_.data() + 1;
    
    // 每个像素的字节数
    size_t bpp = (bit_depth_ == 16) ? 8 : 4; // 16-bit: 4通道*2字节, 8-bit: 4通道*1字节
    size_t row_bytes = width_ * bpp;
    
    switch (filter_type) {
        case 0: // None - 无需处理
            break;
            
        case 1: // Sub - 当前字节 += 左侧字节
            for (size_t i = bpp; i < row_bytes; i++) {
                current[i] = (current[i] + current[i - bpp]) & 0xFF;
            }
            break;
            
        case 2: // Up - 当前字节 += 上方字节
            for (size_t i = 0; i < row_bytes; i++) {
                current[i] = (current[i] + prev[i]) & 0xFF;
            }
            break;
            
        case 3: // Average - 当前字节 += (左侧 + 上方) / 2
            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t left = (i >= bpp) ? current[i - bpp] : 0;
                uint8_t above = prev[i];
                current[i] = (current[i] + ((left + above) / 2)) & 0xFF;
            }
            break;
            
        case 4: // Paeth - 使用Paeth预测器
            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t left = (i >= bpp) ? current[i - bpp] : 0;
                uint8_t above = prev[i];
                uint8_t upper_left = (i >= bpp) ? prev[i - bpp] : 0;
                current[i] = (current[i] + paethPredictor(left, above, upper_left)) & 0xFF;
            }
            break;
            
        default:
            fprintf(stderr, "未知的filter类型 %d\n", filter_type);
            break;
    }
}

uint8_t PNGDecoder::paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

void PNGDecoder::downsample16to8() {
    const uint8_t* src = row_buffer_.data() + 1; // 跳过filter byte
    
    // 16-bit PNG是大端序：每个通道2字节，高字节在前
    for (uint32_t x = 0; x < width_; x++) {
        output_row_[x * 4 + 0] = src[x * 8 + 0]; // R 高字节
        output_row_[x * 4 + 1] = src[x * 8 + 2]; // G 高字节
        output_row_[x * 4 + 2] = src[x * 8 + 4]; // B 高字节
        output_row_[x * 4 + 3] = src[x * 8 + 6]; // A 高字节
    }
}

void PNGDecoder::writeRowToTarget(uint8_t* target, uint32_t target_width) {
    if (current_row_ == 0) {
        return; // 还没解压任何行
    }
    
    uint32_t y = current_row_ - 1; // 当前行索引
    
    // 获取源数据指针
    const uint8_t* src;
    if (bit_depth_ == 16) {
        src = output_row_.data(); // 使用降采样后的8-bit数据
    } else {
        src = row_buffer_.data() + 1; // 8-bit数据，跳过filter byte
    }
    
    // 写入目标缓冲区
    // 目标缓冲区是 8192x8192 RGBA
    // 只写入图片实际尺寸范围内的像素
    if (y >= target_width) {
        return; // 超出目标高度
    }
    
    uint32_t copy_width = (width_ < target_width) ? width_ : target_width;
    
    for (uint32_t x = 0; x < copy_width; x++) {
        uint32_t target_offset = (y * target_width + x) * 4;
        uint32_t src_offset = x * 4;
        
        target[target_offset + 0] = src[src_offset + 0]; // R
        target[target_offset + 1] = src[src_offset + 1]; // G
        target[target_offset + 2] = src[src_offset + 2]; // B
        target[target_offset + 3] = src[src_offset + 3]; // A
    }
}
