/**
 * @file   arrow_io_impl.h
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2020 TileDB, Inc.
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
 * This file defines TileDB interoperation functionality with Apache Arrow.
 */

#ifndef TILEDB_ARROW_H
#define TILEDB_ARROW_H

/* ************************************************************************ */
/*
 * Arrow C Data Interface
 * Apache License 2.0
 * source: https://arrow.apache.org/docs/format/CDataInterface.html
 */

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
  // Array type description
  const char* format;
  const char* name;
  const char* metadata;
  int64_t flags;
  int64_t n_children;
  struct ArrowSchema** children;
  struct ArrowSchema* dictionary;

  // Release callback
  void (*release)(struct ArrowSchema*);
  // Opaque producer-specific data
  void* private_data;
};

struct ArrowArray {
  // Array data description
  int64_t length;
  int64_t null_count;
  int64_t offset;
  int64_t n_buffers;
  int64_t n_children;
  const void** buffers;
  struct ArrowArray** children;
  struct ArrowArray* dictionary;

  // Release callback
  void (*release)(struct ArrowArray*);
  // Opaque producer-specific data
  void* private_data;
};
/* End Arrow C API */
/* ************************************************************************ */

/* ************************************************************************ */
/* Begin TileDB Arrow IO internal implementation */

#include <memory>

/* ****************************** */
/*      Error context helper      */
/* ****************************** */

using _TileDBError = tiledb::TileDBError;
#ifndef NDEBUG
#define TDB_LERROR(m)                                                     \
  _TileDBError(                                                           \
      std::string(m) + " (" + __FILE__ + ":" + std::to_string(__LINE__) + \
      ")");
#else
#define TDB_LERROR tiledb::TileDBError
#endif

namespace tiledb {
namespace arrow {

/* ****************************** */
/*       Helper types             */
/* ****************************** */

// Arrow format and representation
struct ArrowInfo {
  ArrowInfo(std::string fmt, const std::string& rep = std::string())
      : fmt_(fmt)
      , rep_(rep){};

  std::string fmt_;
  std::string rep_;
};

// TileDB type information
struct TypeInfo {
  tiledb_datatype_t type;
  uint64_t elem_size;
  uint32_t cell_val_num;

  // is this represented as "Arrow large"
  bool arrow_large;
};

struct BufferInfo {
  TypeInfo tdbtype;
  bool is_var;
  uint64_t elem_num;    // element count
  void* data;           // data pointer
  uint64_t offset_num;  // # offsets, always uint64_t
  uint64_t* offsets;
  uint64_t elem_size;
};

/* ****************************** */
/*        Type conversions        */
/* ****************************** */

// Get Arrow format from TileDB BufferInfo
ArrowInfo tiledb_buffer_arrow_fmt(BufferInfo bufferinfo, bool use_list = true) {
  auto typeinfo = bufferinfo.tdbtype;
  auto cell_val_num = typeinfo.cell_val_num;

  // TODO support List<T> for simple scalar T
  (void)use_list;
  /*
  if (use_list && cell_val_num == TILEDB_VAR_NUM) {
    switch(typeinfo.type) {
      case TILEDB_STRING_UTF8:
      case TILEDB_STRING_ASCII:
        break;
      case TILEDB_INT8:
      case TILEDB_INT16:
      case TILEDB_INT32:
      case TILEDB_INT64:
      case TILEDB_UINT8:
      case TILEDB_UINT16:
      case TILEDB_UINT32:
      case TILEDB_UINT64:
      case TILEDB_FLOAT32:
      case TILEDB_FLOAT64:
        return ArrowInfo("+l");
      default:
        throw TDB_LERROR(
          "TileDB-Arrow: List<T> translation not yet supported for var-length
  TileDB type ('"
          + tiledb::impl::type_to_str(typeinfo.type) + "')");
    }
  }
  */

  switch (typeinfo.type) {
    ////////////////////////////////////////////////////////////////////////
    // NOTE: should export as arrow's "large utf8" because TileDB offsets
    //       are natively uint64_t. However: our offset buffers are 1 elem
    //       short of the expected length for an arrow offset buffer.
    //       For now, we transform to int32_it in place and export "u"
    case TILEDB_STRING_ASCII:
    case TILEDB_STRING_UTF8:
      return ArrowInfo("u");
    case TILEDB_CHAR:
      return ArrowInfo("z");
    case TILEDB_INT32:
      return ArrowInfo("i");
    case TILEDB_INT64:
      return ArrowInfo("l");
    case TILEDB_FLOAT32:
      return ArrowInfo("f");
    case TILEDB_FLOAT64:
      return ArrowInfo("g");
    case TILEDB_INT8:
      return ArrowInfo("c");
    case TILEDB_UINT8:
      return ArrowInfo("C");
    case TILEDB_INT16:
      return ArrowInfo("s");
    case TILEDB_UINT16:
      return ArrowInfo("S");
    case TILEDB_UINT32:
      return ArrowInfo("I");
    case TILEDB_UINT64:
      return ArrowInfo("L");

    // make sure this matches below
    case TILEDB_DATETIME_NS:
      return ArrowInfo("tsn:");
    case TILEDB_DATETIME_MS:
      return ArrowInfo("tdm");

    // TODO: these could potentially be rep'd w/ additional
    //       language-specific metadata
    case TILEDB_DATETIME_YEAR:
    case TILEDB_DATETIME_MONTH:
    case TILEDB_DATETIME_WEEK:
    case TILEDB_DATETIME_DAY:
    case TILEDB_DATETIME_HR:
    case TILEDB_DATETIME_MIN:
    case TILEDB_DATETIME_SEC:
    case TILEDB_DATETIME_US:
    case TILEDB_DATETIME_PS:
    case TILEDB_DATETIME_FS:
    case TILEDB_DATETIME_AS:
    case TILEDB_STRING_UTF16:
    case TILEDB_STRING_UTF32:
    case TILEDB_STRING_UCS2:
    case TILEDB_STRING_UCS4:
    case TILEDB_ANY:
    default:
      break;
  }
  throw TDB_LERROR(
      "TileDB-Arrow: tiledb datatype not understood ('" +
      tiledb::impl::type_to_str(typeinfo.type) +
      "', cell_val_num: " + std::to_string(cell_val_num) + ")");
}

TypeInfo arrow_type_to_tiledb(ArrowSchema* arw_schema) {
  auto fmt = std::string(arw_schema->format);
  bool large = false;
  if (fmt == "+l") {
    large = false;
    assert(arw_schema->n_children == 1);
    arw_schema = arw_schema->children[0];
  } else if (fmt == "+L") {
    large = true;
    assert(arw_schema->n_children == 1);
    arw_schema = arw_schema->children[0];
  }

  if (fmt == "i")
    return {TILEDB_INT32, 4, 1, large};
  else if (fmt == "l")
    return {TILEDB_INT64, 8, 1, large};
  else if (fmt == "f")
    return {TILEDB_FLOAT32, 4, 1, large};
  else if (fmt == "g")
    return {TILEDB_FLOAT64, 8, 1, large};
  else if (fmt == "c")
    return {TILEDB_INT8, 1, 1, large};
  else if (fmt == "C")
    return {TILEDB_UINT8, 1, 1, large};
  else if (fmt == "s")
    return {TILEDB_INT16, 2, 1, large};
  else if (fmt == "S")
    return {TILEDB_UINT16, 2, 1, large};
  else if (fmt == "I")
    return {TILEDB_UINT32, 4, 1, large};
  else if (fmt == "L")
    return {TILEDB_UINT64, 8, 1, large};
  // this is kind of a hack
  // technically 'tsn:' is timezone-specific, which we don't support
  // however, the blank (no suffix) base is interconvertible w/ np.datetime64
  else if (fmt == "tsn:")
    return {TILEDB_DATETIME_NS, 8, 1, large};
  else if (fmt == "z" || fmt == "Z")
    return {TILEDB_CHAR, 1, TILEDB_VAR_NUM, fmt == "Z"};
  else if (fmt == "u" || fmt == "U")
    return {TILEDB_STRING_UTF8, 1, TILEDB_VAR_NUM, fmt == "U"};
  else
    throw tiledb::TileDBError(
        "[TileDB-Arrow]: Unknown or unsupported Arrow format string '" + fmt +
        "'");
}

TypeInfo tiledb_dt_info(
    const tiledb::ArraySchema& schema, const std::string& name) {
  if (schema.has_attribute(name)) {
    auto attr = schema.attribute(name);

    auto retval = TypeInfo();
    retval.type = attr.type(),
    retval.elem_size = tiledb::impl::type_size(attr.type()),
    retval.cell_val_num = attr.cell_val_num(), retval.arrow_large = false;
    return retval;
  } else if (schema.domain().has_dimension(name)) {
    auto dom = schema.domain();
    auto dim = dom.dimension(name);

    auto retval = TypeInfo();
    retval.type = dim.type();
    retval.elem_size = tiledb::impl::type_size(dim.type());
    retval.cell_val_num = dim.cell_val_num();
    retval.arrow_large = false;
    return retval;
  } else {
    throw TDB_LERROR("Schema does not have attribute named '" + name + "'");
  }
}

/* ****************************** */
/*        Helper functions        */
/* ****************************** */

// Reorder offsets into Arrow-compatible format
// NOTE: this currently hard-codes an inplace conversion from
//       uint64 (TileDB) to int32 ("arrow small") only
void offsets_to_arrow(const BufferInfo& binfo) {
  size_t elem_size = binfo.elem_size;

  uint64_t* offsets = binfo.offsets;
  int32_t* offsets_i32 = reinterpret_cast<int32_t*>(binfo.offsets);

  uint64_t idx = 1;  // important: don't divide by 0
  while (idx < binfo.offset_num) {
    // we must check for zeros here to handle a run of empty
    // values (zero-length) at the start of the buffer
    // note that given the i32 conversion, we can simply
    // skip the assignment because
    //   offsets[idx] == 0 => offsets_i32[i:i+1] == 0
    if (offsets[idx] != 0)
      offsets_i32[idx] = static_cast<int32_t>(offsets[idx] / elem_size);
    ++idx;
  }
  offsets_i32[idx] = binfo.elem_num;
}

void check_arrow_schema(const ArrowSchema* arw_schema) {
  if (arw_schema == nullptr)
    TDB_LERROR("[ArrowIO]: Invalid ArrowSchema object!");

  // sanity check the arrow schema
  if (arw_schema->release == nullptr)
    TDB_LERROR(
        "[ArrowIO]: Invalid ArrowSchema: cannot import released schema.");
  if (arw_schema->format != std::string("+s"))
    TDB_LERROR("[ArrowIO]: Unsupported ArrowSchema: must be struct (+s).");
  if (arw_schema->n_children < 1)
    TDB_LERROR("[ArrowIO]: Unsupported ArrowSchema with 0 children.");
  if (arw_schema->children == nullptr)
    TDB_LERROR(
        "[ArrowIO]: Invalid ArrowSchema with n_children>0 and children==NULL");
}

/* ****************************** */
/*  Arrow C API Struct wrappers   */
/* ****************************** */

// NOTE: These structs manage the lifetime of the contained C structs.
// CAUTION: they do *not* manage the lifetime of the underlying buffers.

struct CPPArrowSchema {
  /*
   * Initialize a CPPArrowSchema object
   *
   * The lifetime of this object is controlled by the
   * release callback set in the ArrowSchema.
   *
   * Note that an ArrowSchema is *movable*, provided
   * the release callback of the source is set to null.
   */
  CPPArrowSchema(
      std::string name,
      std::string format,
      std::string metadata,
      int64_t flags,
      std::vector<ArrowSchema*> children,
      std::shared_ptr<CPPArrowSchema> dictionary)
      : format_(format)
      , name_(name)
      , metadata_(metadata)
      , children_(children)
      , dictionary_(dictionary) {
    flags_ = flags;
    n_children_ = children.size();

    schema_ = static_cast<ArrowSchema*>(std::malloc(sizeof(ArrowSchema)));
    if (schema_ == nullptr)
      throw tiledb::TileDBError("Failed to allocate ArrowSchema");

    // Initialize ArrowSchema with data *owned by this object*
    // Type description
    schema_->format = format_.c_str();
    schema_->name = name_.c_str();
    schema_->metadata = metadata.c_str();
    schema_->flags = flags;
    schema_->n_children = n_children_;

    // Cross-refs
    schema_->children = nullptr;
    schema_->dictionary = nullptr;

    // Release callback
    schema_->release = ([](ArrowSchema* schema_p) {
      assert(schema_p->release != nullptr);

      // Release children
      for (int64_t i = 0; i < schema_p->n_children; i++) {
        ArrowSchema* child_schema = schema_p->children[i];
        child_schema->release(child_schema);
        assert(child_schema->release == nullptr);
      }
      // Release dictionary struct
      struct ArrowSchema* dict = schema_p->dictionary;
      if (dict != nullptr && dict->release != nullptr) {
        dict->release(dict);
        assert(dict->release == nullptr);
      }

      // mark the ArrowSchema struct as released
      schema_p->release = nullptr;

      delete static_cast<CPPArrowSchema*>(schema_p->private_data);
    });

    // Private data for release callback
    schema_->private_data = this;

    if (n_children_ > 0) {
      schema_->children = static_cast<ArrowSchema**>(children.data());
    }

    if (dictionary) {
      schema_->dictionary = dictionary.get()->ptr();
    }
  }

  /*
   * CPPArrowSchema destructor
   *
   * This destructor is invoked via the ArrowSchema.release
   * callback. Owned member data is released via default destructors.
   */
  ~CPPArrowSchema() {
    if (schema_ != nullptr)
      std::free(schema_);
  };

  /*
   * Exports the ArrowSchema to a pre-allocated target struct
   *
   * This function frees the array_.
   * The lifetime of all other member variables is controlled
   * by the ArrowSchema.release callback, which frees the
   * CPPArrowSchema structure (via ArrowSchema.private_data).
   */
  void export_ptr(ArrowSchema* out_schema) {
    assert(out_schema != nullptr);
    memcpy(out_schema, schema_, sizeof(ArrowSchema));
    std::free(schema_);
    schema_ = nullptr;
  }

  ArrowSchema* mutable_ptr() {
    assert(schema_ != nullptr);
    return schema_;
  }

  ArrowSchema* ptr() const {
    assert(schema_ != nullptr);
    return schema_;
  }

 private:
  ArrowSchema* schema_;
  std::string format_;
  std::string name_;
  std::string metadata_;
  int64_t flags_;
  int64_t n_children_;
  std::vector<ArrowSchema*> children_;
  std::shared_ptr<CPPArrowSchema> dictionary_;
};

struct CPPArrowArray {
  /*
   * Initialize a CPPArrowSchema object
   *
   * The lifetime of this object is controlled by the
   * release callback set in the ArrowSchema.
   *
   * Note that an ArrowSchema is *movable*, provided
   * the release callback of the source is set to null.
   */
  CPPArrowArray(
      int64_t elem_num,
      int64_t null_num,
      int64_t offset,
      std::vector<std::shared_ptr<CPPArrowArray>> children,
      std::vector<void*> buffers) {
    array_ = static_cast<ArrowArray*>(std::malloc(sizeof(ArrowArray)));
    if (array_ == nullptr)
      throw tiledb::TileDBError("Failed to allocate ArrowArray");

    // Data description
    array_->length = elem_num;
    array_->null_count = null_num;
    array_->offset = offset;
    array_->n_buffers = static_cast<int64_t>(buffers.size());
    array_->n_children = static_cast<int64_t>(children.size());
    array_->buffers = nullptr;
    array_->children = nullptr;
    array_->dictionary = nullptr;
    // Bookkeeping
    array_->release = ([](ArrowArray* array_p) {
      assert(array_p->release != nullptr);

      // Release children
      for (int64_t i = 0; i < array_p->n_children; i++) {
        ArrowArray* child_array = array_p->children[i];
        child_array->release(child_array);
        assert(child_array->release == nullptr);
      }

      // Release dictionary
      struct ArrowArray* dict = array_p->dictionary;
      if (dict != nullptr && dict->release != nullptr) {
        dict->release(dict);
        assert(dict->release == nullptr);
      }

      // mark the ArrowArray struct as released
      array_p->release = nullptr;

      delete static_cast<CPPArrowArray*>(array_p->private_data);
    });
    array_->private_data = this;

    buffers_ = buffers;
    array_->buffers = const_cast<const void**>(buffers_.data());
  }

  /*
   * CPPArrowArray destructor
   *
   * This destructor is invoked via the ArrowArray.release
   * callback. Owned member data is released via default destructors.
   */
  ~CPPArrowArray() {
    if (array_ != nullptr) {
      // did not export
      std::free(array_);
    }
  }

  void export_ptr(ArrowArray* out_array) {
    assert(out_array != nullptr);
    memcpy(out_array, array_, sizeof(ArrowArray));
    std::free(array_);
    array_ = nullptr;
  }

  ArrowArray* ptr() const {
    assert(array_ != nullptr);
    return array_;
  }

  ArrowArray* mutable_ptr() {
    assert(array_ != nullptr);
    return array_;
  }

 private:
  ArrowArray* array_;
  std::vector<void*> buffers_;
};

/* ****************************** */
/*         Arrow Importer         */
/* ****************************** */

class ArrowImporter {
 public:
  ArrowImporter(std::shared_ptr<tiledb::Query> query);
  ~ArrowImporter();

  void import_(std::string name, ArrowArray* array, ArrowSchema* schema);

 private:
  std::shared_ptr<tiledb::Query> query_;
  std::vector<void*> offset_buffers_;

};  // class ArrowExporter

ArrowImporter::ArrowImporter(std::shared_ptr<tiledb::Query> query) {
  query_ = query;
}

ArrowImporter::~ArrowImporter() {
  for (auto p : offset_buffers_) {
    std::free(p);
  }
}

void ArrowImporter::import_(
    std::string name, ArrowArray* arw_array, ArrowSchema* arw_schema) {
  auto typeinfo = arrow_type_to_tiledb(arw_schema);

  // buffer conversion

  if (typeinfo.cell_val_num == TILEDB_VAR_NUM) {
    assert(arw_array->n_buffers == 3);

    void* p_offsets_arw = const_cast<void*>(arw_array->buffers[1]);
    void* p_data = const_cast<void*>(arw_array->buffers[2]);
    uint64_t data_num = arw_array->length;  // <- number of elements
    uint64_t data_nbytes = 0;

    uint64_t* p_offsets = static_cast<uint64_t*>(
        std::malloc(arw_array->length * sizeof(uint64_t)));
    offset_buffers_.push_back(static_cast<void*>(p_offsets));

    if (typeinfo.arrow_large) {
      int64_t* p_offsets_int64 = static_cast<int64_t*>(p_offsets_arw);
      // the Arrow offsets buffer has length+1 elements
      for (size_t i = 0; i < static_cast<size_t>(data_num); i++) {
        p_offsets[i] = p_offsets_int64[i] * typeinfo.elem_size;
      }
      data_nbytes = p_offsets_int64[data_num] * typeinfo.elem_size;
    } else {
      int32_t* p_offsets_int32 = static_cast<int32_t*>(p_offsets_arw);
      // the Arrow offsets buffer has length+1 elements
      for (size_t i = 0; i < static_cast<size_t>(data_num); i++) {
        p_offsets[i] = p_offsets_int32[i] * typeinfo.elem_size;
      }
      data_nbytes = p_offsets_int32[data_num] * typeinfo.elem_size;
    }
    query_->set_buffer(
        name,
        const_cast<uint64_t*>(p_offsets),
        data_num,
        const_cast<void*>(p_data),
        data_nbytes);
  } else {
    // fixed-size attribute (not TILEDB_VAR_NUM)
    assert(arw_array->n_buffers == 2);

    void* p_data = const_cast<void*>(arw_array->buffers[1]);
    uint64_t data_num = arw_array->length;

    query_->set_buffer(name, static_cast<void*>(p_data), data_num);
  }
}

/* ****************************** */
/*         Arrow Exporter         */
/* ****************************** */

class ArrowExporter {
 public:
  ArrowExporter(std::shared_ptr<tiledb::Query> query);

  void export_(const std::string& name, ArrowArray* array, ArrowSchema* schema);

  BufferInfo buffer_info(const std::string& name);

 private:
  std::shared_ptr<tiledb::Query> query_;
};

// ArrowExporter implementation
ArrowExporter::ArrowExporter(std::shared_ptr<tiledb::Query> query) {
  query_ = query;
}

BufferInfo ArrowExporter::buffer_info(const std::string& name) {
  void* data = nullptr;
  uint64_t data_nelem = 0;
  uint64_t* offsets = nullptr;
  uint64_t offsets_nelem = 0;
  uint64_t elem_size = 0;

  auto typeinfo = tiledb_dt_info(query_->array().schema(), name);

  auto result_elts = query_->result_buffer_elements();
  auto result_elt_iter = result_elts.find(name);
  if (result_elt_iter == result_elts.end()) {
    TDB_LERROR("No results found for attribute '" + name + "'");
  }

  bool is_var = (result_elt_iter->second.first != 0);

  // NOTE: result sizes are in bytes
  if (is_var) {
    query_->get_buffer(
        name, &offsets, &offsets_nelem, &data, &data_nelem, &elem_size);
  } else {
    query_->get_buffer(name, &data, &data_nelem, &elem_size);
  }

  auto retval = BufferInfo();
  retval.tdbtype = typeinfo;
  retval.is_var = is_var;
  retval.elem_num = data_nelem;
  retval.data = data;
  retval.offset_num = (is_var ? offsets_nelem : 1);
  retval.offsets = offsets;
  retval.elem_size = elem_size;

  return retval;
}

int64_t flags_for_buffer(BufferInfo binfo) {
  /*  TODO, use these defs from arrow_cdefs.h -- currently not applicable.
      #define ARROW_FLAG_DICTIONARY_ORDERED 1
      #define ARROW_FLAG_NULLABLE 2
      #define ARROW_FLAG_MAP_KEYS_SORTED 4
  */
  (void)binfo;
  return 0;
}

void ArrowExporter::export_(
    const std::string& name, ArrowArray* array, ArrowSchema* schema) {
  auto bufferinfo = this->buffer_info(name);
  // TODO add checks?:
  // -  .elem_num < INT64_MAX
  // - check that the length of a var-len element does not exceed INT32_MAX
  //   currently we export as "u" and "b" (int32 offsets) rather than
  //   "U and "B" (int64 offsets)

  if (schema == nullptr || array == nullptr) {
    throw tiledb::TileDBError(
        "ArrowExporter: received invalid pointer to output array or schema.");
  }

  auto arrow_fmt = tiledb_buffer_arrow_fmt(bufferinfo);
  auto arrow_flags = flags_for_buffer(bufferinfo);

  // lifetime:
  //   - address is stored in ArrowSchema.private_data
  //   - delete is called by lambda stored in ArrowSchema.release
  CPPArrowSchema* cpp_schema =
      new CPPArrowSchema(name, arrow_fmt.fmt_, "", arrow_flags, {}, {});

  std::vector<void*> buffers;
  if (bufferinfo.is_var) {
    offsets_to_arrow(bufferinfo);  // convert in-place
    buffers = {nullptr, bufferinfo.offsets, bufferinfo.data};
  } else {
    cpp_schema =
        new CPPArrowSchema(name, arrow_fmt.fmt_, "", arrow_flags, {}, {});
    buffers = {nullptr, bufferinfo.data};
  }
  cpp_schema->export_ptr(schema);

  auto cpp_arrow_array = new CPPArrowArray(
      (bufferinfo.is_var ? bufferinfo.offset_num :
                           bufferinfo.elem_num),  // elem_num
      0,                                          // null_num
      0,                                          // offset
      {},                                         // children
      buffers);
  cpp_arrow_array->export_ptr(array);
}

/* End TileDB Arrow IO internal implementation */
/* ************************************************************************ */

/* ************************************************************************ */
/* Begin TileDB Arrow IO public API implementation */

ArrowAdapter::ArrowAdapter(std::shared_ptr<tiledb::Query> query)
    : importer_(nullptr)
    , exporter_(nullptr) {
  importer_ = new ArrowImporter(query);
  if (!importer_) {
    throw tiledb::TileDBError(
        "[TileDB-Arrow] Failed to allocate ArrowImporter!");
  }
  exporter_ = new ArrowExporter(query);
  if (!exporter_) {
    throw tiledb::TileDBError(
        "[TileDB-Arrow] Failed to allocate ArrowImporter!");
  }
}

void ArrowAdapter::export_buffer(
    const char* name, void* arrow_array, void* arrow_schema) {
  exporter_->export_(
      name, (ArrowArray*)arrow_array, (ArrowSchema*)arrow_schema);
}

void ArrowAdapter::import_buffer(
    const char* name, void* arrow_array, void* arrow_schema) {
  importer_->import_(
      name, (ArrowArray*)arrow_array, (ArrowSchema*)arrow_schema);
}

ArrowAdapter::~ArrowAdapter() {
  if (importer_)
    delete importer_;
  if (exporter_)
    delete exporter_;
}

void query_get_buffer_arrow_array(
    std::shared_ptr<Query> query,
    std::string name,
    void* v_arw_array,
    void* v_arw_schema) {
  ArrowExporter exporter(query);

  exporter.export_(name, (ArrowArray*)v_arw_array, (ArrowSchema*)v_arw_schema);
}

void query_set_buffer_arrow_array(
    std::shared_ptr<Query> query,
    std::string name,
    void* v_arw_array,
    void* v_arw_schema) {
  auto arw_schema = (ArrowSchema*)v_arw_schema;
  auto arw_array = (ArrowArray*)v_arw_array;
  check_arrow_schema(arw_schema);

  ArrowImporter importer(query);
  importer.import_(name, arw_array, arw_schema);
}

};  // end namespace arrow
};  // end namespace tiledb

/* End TileDB Arrow IO public API implementation */
/* ************************************************************************ */

#endif  // TILEDB_ARROW_H