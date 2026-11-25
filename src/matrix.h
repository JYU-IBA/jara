/*

    JaRA, a program for time-of-flight elastic recoil detection analysis
    Copyright (C) 2026 Jaakko Julin

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    See LICENSE.txt for the full license.

 */

#ifndef JARA_MATRIX_H
#define JARA_MATRIX_H

#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

// Dense two-dimensional array in one contiguous block, in row-major order: the column index is the fastest.
// The size is set at construction or with resize(), which sets every element to the value and keeps the allocation
// when the size does not grow. resizeRows() keeps the rows that remain.

template<typename T>
class Matrix {
public:
    Matrix() = default;
    Matrix(size_t rows, size_t columns, const T &value = T()) { resize(rows, columns, value); }
    void resize(size_t rows, size_t columns, const T &value = T()) {
        m_rows = rows;
        m_columns = columns;
        m_data.assign(rows * columns, value);
    }
    // Keeps the first rows, new rows get the value
    void resizeRows(size_t rows, const T &value = T()) {
        m_rows = rows;
        m_data.resize(rows * m_columns, value);
    }
    size_t rows() const { return m_rows; }
    size_t columns() const { return m_columns; }
    bool empty() const { return m_data.empty(); }
    T &operator()(size_t row, size_t column) {
        assert(row < m_rows && column < m_columns);
        return m_data[row * m_columns + column];
    }
    const T &operator()(size_t row, size_t column) const {
        assert(row < m_rows && column < m_columns);
        return m_data[row * m_columns + column];
    }
    std::span<T> row(size_t row) {
        assert(row < m_rows);
        return {m_data.data() + row * m_columns, m_columns};
    }
    std::span<const T> row(size_t row) const {
        assert(row < m_rows);
        return {m_data.data() + row * m_columns, m_columns};
    }

private:
    size_t m_rows = 0;
    size_t m_columns = 0;
    std::vector<T> m_data;
};

#endif // JARA_MATRIX_H
