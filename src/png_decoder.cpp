// src/png_decoder.cpp
#include "png_decoder.h"
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
    
    // 初始化zlib
    if (inflateInit(&zstream_) != Z_OK) {
        fprintf(stderr, "inflateInit失败: %s\n", filename.c_str());
        return false;
    }
    
    zstream_.avail_in = idat_data_.size();
    zstream_.next_in = idat_data_.data();
    
    // 分配行缓冲区 - 16-bit 需要更大的缓冲区
    size_t max_row_size = width_ * 8 + 1; // 最大支持 16-bit RGBA
    row_buffer_.resize(max_row_size);
    prev_row_.resize(max_row_size);
    memset(prev_row_.data(), 0, max_row_size);
    
    // 计算内存占用
    memory_usage_ = idat_data_.size() + row_buffer_.size() + prev_row_.size() + 
                    sizeof(z_stream) + 10240; // zlib内部缓冲估计10KB
    
    return true;
}

bool PNGDecoder::extractIDATs(FILE* fp) {
    while (!feof(fp)) {
        uint8_t chunk_header[8];
        if (fread(chunk_header, 1, 8, fp) != 8) {
            break;
        }
        
        uint32_t length = read_be32(chunk_header);
        uint32_t type = read_be32(chunk_header + 4);
        
        if (type == CHUNK_IHDR) {
            std::vector<uint8_t> ihdr_data(length);
            if (fread(ihdr_data.data(), 1, length, fp) != length) {
                fprintf(stderr, "读取IHDR失败\n");
                return false;
            }
            fseek(fp, 4, SEEK_CUR); // 跳过CRC
            
            if (!parseIHDR(ihdr_data.data())) {
                return false;
            }
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
    
    if (width_ == 0 || height_ == 0) {
        fprintf(stderr, "未找到有效的IHDR\n");
        return false;
    }
    
    if (idat_data_.empty()) {
        fprintf(stderr, "未找到IDAT数据\n");
        return false;
    }
    
    return true;
}

bool PNGDecoder::parseIHDR(const uint8_t* data) {
    width_ = read_be32(data);
    height_ = read_be32(data + 4);
    uint8_t bit_depth = data[8];
    uint8_t color_type = data[9];
    
    // 支持 8-bit 和 16-bit RGBA
    if (color_type != 6) { // 6 = RGBA
        fprintf(stderr, "不支持的PNG颜色类型 (color_type=%d), 仅支持RGBA\n", color_type);
        return false;
    }
    
    if (bit_depth != 8 && bit_depth != 16) {
        fprintf(stderr, "不支持的PNG位深度 (bit_depth=%d), 仅支持8位或16位\n", bit_depth);
        return false;
    }
    
    // 记录位深度
    bit_depth_ = bit_depth;
    
    return true;
}

bool PNGDecoder::decompressNextRow() {
    if (finished_) {
        return false;
    }
    
    // 16-bit PNG 每像素 8 字节，8-bit 每像素 4 字节
    size_t bytes_per_row = (bit_depth_ == 16) ? (width_ * 8 + 1) : (width_ * 4 + 1);
    
    zstream_.next_out = row_buffer_.data();
    zstream_.avail_out = bytes_per_row;
    
    int ret = inflate(&zstream_, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        fprintf(stderr, "zlib解压失败 (ret=%d): %s\n", ret, source_filename_.c_str());
        return false;
    }
    
    if (zstream_.avail_out != 0) {
        fprintf(stderr, "解压的数据不足一行: %s\n", source_filename_.c_str());
        return false;
    }
    
    // 应用PNG filter
    uint8_t filter_type = row_buffer_[0];
    applyPNGFilter(filter_type);
    
    // 如果是 16-bit，降采样到 8-bit
    if (bit_depth_ == 16) {
        downsample16to8();
    }
    
    // 保存当前行供下一行使用
    memcpy(prev_row_.data(), row_buffer_.data(), bytes_per_row);
    
    current_row_++;
    if (current_row_ >= height_) {
        finished_ = true;
        inflateEnd(&zstream_);
        zstream_.state = nullptr;
        // 释放IDAT数据
        idat_data_.clear();
        idat_data_.shrink_to_fit();
        memory_usage_ = row_buffer_.size() + prev_row_.size();
    }
    
    return true;
}

void PNGDecoder::applyPNGFilter(uint8_t filter_type) {
    uint8_t* current = row_buffer_.data() + 1; // 跳过filter byte
    const uint8_t* prev = prev_row_.data() + 1;
    
    // 16-bit: 每像素 8 字节，8-bit: 每像素 4 字节
    size_t bpp = (bit_depth_ == 16) ? 8 : 4;
    size_t row_bytes = width_ * bpp;
    
    switch (filter_type) {
        case 0: // None
            break;
            
        case 1: // Sub
            for (size_t i = bpp; i < row_bytes; i++) {
                current[i] = (current[i] + current[i - bpp]) & 0xFF;
            }
            break;
            
        case 2: // Up
            for (size_t i = 0; i < row_bytes; i++) {
                current[i] = (current[i] + prev[i]) & 0xFF;
            }
            break;
            
        case 3: // Average
            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t left = (i >= bpp) ? current[i - bpp] : 0;
                uint8_t above = prev[i];
                current[i] = (current[i] + ((left + above) / 2)) & 0xFF;
            }
            break;
            
        case 4: // Paeth
            for (size_t i = 0; i < row_bytes; i++) {
                uint8_t left = (i >= bpp) ? current[i - bpp] : 0;
                uint8_t above = prev[i];
                uint8_t upper_left = (i >= bpp) ? prev[i - bpp] : 0;
                current[i] = (current[i] + paethPredictor(left, above, upper_left)) & 0xFF;
            }
            break;
            
        default:
            fprintf(stderr, "未知的filter类型: %d\n", filter_type);
            break;
    }
}

// 新增：16-bit 降采样到 8-bit
void PNGDecoder::downsample16to8() {
    uint8_t* src = row_buffer_.data() + 1; // 跳过 filter byte
    uint8_t* dst = row_buffer_.data() + 1;
    
    // 16-bit RGBA: 每通道 2 字节，大端序
    for (uint32_t x = 0; x < width_; x++) {
        // R: 取高字节
        dst[x * 4 + 0] = src[x * 8 + 0];
        // G: 取高字节
        dst[x * 4 + 1] = src[x * 8 + 2];
        // B: 取高字节
        dst[x * 4 + 2] = src[x * 8 + 4];
        // A: 取高字节
        dst[x * 4 + 3] = src[x * 8 + 6];
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

void PNGDecoder::writeRowToTarget(uint8_t* target, uint32_t target_width) {
    if (current_row_ == 0) return;
    
    uint32_t y = current_row_ - 1;
    const uint8_t* src = row_buffer_.data() + 1; // 跳过filter byte
    
    for (uint32_t x = 0; x < width_ && x < target_width; x++) {
        uint32_t target_offset = (y * target_width + x) * 4;
        uint32_t src_offset = x * 4;
        
        target[target_offset + 0] = src[src_offset + 0]; // R
        target[target_offset + 1] = src[src_offset + 1]; // G
        target[target_offset + 2] = src[src_offset + 2]; // B
        target[target_offset + 3] = src[src_offset + 3]; // A
    }
}
