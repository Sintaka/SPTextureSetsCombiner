// src/png_encoder.h
#pragma once

#include "png_encoder.h"
#include <png.h>
#include <zlib.h>
#include <cstdint>
#include <string>
#include <vector>

class PNGEncoder {
public:
    PNGEncoder() = default;
    ~PNGEncoder() = default;

    // 写入PNG文件
    // compression_level: 0-9, 0=无压缩(最快), 9=最高压缩(最慢), 推荐1-3
    bool write(const std::string& filename,
               uint32_t width,
               uint32_t height,
               const uint8_t* data,
               int compression_level = 1);
};
