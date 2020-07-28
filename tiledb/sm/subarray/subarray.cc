/**
 * @file   subarray.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017-2020 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file implements class Subarray.
 */

#include "tiledb/sm/subarray/subarray.h"
#include "tiledb/sm/array/array.h"
#include "tiledb/sm/array_schema/array_schema.h"
#include "tiledb/sm/array_schema/attribute.h"
#include "tiledb/sm/array_schema/dimension.h"
#include "tiledb/sm/array_schema/domain.h"
#include "tiledb/sm/enums/layout.h"
#include "tiledb/sm/fragment/fragment_metadata.h"
#include "tiledb/sm/misc/parallel_functions.h"
#include "tiledb/sm/misc/utils.h"
#include "tiledb/sm/rtree/rtree.h"
#include "tiledb/sm/stats/stats.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace tiledb {
namespace sm {

/* ****************************** */
/*   CONSTRUCTORS & DESTRUCTORS   */
/* ****************************** */

Subarray::Subarray(const Config& config) {
  array_ = nullptr;
  layout_ = Layout::UNORDERED;
  cell_order_ = Layout::ROW_MAJOR;
  est_result_size_computed_ = false;
  tile_overlap_computed_ = false;

  const char* prefix = nullptr;
  config.get("sm.log_prefix", &prefix);
  logger_ = Logger(prefix);
  config_ = config;
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
}

Subarray::Subarray(const Array* array, const Config& config)
    : Subarray(array, Layout::UNORDERED, config) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
}

Subarray::Subarray(const Array* array, Layout layout, const Config& config)
    : array_(array)
    , layout_(layout) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  est_result_size_computed_ = false;
  tile_overlap_computed_ = false;

  const char* prefix = nullptr;
  config.get("sm.log_prefix", &prefix);
  logger_ = Logger(prefix);
  config_ = config;

  cell_order_ = array_->array_schema()->cell_order();
  add_default_ranges();
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
}

Subarray::Subarray(const Subarray& subarray)
    : Subarray(subarray.config_) {
  // Make a deep-copy clone
  auto clone = subarray.clone();
  // Swap with the clone
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  swap(clone);
}

Subarray::Subarray(Subarray&& subarray) noexcept
    : Subarray(subarray.config_) {
  // Swap with the argument
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  swap(subarray);
}

Subarray& Subarray::operator=(const Subarray& subarray) {
  // Make a deep-copy clone
  auto clone = subarray.clone();
  // Swap with the clone
  swap(clone);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return *this;
}

Subarray& Subarray::operator=(Subarray&& subarray) noexcept {
  // Swap with the argument
  swap(subarray);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return *this;
}

/* ****************************** */
/*               API              */
/* ****************************** */

Status Subarray::add_range(uint32_t dim_idx, const Range& range) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();
  if (dim_idx >= dim_num)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot add range to dimension; Invalid dimension index"));

  // Must reset the result size and tile overlap
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  est_result_size_computed_ = false;
  tile_overlap_computed_ = false;

  // Remove the default range
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (is_default_[dim_idx]) {
    ranges_[dim_idx].clear();
    is_default_[dim_idx] = false;
  }

  // Correctness checks
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim = array_->array_schema()->dimension(dim_idx);
  RETURN_NOT_OK(dim->check_range(range));

  // Add the range
  ranges_[dim_idx].emplace_back(range);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::add_range_unsafe(uint32_t dim_idx, const Range& range) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // Must reset the result size and tile overlap
  est_result_size_computed_ = false;
  tile_overlap_computed_ = false;

  // Remove the default range
  if (is_default_[dim_idx]) {
    ranges_[dim_idx].clear();
    is_default_[dim_idx] = false;
  }

  // Add the range
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  ranges_[dim_idx].emplace_back(range);

  return Status::Ok();
}

const Array* Subarray::array() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return array_;
}

uint64_t Subarray::cell_num(uint64_t range_idx) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  uint64_t cell_num = 1, range;
  auto array_schema = array_->array_schema();
  unsigned dim_num = array_schema->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;
  uint64_t tmp_idx = range_idx;

  // Unary case or GLOBAL_ORDER
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (range_num() == 1) {
    for (unsigned d = 0; d < dim_num; ++d) {
      range = array_schema->dimension(d)->domain_range(ranges_[d][0]);
      if (range == std::numeric_limits<uint64_t>::max())  // Overflow
        return range;

      cell_num = utils::math::safe_mul(range, cell_num);
      if (cell_num == std::numeric_limits<uint64_t>::max())  // Overflow
        return cell_num;
    }

    return cell_num;
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // Non-unary case (range_offsets_ must be computed)
  if (layout == Layout::ROW_MAJOR) {
    assert(!range_offsets_.empty());
    for (unsigned d = 0; d < dim_num; ++d) {
      range = array_schema->dimension(d)->domain_range(
          ranges_[d][tmp_idx / range_offsets_[d]]);
      tmp_idx %= range_offsets_[d];
      if (range == std::numeric_limits<uint64_t>::max())  // Overflow
        return range;

      cell_num = utils::math::safe_mul(range, cell_num);
      if (cell_num == std::numeric_limits<uint64_t>::max())  // Overflow
        return cell_num;
    }
  } else if (layout == Layout::COL_MAJOR) {
    assert(!range_offsets_.empty());
    for (unsigned d = dim_num - 1;; --d) {
      range = array_schema->dimension(d)->domain_range(
          ranges_[d][tmp_idx / range_offsets_[d]]);
      tmp_idx %= range_offsets_[d];
      if (range == std::numeric_limits<uint64_t>::max())  // Overflow
        return range;

      cell_num = utils::math::safe_mul(range, cell_num);
      if (cell_num == std::numeric_limits<uint64_t>::max())  // Overflow
        return cell_num;

      if (d == 0)
        break;
    }
  } else {  // GLOBAL_ORDER handled above
    assert(false);
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return cell_num;
}

uint64_t Subarray::cell_num(const std::vector<uint64_t>& range_coords) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto dim_num = array_->array_schema()->dim_num();
  assert(dim_num == range_coords.size());

  uint64_t ret = 1;
  for (unsigned d = 0; d < dim_num; ++d) {
    auto dim = array_schema->dimension(d);
    ret = utils::math::safe_mul(
        ret, dim->domain_range(ranges_[d][range_coords[d]]));
    if (ret == std::numeric_limits<uint64_t>::max())  // Overflow
      return ret;
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

void Subarray::clear() {
  ranges_.clear();
  range_offsets_.clear();
  tile_overlap_.clear();
  est_result_size_computed_ = false;
  tile_overlap_computed_ = false;
}

bool Subarray::coincides_with_tiles() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (range_num() != 1)
    return false;

  auto dim_num = array_->array_schema()->dim_num();
  for (unsigned d = 0; d < dim_num; ++d) {
    auto dim = array_->array_schema()->dimension(d);
    if (!dim->coincides_with_tiles(ranges_[d][0]))
      return false;
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return true;
}

template <class T>
Subarray Subarray::crop_to_tile(const T* tile_coords, Layout layout) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  Subarray ret(array_, layout, config_);
  T new_range[2];
  bool overlaps;

  // Get tile subarray based on the input coordinates
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  std::vector<T> tile_subarray(2 * dim_num());
  array_schema->domain()->get_tile_subarray(tile_coords, &tile_subarray[0]);

  // Compute cropped subarray
  for (unsigned d = 0; d < dim_num(); ++d) {
    auto r_size = 2 * array_schema->dimension(d)->coord_size();
    for (size_t r = 0; r < ranges_[d].size(); ++r) {
      const auto& range = ranges_[d][r];
      utils::geometry::overlap(
          (const T*)range.data(),
          &tile_subarray[2 * d],
          1,
          new_range,
          &overlaps);

      if (overlaps)
        ret.add_range_unsafe(d, Range(new_range, r_size));
    }
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

uint32_t Subarray::dim_num() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return array_->array_schema()->dim_num();
}

NDRange Subarray::domain() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return array_->array_schema()->domain()->domain();
}

bool Subarray::empty() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return range_num() == 0;
}

Status Subarray::get_query_type(QueryType* type) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (array_ == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get query type from array; Invalid array"));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return array_->get_query_type(type);
}

Status Subarray::get_range(
    uint32_t dim_idx, uint64_t range_idx, const Range** range) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();
  if (dim_idx >= dim_num)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get range; Invalid dimension index"));

  auto range_num = ranges_[dim_idx].size();
  if (range_idx >= range_num)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get range; Invalid range index"));

  *range = &ranges_[dim_idx][range_idx];

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::get_range(
    uint32_t dim_idx,
    uint64_t range_idx,
    const void** start,
    const void** end) const {
  auto dim_num = array_->array_schema()->dim_num();
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (dim_idx >= dim_num)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get range; Invalid dimension index"));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto range_num = ranges_[dim_idx].size();
  if (range_idx >= range_num)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get range; Invalid range index"));

  *start = ranges_[dim_idx][range_idx].start();
  *end = ranges_[dim_idx][range_idx].end();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::get_range_var_size(
    uint32_t dim_idx,
    uint64_t range_idx,
    uint64_t* start,
    uint64_t* end) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto schema = array_->array_schema();
  auto dim_num = schema->dim_num();
  if (dim_idx >= dim_num)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get var range size; Invalid dimension index"));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim = schema->domain()->dimension(dim_idx);
  if (!dim->var_size())
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get var range size; Dimension " + dim->name() +
        " is not var sized"));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto range_num = ranges_[dim_idx].size();
  if (range_idx >= range_num)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get var range size; Invalid range index"));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  *start = ranges_[dim_idx][range_idx].start_size();
  *end = ranges_[dim_idx][range_idx].end_size();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::get_range_num(uint32_t dim_idx, uint64_t* range_num) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();
  if (dim_idx >= dim_num)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get number of ranges for a dimension; "
                              "Invalid dimension index"));

  *range_num = ranges_[dim_idx].size();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Subarray Subarray::get_subarray(uint64_t start, uint64_t end) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  Subarray ret(array_, layout_, config_);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto start_coords = get_range_coords(start);
  auto end_coords = get_range_coords(end);

  auto dim_num = this->dim_num();
  for (unsigned d = 0; d < dim_num; ++d) {
    for (uint64_t r = start_coords[d]; r <= end_coords[d]; ++r) {
      ret.add_range_unsafe(d, ranges_[d][r]);
    }
  }

  // Set tile overlap
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto fragment_num = tile_overlap_.size();
  ret.tile_overlap_.resize(fragment_num);
  for (unsigned i = 0; i < fragment_num; ++i) {
    for (uint64_t r = start; r <= end; ++r) {
      ret.tile_overlap_[i].push_back(tile_overlap_[i][r]);
    }
  }

  // Compute range offsets
  ret.compute_range_offsets();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

bool Subarray::is_default(uint32_t dim_index) const {
  return is_default_[dim_index];
}

bool Subarray::is_set() const {
  for (const auto& d : is_default_)
    if (d == false)
      return true;
  return false;
}

bool Subarray::is_set(unsigned dim_idx) const {
  assert(dim_idx < dim_num());
  return !is_default_[dim_idx];
}

bool Subarray::is_unary() const {
  if (range_num() != 1)
    return false;

  for (const auto& range : ranges_) {
    if (!range[0].unary())
      return false;
  }

  return true;
}

bool Subarray::is_unary(uint64_t range_idx) const {
  auto coords = get_range_coords(range_idx);
  auto dim_num = this->dim_num();

  for (unsigned d = 0; d < dim_num; ++d) {
    if (!ranges_[d][coords[d]].unary())
      return false;
  }

  return true;
}

void Subarray::set_is_default(uint32_t dim_index, bool is_default) {
  is_default_[dim_index] = is_default;
}

void Subarray::set_layout(Layout layout) {
  layout_ = layout;
}

Status Subarray::to_byte_vec(std::vector<uint8_t>* byte_vec) const {
  if (range_num() != 1)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot export to byte vector; The subarray must be unary"));

  for (const auto& r : ranges_) {
    auto offset = byte_vec->size();
    byte_vec->resize(offset + r[0].size());
    std::memcpy(&(*byte_vec)[offset], r[0].data(), r[0].size());
  }

  return Status::Ok();
}

Layout Subarray::layout() const {
  return layout_;
}

Status Subarray::get_est_result_size(const char* name, uint64_t* size) {
  // Check attribute/dimension name
  if (name == nullptr)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get estimated result size; "
                              "Attribute/Dimension name cannot be null"));

  // Check size pointer
  if (size == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get estimated result size; Input size cannot be null"));

  // Check if name is attribute or dimension
  auto array_schema = array_->array_schema();
  bool is_dim = array_schema->is_dim(name);
  bool is_attr = array_schema->is_attr(name);

  // Check if attribute/dimension exists
  if (name != constants::coords && !is_dim && !is_attr)
    return LOG_STATUS(Status::SubarrayError(
        std::string("Cannot get estimated result size; Attribute/Dimension '") +
        name + "' does not exist"));

  // Check if the attribute/dimension is fixed-sized
  if (array_schema->var_size(name))
    return LOG_STATUS(
        Status::SubarrayError("Cannot get estimated result size; "
                              "Attribute/Dimension must be fixed-sized"));

  // Compute tile overlap for each fragment
  RETURN_NOT_OK(compute_est_result_size());
  *size = (uint64_t)ceil(est_result_size_[name].size_fixed_);

  return Status::Ok();
}

Status Subarray::get_est_result_size(
    const char* name, uint64_t* size_off, uint64_t* size_val) {
  // Check attribute/dimension name
  if (name == nullptr)
    return LOG_STATUS(
        Status::SubarrayError("Cannot get estimated result size; "
                              "Attribute/Dimension name cannot be null"));

  // Check size pointer
  if (size_off == nullptr || size_val == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get estimated result size; Input sizes cannot be null"));

  // Check if name is attribute or dimension
  auto array_schema = array_->array_schema();
  bool is_dim = array_schema->is_dim(name);
  bool is_attr = array_schema->is_attr(name);

  // Check if attribute/dimension exists
  if (name != constants::coords && !is_dim && !is_attr)
    return LOG_STATUS(Status::SubarrayError(
        std::string("Cannot get estimated result size; Attribute/Dimension '") +
        name + "' does not exist"));

  // Check if the attribute/dimension is var-sized
  if (!array_schema->var_size(name))
    return LOG_STATUS(
        Status::SubarrayError("Cannot get estimated result size; "
                              "Attribute/Dimension must be var-sized"));

  // Compute tile overlap for each fragment
  RETURN_NOT_OK(compute_est_result_size());
  *size_off = (uint64_t)ceil(est_result_size_[name].size_fixed_);
  *size_val = (uint64_t)ceil(est_result_size_[name].size_var_);

  return Status::Ok();
}

Status Subarray::get_max_memory_size(const char* name, uint64_t* size) {
  // Check attribute/dimension name
  if (name == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Attribute/Dimension cannot be null"));

  // Check size pointer
  if (size == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Inpute size cannot be null"));

  // Check if name is attribute or dimension
  auto array_schema = array_->array_schema();
  bool is_dim = array_schema->is_dim(name);
  bool is_attr = array_schema->is_attr(name);

  // Check if attribute/dimension exists
  if (name != constants::coords && !is_dim && !is_attr)
    return LOG_STATUS(Status::SubarrayError(
        std::string("Cannot get max memory size; Attribute/Dimension '") +
        name + "' does not exist"));

  // Check if the attribute/dimension is fixed-sized
  if (name != constants::coords && array_schema->var_size(name))
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Attribute/Dimension must be fixed-sized"));

  // Compute tile overlap for each fragment
  compute_est_result_size();
  *size = max_mem_size_[name].size_fixed_;

  return Status::Ok();
}

Status Subarray::get_max_memory_size(
    const char* name, uint64_t* size_off, uint64_t* size_val) {
  // Check attribute/dimension name
  if (name == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Attribute/Dimension cannot be null"));

  // Check size pointer
  if (size_off == nullptr || size_val == nullptr)
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Input sizes cannot be null"));

  // Check if name is attribute or dimension
  auto array_schema = array_->array_schema();
  bool is_dim = array_schema->is_dim(name);
  bool is_attr = array_schema->is_attr(name);

  // Check if attribute/dimension exists
  if (name != constants::coords && !is_dim && !is_attr)
    return LOG_STATUS(Status::SubarrayError(
        std::string("Cannot get max memory size; Attribute/Dimension '") +
        name + "' does not exist"));

  // Check if the attribute/dimension is var-sized
  if (!array_schema->var_size(name))
    return LOG_STATUS(Status::SubarrayError(
        "Cannot get max memory size; Attribute/Dimension must be var-sized"));

  // Compute tile overlap for each fragment
  compute_est_result_size();
  *size_off = max_mem_size_[name].size_fixed_;
  *size_val = max_mem_size_[name].size_var_;

  return Status::Ok();
}

std::vector<uint64_t> Subarray::get_range_coords(uint64_t range_idx) const {
  std::vector<uint64_t> ret;

  uint64_t tmp_idx = range_idx;
  auto dim_num = this->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;

  if (layout == Layout::ROW_MAJOR) {
    for (unsigned i = 0; i < dim_num; ++i) {
      ret.push_back(tmp_idx / range_offsets_[i]);
      tmp_idx %= range_offsets_[i];
    }
  } else if (layout == Layout::COL_MAJOR) {
    for (unsigned i = dim_num - 1;; --i) {
      ret.push_back(tmp_idx / range_offsets_[i]);
      tmp_idx %= range_offsets_[i];
      if (i == 0)
        break;
    }
    std::reverse(ret.begin(), ret.end());
  } else {
    // Global order - single range
    assert(layout == Layout::GLOBAL_ORDER);
    assert(range_num() == 1);
    for (unsigned i = 0; i < dim_num; ++i)
      ret.push_back(0);
  }

  return ret;
}

void Subarray::get_next_range_coords(
    std::vector<uint64_t>* range_coords) const {
  auto dim_num = array_->array_schema()->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;

  if (layout == Layout::ROW_MAJOR) {
    auto d = dim_num - 1;
    ++(*range_coords)[d];
    while ((*range_coords)[d] >= ranges_[d].size() && d != 0) {
      (*range_coords)[d] = 0;
      --d;
      ++(*range_coords)[d];
    }
  } else if (layout == Layout::COL_MAJOR) {
    auto d = (unsigned)0;
    ++(*range_coords)[d];
    while ((*range_coords)[d] >= ranges_[d].size() && d != dim_num - 1) {
      (*range_coords)[d] = 0;
      ++d;
      ++(*range_coords)[d];
    }
  } else {
    // Global order - noop
  }
}

uint64_t Subarray::range_idx(const std::vector<uint64_t>& range_coords) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  uint64_t ret = 0;
  auto dim_num = this->dim_num();
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (unsigned i = 0; i < dim_num; ++i)
    ret += range_offsets_[i] * range_coords[i];

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

uint64_t Subarray::range_num() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (ranges_.empty())
    return 0;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  uint64_t ret = 1;
  for (const auto& r : ranges_)
    ret *= r.size();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

NDRange Subarray::ndrange(uint64_t range_idx) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  NDRange ret;
  uint64_t tmp_idx = range_idx;
  auto dim_num = this->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;
  ret.reserve(dim_num);

  // Unary case or GLOBAL_ORDER
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (range_idx == 0 && range_num() == 1) {
    for (unsigned d = 0; d < dim_num; ++d)
      ret.emplace_back(ranges_[d][0]);
    return ret;
  }

  // Non-unary case (range_offsets_ must be computed)
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (layout == Layout::ROW_MAJOR) {
    assert(!range_offsets_.empty());
    for (unsigned d = 0; d < dim_num; ++d) {
      ret.emplace_back(ranges_[d][tmp_idx / range_offsets_[d]]);
      tmp_idx %= range_offsets_[d];
    }
  } else if (layout == Layout::COL_MAJOR) {
    assert(!range_offsets_.empty());
    for (unsigned d = dim_num - 1;; --d) {
      ret.emplace_back(ranges_[d][tmp_idx / range_offsets_[d]]);
      tmp_idx %= range_offsets_[d];
      if (d == 0)
        break;
    }
    std::reverse(ret.begin(), ret.end());
  } else {  // GLOBAL_ORDER handled above
    assert(false);
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

NDRange Subarray::ndrange(const std::vector<uint64_t>& range_coords) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = this->dim_num();
  NDRange ret;
  ret.reserve(dim_num);
  for (unsigned d = 0; d < dim_num; ++d)
    ret.emplace_back(ranges_[d][range_coords[d]]);
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

const std::vector<Range>& Subarray::ranges_for_dim(uint32_t dim_idx) const {
  return ranges_[dim_idx];
}

Status Subarray::set_ranges_for_dim(
    uint32_t dim_idx, const std::vector<Range>& ranges) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  ranges_.resize(dim_idx + 1);
  ranges_[dim_idx] = ranges;
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::split(
    unsigned splitting_dim,
    const ByteVecValue& splitting_value,
    Subarray* r1,
    Subarray* r2) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  assert(r1 != nullptr);
  assert(r2 != nullptr);
  *r1 = Subarray(array_, layout_, config_);
  *r2 = Subarray(array_, layout_, config_);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();

  Range sr1, sr2;
  for (unsigned d = 0; d < dim_num; ++d) {
    const auto& r = ranges_[d][0];
    if (d == splitting_dim) {
      auto dim = array_->array_schema()->dimension(d);
      dim->split_range(r, splitting_value, &sr1, &sr2);
      RETURN_NOT_OK(r1->add_range_unsafe(d, sr1));
      RETURN_NOT_OK(r2->add_range_unsafe(d, sr2));
    } else {
      RETURN_NOT_OK(r1->add_range_unsafe(d, r));
      RETURN_NOT_OK(r2->add_range_unsafe(d, r));
    }
  }
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());

  return Status::Ok();
}

Status Subarray::split(
    uint64_t splitting_range,
    unsigned splitting_dim,
    const ByteVecValue& splitting_value,
    Subarray* r1,
    Subarray* r2) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  assert(r1 != nullptr);
  assert(r2 != nullptr);
  *r1 = Subarray(array_, layout_, config_);
  *r2 = Subarray(array_, layout_, config_);

  // For easy reference
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto dim_num = array_schema->dim_num();
  uint64_t range_num;
  Range sr1, sr2;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (unsigned d = 0; d < dim_num; ++d) {
    RETURN_NOT_OK(this->get_range_num(d, &range_num));
    if (d != splitting_dim) {
      for (uint64_t j = 0; j < range_num; ++j) {
        const auto& r = ranges_[d][j];
        RETURN_NOT_OK(r1->add_range_unsafe(d, r));
        RETURN_NOT_OK(r2->add_range_unsafe(d, r));
      }
    } else {                                // d == splitting_dim
      if (splitting_range != UINT64_MAX) {  // Need to split multiple ranges
        for (uint64_t j = 0; j <= splitting_range; ++j) {
          const auto& r = ranges_[d][j];
          RETURN_NOT_OK(r1->add_range_unsafe(d, r));
        }
        for (uint64_t j = splitting_range + 1; j < range_num; ++j) {
          const auto& r = ranges_[d][j];
          RETURN_NOT_OK(r2->add_range_unsafe(d, r));
        }
      } else {  // Need to split a single range
        const auto& r = ranges_[d][0];
        auto dim = array_schema->dimension(d);
        dim->split_range(r, splitting_value, &sr1, &sr2);
        RETURN_NOT_OK(r1->add_range_unsafe(d, sr1));
        RETURN_NOT_OK(r2->add_range_unsafe(d, sr2));
      }
    }
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

const std::vector<std::vector<uint8_t>>& Subarray::tile_coords() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return tile_coords_;
}

const std::vector<std::vector<TileOverlap>>& Subarray::tile_overlap() const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return tile_overlap_;
}

template <class T>
Status Subarray::compute_tile_coords() {
  STATS_START_TIMER(stats::Stats::TimerType::READ_COMPUTE_TILE_COORDS)

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (array_->array_schema()->tile_order() == Layout::ROW_MAJOR)
    return compute_tile_coords_row<T>();
  return compute_tile_coords_col<T>();

  STATS_END_TIMER(stats::Stats::TimerType::READ_COMPUTE_TILE_COORDS)
}

template <class T>
const T* Subarray::tile_coords_ptr(
    const std::vector<T>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();
  auto coord_size = array_->array_schema()->dimension(0)->coord_size();
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  std::memcpy(&((*aux_tile_coords)[0]), &tile_coords[0], dim_num * coord_size);
  auto it = tile_coords_map_.find(*aux_tile_coords);
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (it == tile_coords_map_.end())
    return nullptr;
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return (const T*)&(tile_coords_[it->second][0]);
}

Status Subarray::compute_relevant_fragment_est_result_sizes(
    const std::vector<std::string>& names,
    uint64_t range_start,
    uint64_t range_end,
    std::vector<std::vector<ResultSize>>* result_sizes,
    std::vector<std::vector<MemorySize>>* mem_sizes) {
  // For easy reference
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto fragment_metadata = array_->fragment_metadata();
  auto encryption_key = array_->encryption_key();
  auto dim_num = array_->array_schema()->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  RETURN_NOT_OK(load_relevant_fragment_tile_var_sizes(names));

  // Prepare result sizes vectors
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto range_num = range_end - range_start + 1;
  result_sizes->resize(range_num);
  std::vector<std::set<std::pair<unsigned, uint64_t>>> frag_tiles(range_num);
  for (size_t r = 0; r < range_num; ++r)
    (*result_sizes)[r].reserve(names.size());

  // Create vector of var sizes
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  std::vector<bool> var_sizes;
  var_sizes.reserve(names.size());
  for (const auto& name : names)
    var_sizes.push_back(array_schema->var_size(name));

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto all_dims_same_type = array_schema->domain()->all_dims_same_type();
  auto all_dims_fixed = array_schema->domain()->all_dims_fixed();
  auto num_threads = array_->num_threads();
  auto ranges_per_thread = (uint64_t)ceil((double)range_num / num_threads);
  auto statuses = parallel_for(0, num_threads, [&](uint64_t t) {
    auto r_start = range_start + t * ranges_per_thread;
    auto r_end =
        std::min(range_start + (t + 1) * ranges_per_thread - 1, range_end);
    auto r_coords = get_range_coords(r_start);
    for (uint64_t r = r_start; r <= r_end; ++r) {
      RETURN_NOT_OK(compute_relevant_fragment_est_result_sizes(
          encryption_key,
          array_schema,
          all_dims_same_type,
          all_dims_fixed,
          fragment_metadata,
          names,
          var_sizes,
          r,
          r_coords,
          &(*result_sizes)[r - range_start],
          &frag_tiles[r - range_start]));

      // Get next range coordinates
      if (layout == Layout::ROW_MAJOR) {
        auto d = dim_num - 1;
        ++(r_coords)[d];
        while ((r_coords)[d] >= ranges_[d].size() && d != 0) {
          (r_coords)[d] = 0;
          --d;
          ++(r_coords)[d];
        }
      } else if (layout == Layout::COL_MAJOR) {
        auto d = (unsigned)0;
        ++(r_coords)[d];
        while ((r_coords)[d] >= ranges_[d].size() && d != dim_num - 1) {
          (r_coords)[d] = 0;
          ++d;
          ++(r_coords)[d];
        }
      } else {
        // Global order - noop
      }
    }
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    return Status::Ok();
  });
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (auto st : statuses)
    RETURN_NOT_OK(st);

  // Compute the mem sizes vector
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  mem_sizes->resize(range_num);
  for (auto& ms : *mem_sizes)
    ms.resize(names.size(), {0, 0});
  std::unordered_set<std::pair<unsigned, uint64_t>, utils::hash::pair_hash>
      all_frag_tiles;
  uint64_t tile_size, tile_var_size;
  for (uint64_t r = 0; r < range_num; ++r) {
    auto& mem_vec = (*mem_sizes)[r];
    for (const auto& ft : frag_tiles[r]) {
      auto it = all_frag_tiles.insert(ft);
      if (it.second) {  // If the fragment/tile pair is new
        auto meta = fragment_metadata[ft.first];
        for (size_t i = 0; i < names.size(); ++i) {
          tile_size = meta->tile_size(names[i], ft.second);
          if (!var_sizes[i]) {
            mem_vec[i].size_fixed_ += tile_size;
          } else {
            RETURN_NOT_OK(meta->tile_var_size(
                *encryption_key, names[i], ft.second, &tile_var_size));
            mem_vec[i].size_fixed_ += tile_size;
            mem_vec[i].size_var_ += tile_var_size;
          }
        }
      }
    }
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

std::unordered_map<std::string, Subarray::ResultSize>
Subarray::get_est_result_size_map() {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // If the result sizes have not been computed, compute them first
  if (!est_result_size_computed_)
    compute_est_result_size();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return est_result_size_;
}

std::unordered_map<std::string, Subarray::MemorySize>
Subarray::get_max_mem_size_map() {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // If the result sizes have not been computed, compute them first
  if (!est_result_size_computed_)
    compute_est_result_size();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return max_mem_size_;
}

Status Subarray::set_est_result_size(
    std::unordered_map<std::string, ResultSize>& est_result_size,
    std::unordered_map<std::string, MemorySize>& max_mem_size) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  est_result_size_ = est_result_size;
  max_mem_size_ = max_mem_size;
  est_result_size_computed_ = true;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

/* ****************************** */
/*          PRIVATE METHODS       */
/* ****************************** */

void Subarray::add_default_ranges() {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto dim_num = array_schema->dim_num();
  auto domain = array_schema->domain()->domain();

  ranges_.resize(dim_num);
  is_default_.resize(dim_num, true);
  for (unsigned d = 0; d < dim_num; ++d)
    ranges_[d].emplace_back(domain[d]);
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
}

void Subarray::compute_range_offsets() {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  range_offsets_.clear();

  auto dim_num = this->dim_num();
  auto layout = (layout_ == Layout::UNORDERED) ? cell_order_ : layout_;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (layout == Layout::COL_MAJOR) {
    range_offsets_.push_back(1);
    if (dim_num > 1) {
      for (unsigned int i = 1; i < dim_num; ++i)
        range_offsets_.push_back(range_offsets_.back() * ranges_[i - 1].size());
    }
  } else if (layout == Layout::ROW_MAJOR) {
    range_offsets_.push_back(1);
    if (dim_num > 1) {
      for (unsigned int i = dim_num - 2;; --i) {
        range_offsets_.push_back(range_offsets_.back() * ranges_[i + 1].size());
        if (i == 0)
          break;
      }
    }
    std::reverse(range_offsets_.begin(), range_offsets_.end());
  } else {
    // Global order - single range
    assert(layout == Layout::GLOBAL_ORDER);
    assert(range_num() == 1);
    range_offsets_.push_back(1);
    if (dim_num > 1) {
      for (unsigned int i = 1; i < dim_num; ++i)
        range_offsets_.push_back(1);
    }
  }
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
}

Status Subarray::compute_est_result_size() {
  STATS_START_TIMER(stats::Stats::TimerType::READ_COMPUTE_EST_RESULT_SIZE)

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (est_result_size_computed_)
    return Status::Ok();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  RETURN_NOT_OK(compute_tile_overlap());

  // Prepare estimated result size vector for all
  // attributes/dimension and zipped coords
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto attribute_num = array_schema->attribute_num();
  auto dim_num = array_schema->dim_num();
  auto attributes = array_schema->attributes();
  auto num = attribute_num + dim_num + 1;
  auto range_num = this->range_num();

  // Compute estimated result in parallel over fragments and ranges
  auto meta = array_->fragment_metadata();

  // Get attribute and dimension names
  std::vector<std::string> names(num);
  for (unsigned i = 0; i < num; ++i) {
    if (i < attribute_num)
      names[i] = attributes[i]->name();
    else if (i < attribute_num + dim_num)
      names[i] = array_schema->domain()->dimension(i - attribute_num)->name();
    else
      names[i] = constants::coords;
  }

  // Compute the estimated result and max memory sizes
  std::vector<std::vector<ResultSize>> result_sizes;
  std::vector<std::vector<MemorySize>> mem_sizes;
  std::vector<std::set<std::pair<unsigned, uint64_t>>> frag_tiles;
  RETURN_NOT_OK(compute_relevant_fragment_est_result_sizes(
      names, 0, range_num - 1, &result_sizes, &mem_sizes));

  // Accummulate the individual estimated result sizes
  std::vector<ResultSize> est_vec(num, ResultSize{0.0, 0.0});
  std::vector<MemorySize> mem_vec(num, MemorySize{0, 0});
  for (uint64_t r = 0; r < range_num; ++r) {
    for (size_t i = 0; i < result_sizes[r].size(); ++i) {
      est_vec[i].size_fixed_ += result_sizes[r][i].size_fixed_;
      est_vec[i].size_var_ += result_sizes[r][i].size_var_;
      mem_vec[i].size_fixed_ += mem_sizes[r][i].size_fixed_;
      mem_vec[i].size_var_ += mem_sizes[r][i].size_var_;
    }
  }

  // Amplify result estimation
  if (constants::est_result_size_amplification != 1.0) {
    for (auto& r : est_vec) {
      r.size_fixed_ *= constants::est_result_size_amplification;
      r.size_var_ *= constants::est_result_size_amplification;
    }
  }

  // Set the estimated result size map
  est_result_size_.clear();
  max_mem_size_.clear();
  for (unsigned i = 0; i < num; ++i) {
    est_result_size_[names[i]] = est_vec[i];
    max_mem_size_[names[i]] = mem_vec[i];
  }
  est_result_size_computed_ = true;

  return Status::Ok();

  STATS_END_TIMER(stats::Stats::TimerType::READ_COMPUTE_EST_RESULT_SIZE)
}

bool Subarray::est_result_size_computed() {
  return est_result_size_computed_;
}

Status Subarray::compute_relevant_fragment_est_result_sizes(
    const EncryptionKey* encryption_key,
    const ArraySchema* array_schema,
    bool all_dims_same_type,
    bool all_dims_fixed,
    const std::vector<FragmentMetadata*>& fragment_meta,
    const std::vector<std::string>& names,
    const std::vector<bool>& var_sizes,
    uint64_t range_idx,
    const std::vector<uint64_t>& range_coords,
    std::vector<ResultSize>* result_sizes,
    std::set<std::pair<unsigned, uint64_t>>* frag_tiles) {
  result_sizes->resize(names.size(), {0.0, 0.0});

  // Compute estimated result
  auto fragment_num = (unsigned)relevant_fragments_.size();
  for (unsigned i = 0; i < fragment_num; ++i) {
    auto f = relevant_fragments_[i];
    const auto& overlap = tile_overlap_[f][range_idx];
    auto meta = fragment_meta[f];

    // Parse tile ranges
    uint64_t tile_size = 0, tile_var_size = 0;
    for (const auto& tr : overlap.tile_ranges_) {
      for (uint64_t tid = tr.first; tid <= tr.second; ++tid) {
        for (size_t n = 0; n < names.size(); ++n) {
          // Zipped coords applicable only in homogeneous domains
          if (names[n] == constants::coords && !all_dims_same_type)
            continue;

          frag_tiles->insert(std::pair<unsigned, uint64_t>(f, tid));
          tile_size = meta->tile_size(names[n], tid);
          if (!var_sizes[n]) {
            (*result_sizes)[n].size_fixed_ += tile_size;
          } else {
            (*result_sizes)[n].size_fixed_ += tile_size;
            RETURN_NOT_OK(meta->tile_var_size(
                *encryption_key, names[n], tid, &tile_var_size));
            (*result_sizes)[n].size_var_ += tile_var_size;
          }
        }
      }
    }

    // Parse individual tiles
    for (const auto& t : overlap.tiles_) {
      auto tid = t.first;
      auto ratio = t.second;
      for (size_t n = 0; n < names.size(); ++n) {
        // Zipped coords applicable only in homogeneous domains
        if (names[n] == constants::coords && !all_dims_same_type)
          continue;

        frag_tiles->insert(std::pair<unsigned, uint64_t>(f, tid));
        tile_size = meta->tile_size(names[n], tid);
        if (!var_sizes[n]) {
          (*result_sizes)[n].size_fixed_ += tile_size * ratio;
        } else {
          (*result_sizes)[n].size_fixed_ += tile_size * ratio;
          RETURN_NOT_OK(meta->tile_var_size(
              *encryption_key, names[n], tid, &tile_var_size));
          (*result_sizes)[n].size_var_ += tile_var_size * ratio;
        }
      }
    }
  }

  // Calibrate result - applicable only to arrays without coordinate duplicates
  // and fixed dimensions
  if (!array_schema->allows_dups() && all_dims_fixed) {
    // Calculate cell num
    uint64_t cell_num = 1;
    auto dim_num = array_schema->dim_num();
    for (unsigned d = 0; d < dim_num; ++d) {
      auto dim = array_schema->dimension(d);
      cell_num = utils::math::safe_mul(
          cell_num, dim->domain_range(ranges_[d][range_coords[d]]));
    }

    uint64_t max_size_fixed, max_size_var = UINT64_MAX;
    for (size_t n = 0; n < names.size(); ++n) {
      // Zipped coords applicable only in homogeneous domains
      if (names[n] == constants::coords && !all_dims_same_type)
        continue;

      if (var_sizes[n]) {
        max_size_fixed =
            utils::math::safe_mul(cell_num, constants::cell_var_offset_size);
      } else {
        max_size_fixed =
            utils::math::safe_mul(cell_num, array_schema->cell_size(names[n]));
      }
      (*result_sizes)[n].size_fixed_ =
          std::min<double>((*result_sizes)[n].size_fixed_, max_size_fixed);
      (*result_sizes)[n].size_var_ =
          std::min<double>((*result_sizes)[n].size_var_, max_size_var);
    }
  }

  return Status::Ok();
}

template <class T>
Status Subarray::compute_tile_coords_col() {
  std::vector<std::set<T>> coords_set;
  auto array_schema = array_->array_schema();
  auto domain = array_schema->domain()->domain();
  auto dim_num = this->dim_num();
  uint64_t tile_start, tile_end;

  // Compute unique tile coords per dimension
  coords_set.resize(dim_num);
  for (unsigned d = 0; d < dim_num; ++d) {
    auto tile_extent = *(const T*)array_schema->domain()->tile_extent(d).data();
    for (uint64_t j = 0; j < ranges_[d].size(); ++j) {
      auto dim_dom = (const T*)domain[d].data();
      auto r = (const T*)ranges_[d][j].data();
      tile_start = (r[0] - dim_dom[0]) / tile_extent;
      tile_end = (r[1] - dim_dom[0]) / tile_extent;
      for (uint64_t t = tile_start; t <= tile_end; ++t)
        coords_set[d].insert(t);
    }
  }

  // Compute `tile_coords_`
  std::vector<typename std::set<T>::iterator> iters;
  size_t tile_coords_num = 1;
  for (unsigned d = 0; d < dim_num; ++d) {
    iters.push_back(coords_set[d].begin());
    tile_coords_num *= coords_set[d].size();
  }

  tile_coords_.resize(tile_coords_num);
  std::vector<uint8_t> coords;
  auto coords_size = dim_num * array_schema->dimension(0)->coord_size();
  coords.resize(coords_size);
  size_t coord_size = sizeof(T);
  size_t tile_coords_pos = 0;
  while (iters[dim_num - 1] != coords_set[dim_num - 1].end()) {
    for (unsigned d = 0; d < dim_num; ++d)
      std::memcpy(&(coords[d * coord_size]), &(*iters[d]), coord_size);
    tile_coords_[tile_coords_pos++] = coords;

    // Advance the iterators
    unsigned d = 0;
    while (d < dim_num) {
      iters[d]++;
      if (iters[d] != coords_set[d].end())
        break;
      if (d < dim_num - 1)
        iters[d] = coords_set[d].begin();
      d++;
    }
  }

  // Compute `tile_coords_map_`
  for (size_t i = 0; i < tile_coords_.size(); ++i, ++tile_coords_pos)
    tile_coords_map_[tile_coords_[i]] = i;

  return Status::Ok();
}

template <class T>
Status Subarray::compute_tile_coords_row() {
  std::vector<std::set<T>> coords_set;
  auto array_schema = array_->array_schema();
  auto domain = array_schema->domain()->domain();
  auto dim_num = this->dim_num();
  uint64_t tile_start, tile_end;

  // Compute unique tile coords per dimension
  coords_set.resize(dim_num);
  for (unsigned d = 0; d < dim_num; ++d) {
    auto tile_extent = *(const T*)array_schema->domain()->tile_extent(d).data();
    auto dim_dom = (const T*)domain[d].data();
    for (uint64_t j = 0; j < ranges_[d].size(); ++j) {
      auto r = (const T*)ranges_[d][j].data();
      tile_start = (r[0] - dim_dom[0]) / tile_extent;
      tile_end = (r[1] - dim_dom[0]) / tile_extent;
      for (uint64_t t = tile_start; t <= tile_end; ++t)
        coords_set[d].insert(t);
    }
  }

  // Compute `tile_coords_`
  std::vector<typename std::set<T>::iterator> iters;
  size_t tile_coords_num = 1;
  for (unsigned d = 0; d < dim_num; ++d) {
    iters.push_back(coords_set[d].begin());
    tile_coords_num *= coords_set[d].size();
  }

  tile_coords_.resize(tile_coords_num);
  std::vector<uint8_t> coords;
  auto coords_size = dim_num * array_schema->dimension(0)->coord_size();
  coords.resize(coords_size);
  size_t coord_size = sizeof(T);
  size_t tile_coords_pos = 0;
  while (iters[0] != coords_set[0].end()) {
    for (unsigned d = 0; d < dim_num; ++d)
      std::memcpy(&(coords[d * coord_size]), &(*iters[d]), coord_size);
    tile_coords_[tile_coords_pos++] = coords;

    // Advance the iterators
    auto d = (int)dim_num - 1;
    while (d >= 0) {
      iters[d]++;
      if (iters[d] != coords_set[d].end())
        break;
      if (d > 0)
        iters[d] = coords_set[d].begin();
      d--;
    }
  }

  // Compute `tile_coords_map_`
  for (size_t i = 0; i < tile_coords_.size(); ++i, ++tile_coords_pos)
    tile_coords_map_[tile_coords_[i]] = i;

  return Status::Ok();
}

Status Subarray::compute_tile_overlap() {
  STATS_START_TIMER(stats::Stats::TimerType::READ_COMPUTE_TILE_OVERLAP)

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (tile_overlap_computed_)
    return Status::Ok();

  compute_range_offsets();

  // Initialization
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  tile_overlap_.clear();
  auto meta = array_->fragment_metadata();
  auto fragment_num = meta.size();
  tile_overlap_.resize(fragment_num);
  auto range_num = this->range_num();
  for (unsigned i = 0; i < fragment_num; ++i)
    tile_overlap_[i].resize(range_num);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());

  // Compute relevant fragments to the subarray
  RETURN_NOT_OK(compute_relevant_fragments());

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // Load the R-Trees and compute tile overlap only for relevant fragments
  RETURN_NOT_OK(load_relevant_fragment_rtrees());
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  RETURN_NOT_OK(compute_relevant_fragment_tile_overlap());

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  tile_overlap_computed_ = true;

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();

  STATS_END_TIMER(stats::Stats::TimerType::READ_COMPUTE_TILE_OVERLAP)
}

Subarray Subarray::clone() const {
  Subarray clone(config_);
  clone.array_ = array_;
  clone.layout_ = layout_;
  clone.cell_order_ = cell_order_;
  clone.ranges_ = ranges_;
  clone.is_default_ = is_default_;
  clone.range_offsets_ = range_offsets_;
  clone.tile_overlap_ = tile_overlap_;
  clone.est_result_size_computed_ = est_result_size_computed_;
  clone.tile_overlap_computed_ = tile_overlap_computed_;
  clone.est_result_size_ = est_result_size_;
  clone.max_mem_size_ = max_mem_size_;
  clone.relevant_fragments_ = relevant_fragments_;
  clone.logger_ = logger_;
  clone.config_ = config_;

  const char* prefix = nullptr;
  config_.get("sm.log_prefix", &prefix);
  clone.logger_ = Logger(prefix);

  return clone;
}

TileOverlap Subarray::get_tile_overlap(uint64_t range_idx, unsigned fid) const {
  assert(array_->array_schema()->dense());
  auto type = array_->array_schema()->dimension(0)->type();
  switch (type) {
    case Datatype::INT8:
      return get_tile_overlap<int8_t>(range_idx, fid);
    case Datatype::UINT8:
      return get_tile_overlap<uint8_t>(range_idx, fid);
    case Datatype::INT16:
      return get_tile_overlap<int16_t>(range_idx, fid);
    case Datatype::UINT16:
      return get_tile_overlap<uint16_t>(range_idx, fid);
    case Datatype::INT32:
      return get_tile_overlap<int32_t>(range_idx, fid);
    case Datatype::UINT32:
      return get_tile_overlap<uint32_t>(range_idx, fid);
    case Datatype::INT64:
      return get_tile_overlap<int64_t>(range_idx, fid);
    case Datatype::UINT64:
      return get_tile_overlap<uint64_t>(range_idx, fid);
    case Datatype::FLOAT32:
      return get_tile_overlap<float>(range_idx, fid);
    case Datatype::FLOAT64:
      return get_tile_overlap<double>(range_idx, fid);
    case Datatype::DATETIME_YEAR:
    case Datatype::DATETIME_MONTH:
    case Datatype::DATETIME_WEEK:
    case Datatype::DATETIME_DAY:
    case Datatype::DATETIME_HR:
    case Datatype::DATETIME_MIN:
    case Datatype::DATETIME_SEC:
    case Datatype::DATETIME_MS:
    case Datatype::DATETIME_US:
    case Datatype::DATETIME_NS:
    case Datatype::DATETIME_PS:
    case Datatype::DATETIME_FS:
    case Datatype::DATETIME_AS:
      return get_tile_overlap<int64_t>(range_idx, fid);
    default:
      assert(false);
  }
  return TileOverlap();
}

template <class T>
TileOverlap Subarray::get_tile_overlap(uint64_t range_idx, unsigned fid) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  assert(array_->array_schema()->dense());
  TileOverlap ret;
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto ndrange = this->ndrange(range_idx);

  // Prepare a range copy
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto dim_num = array_->array_schema()->dim_num();
  std::vector<T> range_cpy;
  range_cpy.resize(2 * dim_num);
  for (unsigned d = 0; d < dim_num; ++d) {
    auto r = (const T*)ndrange[d].data();
    range_cpy[2 * d] = r[0];
    range_cpy[2 * d + 1] = r[1];
  }

  // Get tile overlap from fragment
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto meta = array_->fragment_metadata()[fid];
  auto frag_overlap = meta->compute_overlapping_tile_ids_cov<T>(&range_cpy[0]);

  // Prepare ret. Contiguous tile ids with full overlap
  // will be grouped together
  uint64_t start_tid = UINT64_MAX;  // Indicates no new range has started
  uint64_t end_tid = UINT64_MAX;    // Indicates no new range has started
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (auto o : frag_overlap) {
    // Partial overlap
    if (o.second != 1.0) {
      // Add previous range (if started) and reset
      if (start_tid != UINT64_MAX) {
        if (start_tid != end_tid)
          ret.tile_ranges_.emplace_back(start_tid, end_tid);
        else
          ret.tiles_.emplace_back(start_tid, 1.0);
        start_tid = UINT64_MAX;
        end_tid = UINT64_MAX;
      }
      // Add this tile overlap
      ret.tiles_.push_back(o);
    } else {  // Full overlap
      // New range starting
      if (start_tid == UINT64_MAX) {
        start_tid = o.first;
        end_tid = o.first;
      } else {
        if (o.first == end_tid + 1) {  // Group into previous range
          end_tid++;
        } else {
          // Add previous range
          if (start_tid != end_tid)
            ret.tile_ranges_.emplace_back(start_tid, end_tid);
          else
            ret.tiles_.emplace_back(start_tid, 1.0);
          // New range starting
          start_tid = o.first;
          end_tid = o.first;
        }
      }
    }
  }

  // Potentially add last tile range
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  if (start_tid != UINT64_MAX) {
    if (start_tid != end_tid)
      ret.tile_ranges_.emplace_back(start_tid, end_tid);
    else
      ret.tiles_.emplace_back(start_tid, 1.0);
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return ret;
}

void Subarray::swap(Subarray& subarray) {
  std::swap(array_, subarray.array_);
  std::swap(layout_, subarray.layout_);
  std::swap(cell_order_, subarray.cell_order_);
  std::swap(ranges_, subarray.ranges_);
  std::swap(is_default_, subarray.is_default_);
  std::swap(range_offsets_, subarray.range_offsets_);
  std::swap(tile_overlap_, subarray.tile_overlap_);
  std::swap(est_result_size_computed_, subarray.est_result_size_computed_);
  std::swap(tile_overlap_computed_, subarray.tile_overlap_computed_);
  std::swap(est_result_size_, subarray.est_result_size_);
  std::swap(max_mem_size_, subarray.max_mem_size_);
  std::swap(relevant_fragments_, subarray.relevant_fragments_);

  const char* prefix = nullptr;
  config_.get("sm.log_prefix", &prefix);
  subarray.logger_ = Logger(prefix);
}

Status Subarray::compute_relevant_fragments() {
  STATS_START_TIMER(stats::Stats::TimerType::READ_COMPUTE_RELEVANT_FRAGS)

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto meta = array_->fragment_metadata();
  auto fragment_num = meta.size();
  auto range_num = this->range_num();
  std::vector<uint8_t> frag_bitmap(fragment_num, 0);

  // Compute the relevant fragments
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto statuses = parallel_for_2d(
      0, fragment_num, 0, range_num, [&](unsigned f, uint64_t r) {
        if (frag_bitmap[f] == 0 &&
            meta[f]->overlaps_non_empty_domain(this->ndrange(r)))
          frag_bitmap[f] = 1;
        return Status::Ok();
      });

  // Copy to the result
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  relevant_fragments_.reserve(fragment_num);
  for (unsigned f = 0; f < fragment_num; ++f) {
    if (frag_bitmap[f])
      relevant_fragments_.emplace_back(f);
  }
  relevant_fragments_.shrink_to_fit();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();

  STATS_END_TIMER(stats::Stats::TimerType::READ_COMPUTE_RELEVANT_FRAGS)
}

Status Subarray::load_relevant_fragment_rtrees() const {
  STATS_START_TIMER(stats::Stats::TimerType::READ_LOAD_RELEVANT_RTREES)
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());

  auto meta = array_->fragment_metadata();
  auto encryption_key = array_->encryption_key();

  auto statuses = parallel_for(0, relevant_fragments_.size(), [&](uint64_t f) {
    return meta[relevant_fragments_[f]]->load_rtree(*encryption_key);
  });
  for (auto st : statuses)
    RETURN_NOT_OK(st);

    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();

  STATS_END_TIMER(stats::Stats::TimerType::READ_LOAD_RELEVANT_RTREES)
}

Status Subarray::compute_relevant_fragment_tile_overlap() {
  STATS_START_TIMER(stats::Stats::TimerType::READ_COMPUTE_RELEVANT_TILE_OVERLAP)

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  const auto& meta = array_->fragment_metadata();
  auto range_num = this->range_num();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto statuses = parallel_for(0, relevant_fragments_.size(), [&](uint64_t i) {
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    auto f = relevant_fragments_[i];
    auto dense = meta[f]->dense();
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    return compute_relevant_fragment_tile_overlap(meta[f], f, dense, range_num);
  });
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (const auto& st : statuses)
    RETURN_NOT_OK(st);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();

  STATS_END_TIMER(stats::Stats::TimerType::READ_COMPUTE_RELEVANT_TILE_OVERLAP)
}

Status Subarray::compute_relevant_fragment_tile_overlap(
    FragmentMetadata* meta, unsigned frag_idx, bool dense, uint64_t range_num) {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto num_threads = array_->num_threads();
  auto ranges_per_thread = (uint64_t)ceil((double)range_num / num_threads);
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto statuses = parallel_for(0, num_threads, [&](uint64_t t) {
    auto r_start = t * ranges_per_thread;
    auto r_end = std::min((t + 1) * ranges_per_thread - 1, range_num - 1);
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    for (uint64_t r = r_start; r <= r_end; ++r) {
      if (dense) {  // Dense fragment
        tile_overlap_[frag_idx][r] = get_tile_overlap(r, frag_idx);
      } else {  // Sparse fragment
        const auto& range = this->ndrange(r);
        RETURN_NOT_OK(
            meta->get_tile_overlap(range, &(tile_overlap_[frag_idx][r])));
      }
    }

    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    return Status::Ok();
  });
  for (const auto& st : statuses)
    RETURN_NOT_OK(st);

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

Status Subarray::load_relevant_fragment_tile_var_sizes(
    const std::vector<std::string>& names) const {
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  auto array_schema = array_->array_schema();
  auto encryption_key = array_->encryption_key();
  auto meta = array_->fragment_metadata();

  // Find the names of the var-sized dimensions or attributes
  std::vector<std::string> var_names;
  var_names.reserve(names.size());
  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  for (unsigned i = 0; i < names.size(); ++i) {
    if (array_schema->var_size(names[i]))
      var_names.emplace_back(names[i]);
  }

  // No var-sized attributes/dimensions in `names`
  if (var_names.empty())
    return Status::Ok();

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  // Load all metadata for tile var sizes among fragments.
  for (const auto& var_name : var_names) {
    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    const auto statuses =
        parallel_for(0, relevant_fragments_.size(), [&](const size_t i) {
          auto f = relevant_fragments_[i];
          return meta[f]->load_tile_var_sizes(*encryption_key, var_name);
        });

    logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
    for (const auto& st : statuses)
      RETURN_NOT_OK(st);
  }

  logger_.error((__FILE__ + std::string(":") + std::to_string(__LINE__)).c_str());
  return Status::Ok();
}

// Explicit instantiations
template Status Subarray::compute_tile_coords<int8_t>();
template Status Subarray::compute_tile_coords<uint8_t>();
template Status Subarray::compute_tile_coords<int16_t>();
template Status Subarray::compute_tile_coords<uint16_t>();
template Status Subarray::compute_tile_coords<int32_t>();
template Status Subarray::compute_tile_coords<uint32_t>();
template Status Subarray::compute_tile_coords<int64_t>();
template Status Subarray::compute_tile_coords<uint64_t>();
template Status Subarray::compute_tile_coords<float>();
template Status Subarray::compute_tile_coords<double>();

template const int8_t* Subarray::tile_coords_ptr<int8_t>(
    const std::vector<int8_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const uint8_t* Subarray::tile_coords_ptr<uint8_t>(
    const std::vector<uint8_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const int16_t* Subarray::tile_coords_ptr<int16_t>(
    const std::vector<int16_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const uint16_t* Subarray::tile_coords_ptr<uint16_t>(
    const std::vector<uint16_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const int32_t* Subarray::tile_coords_ptr<int32_t>(
    const std::vector<int32_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const uint32_t* Subarray::tile_coords_ptr<uint32_t>(
    const std::vector<uint32_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const int64_t* Subarray::tile_coords_ptr<int64_t>(
    const std::vector<int64_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const uint64_t* Subarray::tile_coords_ptr<uint64_t>(
    const std::vector<uint64_t>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const float* Subarray::tile_coords_ptr<float>(
    const std::vector<float>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;
template const double* Subarray::tile_coords_ptr<double>(
    const std::vector<double>& tile_coords,
    std::vector<uint8_t>* aux_tile_coords) const;

template Subarray Subarray::crop_to_tile<int8_t>(
    const int8_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<uint8_t>(
    const uint8_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<int16_t>(
    const int16_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<uint16_t>(
    const uint16_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<int32_t>(
    const int32_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<uint32_t>(
    const uint32_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<int64_t>(
    const int64_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<uint64_t>(
    const uint64_t* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<float>(
    const float* tile_coords, Layout layout) const;
template Subarray Subarray::crop_to_tile<double>(
    const double* tile_coords, Layout layout) const;

}  // namespace sm
}  // namespace tiledb
