// src/png_encoder.cpp
#include "png_encoder.h"
#include <png.h>
#include <cstdio>
#include <vector>

bool PNGEncoder::write(const std::string& filename,
                       uint32_t width,
                       uint32_t height,
                       const uint8_t* data,
                       int compression_level) {
    printf("  使用 libpng 写入 (压缩级别: %d)\n", compression_level);
    
    // 打开文件
    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "错误: 无法打开文件: %s\n", filename.c_str());
        return false;
    }

    // 创建 PNG 写入结构
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, 
                                                   nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fprintf(stderr, "错误: 无法创建 PNG 写入结构\n");
        fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "错误: 无法创建 PNG 信息结构\n");
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(fp);
        return false;
    }

    // 错误处理
    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "错误: PNG 写入过程中发生错误\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    // 初始化IO
    png_init_io(png_ptr, fp);

    // 设置压缩级别 (0-9)
    png_set_compression_level(png_ptr, compression_level);
    
    // 对于低压缩级别，使用更快的过滤策略
    if (compression_level <= 3) {
        png_set_compression_strategy(png_ptr, Z_FILTERED);
        png_set_filter(png_ptr, 0, PNG_FILTER_SUB);
    }

    // 设置图像信息
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    // 写入信息头
    png_write_info(png_ptr, info_ptr);

    // 准备行指针
    std::vector<png_bytep> row_pointers(height);
    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = const_cast<png_bytep>(data + y * width * 4);
    }

    // 写入图像数据
    printf("  正在写入图像数据...\n");
    png_write_image(png_ptr, row_pointers.data());

    // 完成写入
    png_write_end(png_ptr, nullptr);

    // 清理
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    printf("  写入完成\n");
    return true;
}
