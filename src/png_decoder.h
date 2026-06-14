// src/png_decoder.h - 添加访问方法
#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <string>
#include <vector>
#include <cstdint>
#include <zlib.h>

class PNGDecoder
{
public:
    PNGDecoder();
    ~PNGDecoder();

    bool initialize(const std::string &filename);
    bool decompressNextRow();
    void writeRowToTarget(uint8_t *target, uint32_t target_width);

    bool isFinished() const { return finished_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint32_t getCurrentRow() const { return current_row_; }
    size_t getMemoryUsage() const { return memory_usage_; }
    const std::string &getSourceFilename() const { return source_filename_; }

    // 新增：访问解码后的行数据
    uint8_t getBitDepth() const { return bit_depth_; }
    const uint8_t *getRowBuffer() const { return row_buffer_.data(); }
    const uint8_t *getOutputRow() const { return output_row_.data(); }

private:
    bool parseIHDR(const uint8_t *data);
    bool extractIDATs(FILE *fp);
    void applyPNGFilter(uint8_t filter_type);
    uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c);
    void downsample16to8();

    z_stream zstream_;
    std::vector<uint8_t> idat_data_;
    uint32_t width_;
    uint32_t height_;
    uint8_t bit_depth_;
    std::string source_filename_;
    std::vector<uint8_t> row_buffer_;
    std::vector<uint8_t> prev_row_;
    std::vector<uint8_t> output_row_;
    uint32_t current_row_;
    bool finished_;
    size_t memory_usage_;
    uint32_t bytes_per_pixel_;
};

#endif // PNG_DECODER_H
