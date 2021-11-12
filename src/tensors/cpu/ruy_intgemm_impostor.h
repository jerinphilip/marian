// @jerinphilip is sorry about this abomination. This should never have had to
// exist.

#pragma once
#include <cstdint>
#include "3rd_party/intgemm/intgemm/callbacks/configs.h"
#include "wasm_arm_ruy_implementation.h"

namespace intgemm {
struct MeanStd {
  float mean;
  float stddev;
};

static inline MeanStd VectorMeanStd(const float *begin_float,
                                    const float *end_float,
                                    bool absolute) {
  // FIXME
  return MeanStd{0, 1};
}

struct TileInfo {
  const Index a_rows;
  const Index a_cols;
  const Index b_rows;
  const Index b_cols;
};

template <class _Integer>
struct IntImpl {
  using Integer = _Integer;

  // A's size must be a multiple of 1x64, B's size must be a multiple of 64x8.
  static constexpr TileInfo tile_info{1, 64, 64, 8};

  // Currently A is prepared by quantization but this could theoretically
  // change. A's columns must be a multiple of 8. The number of rows is
  // anything.
  static inline void PrepareA(const float *input,
                              Integer *output,
                              float quant_mult,
                              Index rows,
                              Index cols) {}

  // Warning: the output of PrepareB depends on the CPU.
  // It will match the Multiply function on the same CPU though.
  static void PrepareB(const float *input,
                       Integer *output,
                       float quant_mult,
                       Index rows,
                       Index cols) {}

  // Convert from a B that was already transposed (routine not provided) and
  // quantized (e.g. with Quantize) to the CPU-dependent format used for
  // Multiply.  This is useful for storing a quantized model on disk then in a
  // CPU-independent fashion.
  static void PrepareBQuantizedTransposed(const Integer *input,
                                          Integer *output,
                                          Index inner,
                                          Index B_untransposed_cols) {}

  // Convert from a B that was already transposed (routine not provided) to
  // the CPU-dependent format used for Multiply.  This is useful for storing
  // a quantized model on disk then in a CPU-independent fashion.
  static void PrepareBTransposed(const float *input,
                                 Integer *output,
                                 float quant_mul,
                                 Index inner,
                                 Index B_untransposed_cols) {}
  template <class Callback>
  static void PrepareBias(const Integer *B, Integer width, Index B_cols, Callback callback) {}

  // Select columns from a prepared B matrix.  The number of selected columns
  // must be a multiple of 8.
  static void SelectColumnsB(const Integer *input,
                             Integer *output,
                             Index rows,
                             const Index *cols_begin,
                             const Index *cols_end) {}

  // Multiply C = A * B, presuming A and B have been prepared.
  template <class Callback>
  static void Multiply(const Integer *A,
                       const Integer *B,
                       Index A_rows,
                       Index width,
                       Index B_cols,
                       Callback callback) {}

  static const char *const kName;
};

struct Int8 {
  using Integer = int8_t;

  // A's size must be a multiple of 1x64, B's size must be a multiple of 64x8.
  static constexpr TileInfo tile_info{1, 64, 64, 8};

  // Currently A is prepared by quantization but this could theoretically
  // change. A's columns must be a multiple of 8. The number of rows is
  // anything.
  static inline void PrepareA(const float *input,
                              Integer *output,
                              float quant_mult,
                              Index rows,
                              Index cols) {
    int8PrepareA(input, /*scale=*/quant_mult, /*zero_point=*/0, rows, cols, output);
  }

  // Warning: the output of PrepareB depends on the CPU.
  // It will match the Multiply function on the same CPU though.
  static void PrepareB(const float *input,
                       Integer *output,
                       float quant_mult,
                       Index rows,
                       Index cols) {
    int8PrepareB(input, /*scale=*/quant_mult, /*zero_point=*/0, rows, cols, output);
  }

  // Convert from a B that was already transposed (routine not provided) and
  // quantized (e.g. with Quantize) to the CPU-dependent format used for
  // Multiply.  This is useful for storing a quantized model on disk then in a
  // CPU-independent fashion.
  static void PrepareBQuantizedTransposed(const Integer *input,
                                          Integer *output,
                                          Index inner,
                                          Index B_untransposed_cols) {
    int8PrepareBFromQuantizedTransposed(input, inner, B_untransposed_cols, output);
  }

  // Convert from a B that was already transposed (routine not provided) to
  // the CPU-dependent format used for Multiply.  This is useful for storing
  // a quantized model on disk then in a CPU-independent fashion.
  static void PrepareBTransposed(const float *input,
                                 Integer *output,
                                 float quant_mul,
                                 Index inner,
                                 Index B_untransposed_cols) {
    int8PrepareBFromTransposed(
        input, /*scale=*/quant_mul, /*zero_point=*/0, inner, B_untransposed_cols, output);
  }

  // Select columns from a prepared B matrix.  The number of selected columns
  // must be a multiple of 8.
  static void SelectColumnsB(const Integer *input,
                             Integer *output,
                             Index rows,
                             const Index *cols_begin,
                             const Index *cols_end) {
    // void int8SelectColumnsOfB(const int8_t *input_B_prepared, Index width, Index cols_B, const
    // Index *cols, const Index num_cols, int8_t *output)
    // FIXME: cols_B comes from somewhere...
    int8SelectColumnsOfB(input,
                         rows,
                         rows / sizeof(Integer),
                         cols_begin,
                         std::distance(cols_begin, cols_end),
                         output);
  }

  template <class Callback>
  static void PrepareBias(const Integer *B, Integer width, Index B_cols, Callback callback) {}

  static void PrepareBias(const Integer *B,
                          Integer width,
                          Index B_cols,
                          intgemm::callbacks::UnquantizeAndAddBiasAndWrite callback) {
    /*
void int8PrepareBias(const int8_t *input_B_prepared,
                   float scale_A,
                   float zero_point_A,
                   float scale_B,
                   float zero_point_B,
                   Index width,
                   Index cols_B,
                   const float *input_bias,
     */
    int8PrepareBias(B,
                    /*scale=*/1.0f,
                    /*zero_point=*/0.0f,
                    /*scale=*/1.0f,
                    /*zero_point_B=*/0.0f,
                    width,
                    B_cols,
                    callback.bias_addr,
                    callback.output_addr);
  }

  // Multiply C = A * B, presuming A and B have been prepared.
  template <class Callback>
  static void Multiply(const Integer *A,
                       const Integer *B,
                       Index A_rows,
                       Index width,
                       Index B_cols,
                       Callback callback) {
    /*
    void int8MultiplyAndAddBias(
        const int8_t *input_A_prepared, float scale_A, float zero_point_A,
        const int8_t *input_B_prepared, float scale_B, float zero_point_B,
        const float *input_bias_prepared, float unquant_multiplier,
        Index rows_A, Index width, Index cols_B, float *output);
    */

    // clang-format off
    int8MultiplyAndAddBias(
        A, /*scale_A=*/1.0f, /*zero_point_A=*/0, 
        B, /*scale_B=*/0.0f, /*zero_point_B=*/0, 
        callback.bias_addr, callback.unquant_mult,
        A_rows, width, B_cols, callback.output_addr);
    // clang-format on

    /*
    intgemm::Int8Shift::Multiply(
        input_A_prepared, input_B_prepared, rows_A, width, cols_B,
        intgemm::callbacks::UnquantizeAndAddBiasAndWrite(
            unquant_factor, input_bias_prepared, output));
    */
  }

  static const char *const kName;
};

using Int16 = IntImpl<int16_t>;
using Int8Shift = IntImpl<int8_t>;

static inline MeanStd GetVectorMeanStd(const float *begin,
                                       const float *end,
                                       bool absolute = false) {
  return VectorMeanStd(begin, end, absolute);
}

static inline float MaxAbsolute(const float *begin_float, const float *end_float) {
  float maxAbsolute = std::numeric_limits<float>::infinity();
  for(auto p = begin_float; p != end_float; ++p) {
    maxAbsolute = std::max<float>(maxAbsolute, *p);
  }
  return maxAbsolute;
}

}  // namespace intgemm
