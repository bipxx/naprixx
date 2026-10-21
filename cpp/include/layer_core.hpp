#pragma once

#include "application.hpp"
#include <optional>
#include <stdexcept>

namespace nx::app {
struct Matrix {
    size_t rows, cols;
    std::vector<double> values;
    Matrix(size_t rows, size_t cols);
    double& operator()(size_t row, size_t col) {
        if (row >= rows || col >= cols) throw std::out_of_range("matrix index is out of bounds");
        return values.at(row * cols + col);
    }
    double operator()(size_t row, size_t col) const {
        if (row >= rows || col >= cols) throw std::out_of_range("matrix index is out of bounds");
        return values.at(row * cols + col);
    }
    static Matrix identity(size_t size);
    Matrix transpose() const;
};
Matrix multiply(const Matrix& left, const Matrix& right);
Matrix inverse(const Matrix& matrix);
Matrix embed(const Matrix& matrix, size_t ndim);
Matrix rotation_2d(double angle);
Matrix rotation_3d(double alpha, double beta, double gamma);
Matrix expand_shear(const std::vector<double>& values);
bool triangular(const Matrix& matrix, bool upper);
bool diagonal(const Matrix& matrix, double tolerance);
struct Decomposition { Matrix rotation; std::vector<double> scale; Matrix shear; };
Decomposition decompose(const Matrix& matrix, bool upper);
std::vector<double> trailing_vector(const std::optional<std::vector<double>>& values,
                                    size_t ndim, double pad);

struct SlicePlan {
    Index ndisplay;
    std::vector<Index> order;
    std::vector<Index> axes(bool displayed) const;
    SlicePlan resized(Index ndim) const;
    bool orthogonal(const Matrix& transform) const;
    Matrix data_slice(const Matrix& transform, const std::vector<double>& translate,
                      const Matrix& world) const;
};
std::vector<Index> world_to_layer_axes(const std::vector<Index>& world,
                                      Index world_ndim, Index layer_ndim);
struct LayerLoadState {
    bool loaded = true;
    Index request_id = -1;
    bool set_loaded(bool value);
    bool begin(Index id);
    bool finish(Index id);
};
Matrix aggregate_extent(const std::vector<std::vector<double>>& minima,
                        const std::vector<std::vector<double>>& maxima);
std::vector<double> aggregate_steps(const std::vector<std::vector<double>>& scales);
std::vector<std::string> layer_axis_labels(Index ndim,
    const std::vector<std::vector<std::string>>& annotations);
} // namespace nx::app
