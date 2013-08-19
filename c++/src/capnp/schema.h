// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CAPNP_SCHEMA_H_
#define CAPNP_SCHEMA_H_

#include <capnp/schema2.capnp.h>

namespace capnp {

class Schema;
class StructSchema;
class EnumSchema;
class InterfaceSchema;
class ListSchema;

template <typename T, Kind k = kind<T>()> struct SchemaType_ { typedef Schema Type; };
template <typename T> struct SchemaType_<T, Kind::PRIMITIVE> { typedef schema2::Type::Which Type; };
template <typename T> struct SchemaType_<T, Kind::BLOB> { typedef schema2::Type::Which Type; };
template <typename T> struct SchemaType_<T, Kind::ENUM> { typedef EnumSchema Type; };
template <typename T> struct SchemaType_<T, Kind::STRUCT> { typedef StructSchema Type; };
template <typename T> struct SchemaType_<T, Kind::INTERFACE> { typedef InterfaceSchema Type; };
template <typename T> struct SchemaType_<T, Kind::LIST> { typedef ListSchema Type; };

template <typename T>
using SchemaType = typename SchemaType_<T>::Type;
// SchemaType<T> is the type of T's schema, e.g. StructSchema if T is a struct.

class Schema {
  // Convenience wrapper around capnp::schema::Node.

public:
  inline Schema(): raw(nullptr) {}

  template <typename T>
  static inline SchemaType<T> from() { return SchemaType<T>::template fromImpl<T>(); }
  // Get the Schema for a particular compiled-in type.

  schema2::Node::Reader getProto() const;

  kj::ArrayPtr<const word> asUncheckedMessage() const;
  // Get the encoded schema node content as a single message segment.  It is safe to read as an
  // unchecked message.

  Schema getDependency(uint64_t id) const;
  // Gets the Schema for one of this Schema's dependencies.  For example, if this Schema is for a
  // struct, you could look up the schema for one of its fields' types.  Throws an exception if this
  // schema doesn't actually depend on the given id.  Note that annotation declarations are not
  // considered dependencies for this purpose.

  StructSchema asStruct() const;
  EnumSchema asEnum() const;
  InterfaceSchema asInterface() const;
  // Cast the Schema to a specific type.  Throws an exception if the type doesn't match.

  inline bool operator==(const Schema& other) const { return raw == other.raw; }
  inline bool operator!=(const Schema& other) const { return raw != other.raw; }
  // Determine whether two Schemas are wrapping the exact same underlying data, by identity.  If
  // you want to check if two Schemas represent the same type (but possibly different versions of
  // it), compare their IDs instead.

  template <typename T>
  void requireUsableAs() const;
  // Throws an exception if a value with this Schema cannot safely be cast to a native value of
  // the given type.  This passes if either:
  // - *this == from<T>()
  // - This schema was loaded with SchemaLoader, the type ID matches typeId<T>(), and
  //   loadCompiledTypeAndDependencies<T>() was called on the SchemaLoader.

private:
  const _::RawSchema* raw;

  inline explicit Schema(const _::RawSchema* raw): raw(raw) {
    KJ_IREQUIRE(raw->lazyInitializer == nullptr,
        "Must call ensureInitialized() on RawSchema before constructing Schema.");
  }

  template <typename T> static inline Schema fromImpl() {
    return Schema(&_::rawSchema<T>());
  }

  void requireUsableAs(const _::RawSchema* expected) const;

  friend class StructSchema;
  friend class EnumSchema;
  friend class InterfaceSchema;
  friend class ListSchema;
  friend class SchemaLoader;
};

// -------------------------------------------------------------------

class StructSchema: public Schema {
public:
  StructSchema() = default;

  class Field;
  class FieldList;
  class FieldSubset;

  FieldList getFields() const;
  // List top-level fields of this struct.  This list will contain top-level groups (including
  // named unions) but not the members of those groups.  The list does, however, contain the
  // members of the unnamed union, if there is one.

  FieldSubset getUnionFields() const;
  // If the field contains an unnamed union, get a list of fields in the union, ordered by
  // ordinal.  Since discriminant values are assigned sequentially by ordinal, you may index this
  // list by discriminant value.

  FieldSubset getNonUnionFields() const;
  // Get the fields of this struct which are not in an unnamed union, ordered by ordinal.

  kj::Maybe<Field> findFieldByName(kj::StringPtr name) const;
  // Find the field with the given name, or return null if there is no such field.  If the struct
  // contains an unnamed union, then this will find fields of that union in addition to fields
  // of the outer struct, since they exist in the same namespace.  It will not, however, find
  // members of groups (including named unions) -- you must first look up the group itself,
  // then dig into its type.

  Field getFieldByName(kj::StringPtr name) const;
  // Like findFieldByName() but throws an exception on failure.

  kj::Maybe<Field> getFieldByDiscriminant(uint16_t discriminant) const;
  // Finds the field whose `discriminantValue` is equal to the given value, or returns null if
  // there is no such field.  (If the schema does not represent a union or a struct containing
  // an unnamed union, then this always returns null.)

private:
  StructSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline StructSchema fromImpl() {
    return StructSchema(&_::rawSchema<T>());
  }
  friend class Schema;
  friend kj::StringTree _::structString(
      _::StructReader reader, const _::RawSchema& schema);
  friend kj::StringTree _::unionString(
      _::StructReader reader, const _::RawSchema& schema, uint fieldIndex);
};

class StructSchema::Field {
public:
  Field() = default;

  inline schema2::Field::Reader getProto() const { return proto; }
  inline StructSchema getContainingStruct() const { return parent; }

  inline uint getIndex() const { return index; }
  // Get the index of this field within the containing struct or union.

  uint32_t getDefaultValueSchemaOffset() const;
  // For struct, list, and object fields, returns the offset, in words, within the first segment of
  // the struct's schema, where this field's default value pointer is located.  The schema is
  // always stored as a single-segment unchecked message, which in turn means that the default
  // value pointer itself can be treated as the root of an unchecked message -- if you know where
  // to find it, which is what this method helps you with.
  //
  // For blobs, returns the offset of the begging of the blob's content within the first segment of
  // the struct's schema.
  //
  // This is primarily useful for code generators.  The C++ code generator, for example, embeds
  // the entire schema as a raw word array within the generated code.  Of course, to implement
  // field accessors, it needs access to those fields' default values.  Embedding separate copies
  // of those default values would be redundant since they are already included in the schema, but
  // seeking through the schema at runtime to find the default values would be ugly.  Instead,
  // the code generator can use getDefaultValueSchemaOffset() to find the offset of the default
  // value within the schema, and can simply apply that offset at runtime.
  //
  // If the above does not make sense, you probably don't need this method.

  inline bool operator==(const Field& other) const;
  inline bool operator!=(const Field& other) const { return !(*this == other); }

private:
  StructSchema parent;
  uint index;
  schema2::Field::Reader proto;

  inline Field(StructSchema parent, uint index, schema2::Field::Reader proto)
      : parent(parent), index(index), proto(proto) {}

  friend class StructSchema;
};

class StructSchema::FieldList {
public:
  FieldList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Field operator[](uint index) const { return Field(parent, index, list[index]); }

  typedef _::IndexingIterator<const FieldList, Field> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  StructSchema parent;
  List<schema2::Field>::Reader list;

  inline FieldList(StructSchema parent, List<schema2::Field>::Reader list)
      : parent(parent), list(list) {}

  friend class StructSchema;
};

class StructSchema::FieldSubset {
public:
  FieldSubset() = default;  // empty list

  inline uint size() const { return size_; }
  inline Field operator[](uint index) const {
    return Field(parent, indices[index], list[indices[index]]);
  }

  typedef _::IndexingIterator<const FieldSubset, Field> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  StructSchema parent;
  List<schema2::Field>::Reader list;
  const uint16_t* indices;
  uint size_;

  inline FieldSubset(StructSchema parent, List<schema2::Field>::Reader list,
                     const uint16_t* indices, uint size)
      : parent(parent), list(list), indices(indices), size_(size) {}

  friend class StructSchema;
};

// -------------------------------------------------------------------

class EnumSchema: public Schema {
public:
  EnumSchema() = default;

  class Enumerant;
  class EnumerantList;

  EnumerantList getEnumerants() const;

  kj::Maybe<Enumerant> findEnumerantByName(kj::StringPtr name) const;

  Enumerant getEnumerantByName(kj::StringPtr name) const;
  // Like findEnumerantByName() but throws an exception on failure.

private:
  EnumSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline EnumSchema fromImpl() {
    return EnumSchema(&_::rawSchema<T>());
  }
  friend class Schema;
};

class EnumSchema::Enumerant {
public:
  Enumerant() = default;

  inline schema2::Enumerant::Reader getProto() const { return proto; }
  inline EnumSchema getContainingEnum() const { return parent; }

  inline uint16_t getOrdinal() const { return ordinal; }
  inline uint getIndex() const { return ordinal; }

  inline bool operator==(const Enumerant& other) const;
  inline bool operator!=(const Enumerant& other) const { return !(*this == other); }

private:
  EnumSchema parent;
  uint16_t ordinal;
  schema2::Enumerant::Reader proto;

  inline Enumerant(EnumSchema parent, uint16_t ordinal, schema2::Enumerant::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class EnumSchema;
};

class EnumSchema::EnumerantList {
public:
  EnumerantList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Enumerant operator[](uint index) const { return Enumerant(parent, index, list[index]); }

  typedef _::IndexingIterator<const EnumerantList, Enumerant> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  EnumSchema parent;
  List<schema2::Enumerant>::Reader list;

  inline EnumerantList(EnumSchema parent, List<schema2::Enumerant>::Reader list)
      : parent(parent), list(list) {}

  friend class EnumSchema;
};

// -------------------------------------------------------------------

class InterfaceSchema: public Schema {
public:
  InterfaceSchema() = default;

  class Method;
  class MethodList;

  MethodList getMethods() const;

  kj::Maybe<Method> findMethodByName(kj::StringPtr name) const;

  Method getMethodByName(kj::StringPtr name) const;
  // Like findMethodByName() but throws an exception on failure.

private:
  InterfaceSchema(const _::RawSchema* raw): Schema(raw) {}
  template <typename T> static inline InterfaceSchema fromImpl() {
    return InterfaceSchema(&_::rawSchema<T>());
  }
  friend class Schema;
};

class InterfaceSchema::Method {
public:
  Method() = default;

  inline schema2::Method::Reader getProto() const { return proto; }
  inline InterfaceSchema getContainingInterface() const { return parent; }

  inline uint16_t getOrdinal() const { return ordinal; }
  inline uint getIndex() const { return ordinal; }

  inline bool operator==(const Method& other) const;
  inline bool operator!=(const Method& other) const { return !(*this == other); }

private:
  InterfaceSchema parent;
  uint16_t ordinal;
  schema2::Method::Reader proto;

  inline Method(InterfaceSchema parent, uint16_t ordinal,
                schema2::Method::Reader proto)
      : parent(parent), ordinal(ordinal), proto(proto) {}

  friend class InterfaceSchema;
};

class InterfaceSchema::MethodList {
public:
  MethodList() = default;  // empty list

  inline uint size() const { return list.size(); }
  inline Method operator[](uint index) const { return Method(parent, index, list[index]); }

  typedef _::IndexingIterator<const MethodList, Method> Iterator;
  inline Iterator begin() const { return Iterator(this, 0); }
  inline Iterator end() const { return Iterator(this, size()); }

private:
  InterfaceSchema parent;
  List<schema2::Method>::Reader list;

  inline MethodList(InterfaceSchema parent, List<schema2::Method>::Reader list)
      : parent(parent), list(list) {}

  friend class InterfaceSchema;
};

// -------------------------------------------------------------------

class ListSchema {
  // ListSchema is a little different because list types are not described by schema nodes.  So,
  // ListSchema doesn't subclass Schema.

public:
  ListSchema() = default;

  static ListSchema of(schema2::Type::Which primitiveType);
  static ListSchema of(StructSchema elementType);
  static ListSchema of(EnumSchema elementType);
  static ListSchema of(InterfaceSchema elementType);
  static ListSchema of(ListSchema elementType);
  // Construct the schema for a list of the given type.

  static ListSchema of(schema2::Type::Reader elementType, Schema context);
  // Construct from an element type schema.  Requires a context which can handle getDependency()
  // requests for any type ID found in the schema.

  inline schema2::Type::Which whichElementType() const;
  // Get the element type's "which()".  ListSchema does not actually store a schema::Type::Reader
  // describing the element type, but if it did, this would be equivalent to calling
  // .getBody().which() on that type.

  StructSchema getStructElementType() const;
  EnumSchema getEnumElementType() const;
  InterfaceSchema getInterfaceElementType() const;
  ListSchema getListElementType() const;
  // Get the schema for complex element types.  Each of these throws an exception if the element
  // type is not of the requested kind.

  inline bool operator==(const ListSchema& other) const;
  inline bool operator!=(const ListSchema& other) const { return !(*this == other); }

  template <typename T>
  void requireUsableAs() const;

private:
  schema2::Type::Which elementType;
  uint8_t nestingDepth;  // 0 for T, 1 for List(T), 2 for List(List(T)), ...
  Schema elementSchema;  // if elementType is struct, enum, interface...

  inline ListSchema(schema2::Type::Which elementType)
      : elementType(elementType), nestingDepth(0) {}
  inline ListSchema(schema2::Type::Which elementType, Schema elementSchema)
      : elementType(elementType), nestingDepth(0), elementSchema(elementSchema) {}
  inline ListSchema(schema2::Type::Which elementType, uint8_t nestingDepth,
                    Schema elementSchema)
      : elementType(elementType), nestingDepth(nestingDepth), elementSchema(elementSchema) {}

  template <typename T>
  struct FromImpl;
  template <typename T> static inline ListSchema fromImpl() {
    return FromImpl<T>::get();
  }

  void requireUsableAs(ListSchema expected) const;

  friend class Schema;
};

// =======================================================================================
// inline implementation

template <> inline schema2::Type::Which Schema::from<Void>() { return schema2::Type::VOID; }
template <> inline schema2::Type::Which Schema::from<bool>() { return schema2::Type::BOOL; }
template <> inline schema2::Type::Which Schema::from<int8_t>() { return schema2::Type::INT8; }
template <> inline schema2::Type::Which Schema::from<int16_t>() { return schema2::Type::INT16; }
template <> inline schema2::Type::Which Schema::from<int32_t>() { return schema2::Type::INT32; }
template <> inline schema2::Type::Which Schema::from<int64_t>() { return schema2::Type::INT64; }
template <> inline schema2::Type::Which Schema::from<uint8_t>() { return schema2::Type::UINT8; }
template <> inline schema2::Type::Which Schema::from<uint16_t>() { return schema2::Type::UINT16; }
template <> inline schema2::Type::Which Schema::from<uint32_t>() { return schema2::Type::UINT32; }
template <> inline schema2::Type::Which Schema::from<uint64_t>() { return schema2::Type::UINT64; }
template <> inline schema2::Type::Which Schema::from<float>() { return schema2::Type::FLOAT32; }
template <> inline schema2::Type::Which Schema::from<double>() { return schema2::Type::FLOAT64; }
template <> inline schema2::Type::Which Schema::from<Text>() { return schema2::Type::TEXT; }
template <> inline schema2::Type::Which Schema::from<Data>() { return schema2::Type::DATA; }

template <typename T>
inline void Schema::requireUsableAs() const {
  requireUsableAs(&_::rawSchema<T>());
}

inline bool StructSchema::Field::operator==(const Field& other) const {
  return parent == other.parent && index == other.index;
}
inline bool EnumSchema::Enumerant::operator==(const Enumerant& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}
inline bool InterfaceSchema::Method::operator==(const Method& other) const {
  return parent == other.parent && ordinal == other.ordinal;
}

inline ListSchema ListSchema::of(StructSchema elementType) {
  return ListSchema(schema2::Type::STRUCT, 0, elementType);
}
inline ListSchema ListSchema::of(EnumSchema elementType) {
  return ListSchema(schema2::Type::ENUM, 0, elementType);
}
inline ListSchema ListSchema::of(InterfaceSchema elementType) {
  return ListSchema(schema2::Type::INTERFACE, 0, elementType);
}
inline ListSchema ListSchema::of(ListSchema elementType) {
  return ListSchema(elementType.elementType, elementType.nestingDepth + 1,
                    elementType.elementSchema);
}

inline schema2::Type::Which ListSchema::whichElementType() const {
  return nestingDepth == 0 ? elementType : schema2::Type::LIST;
}

inline bool ListSchema::operator==(const ListSchema& other) const {
  return elementType == other.elementType && nestingDepth == other.nestingDepth &&
      elementSchema == other.elementSchema;
}

template <typename T>
inline void ListSchema::requireUsableAs() const {
  static_assert(kind<T>() == Kind::LIST,
                "ListSchema::requireUsableAs<T>() requires T is a list type.");
  requireUsableAs(Schema::from<T>());
}

template <typename T>
struct ListSchema::FromImpl<List<T>> {
  static inline ListSchema get() { return of(Schema::from<T>()); }
};

}  // namespace capnp

#endif  // CAPNP_SCHEMA_H_