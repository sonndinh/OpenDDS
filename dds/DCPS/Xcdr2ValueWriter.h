/*
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#ifndef OPENDDS_DCPS_XCDR2_VALUE_WRITER_H
#define OPENDDS_DCPS_XCDR2_VALUE_WRITER_H

#ifndef OPENDDS_SAFETY_PROFILE

#include "ValueWriter.h"
#include "Serializer.h"

#include <dds/DdsDynamicDataC.h>

OPENDDS_BEGIN_VERSIONED_NAMESPACE_DECL

namespace OpenDDS {
namespace DCPS {

/// Write a dynamic data object in XCDR2 format
class Xcdr2ValueWriter : public ValueWriter {
public:
  Xcdr2ValueWriter(Serializer& ser)
    : writer_(ser)
    , encoding_(ser.encoding())
  {}

  bool begin_struct();
  bool end_struct();
  bool begin_struct_member(const DDS::MemberDescriptor& descriptor);
  bool end_struct_member();

  bool begin_union();
  bool end_union();
  bool begin_discriminator();
  bool end_discriminator();
  bool begin_union_member(const char* name);
  bool end_union_member();

  bool begin_array();
  bool end_array();
  bool begin_sequence();
  bool end_sequence();
  bool begin_element(size_t idx);
  bool end_element();

  bool write_boolean(ACE_CDR::Boolean value);
  bool write_byte(ACE_CDR::Octet value);
#if OPENDDS_HAS_EXPLICIT_INTS
  bool write_int8(ACE_CDR::Int8 value);
  bool write_uint8(ACE_CDR::UInt8 value);
#endif
  bool write_int16(ACE_CDR::Short value);
  bool write_uint16(ACE_CDR::UShort value);
  bool write_int32(ACE_CDR::Long value);
  bool write_uint32(ACE_CDR::ULong value);
  bool write_int64(ACE_CDR::LongLong value);
  bool write_uint64(ACE_CDR::ULongLong value);
  bool write_float32(ACE_CDR::Float value);
  bool write_float64(ACE_CDR::Double value);
  bool write_float128(ACE_CDR::LongDouble value);
  bool write_fixed(const OpenDDS::FaceTypes::Fixed& value);
  bool write_char8(ACE_CDR::Char value);
  bool write_char16(ACE_CDR::WChar value);
  bool write_string(const ACE_CDR::Char* value, size_t length);
  bool write_wstring(const ACE_CDR::WChar* value, size_t length);
  bool write_enum(const char* /*name*/, ACE_CDR::Long value);
  bool write_absent_value();

private:
  struct WriteState {
    // Offset of a member in the stream. Starting from the header if there is one.
    size_t offset;

    // Accumulated size of the member including any paddings required to serialize it.
    // This does not include the header if there is one.
    size_t size;
  };

  // The state for the top-level type is the bottom entry. For each nested member,
  // a new entry for that member is pushed. When we finish writing a nested member,
  // its entry is popped. This is handled in a recursive way, e.g., if a nested member
  // has a nested member of its own, we push another entry to the stack.
  // For members that are not nested, we can just update the size of the containing
  // entity directly by updating the size field of the top entry.
  std::stack<WriteState> state_;

  Serializer& writer_;
  const Encoding& encoding_;
};

// TODO(sonndinh): Mark the start of a struct. When end_struct is called, we need to compute
// the size of the whole struct and come back to the begin of the stream to write the struct's
// serialized size. To do this, it needs to preallocate the space for the dheader if needed.
// Final and appendable types are simpler since there is no member header.
// For mutable types, each member has emheader with optional nextint depending on the size of
// the member. So we don't know if the member header needs 4 bytes or 8 bytes until we finish
// writing the member and know its size.
//
// A potential way is to assume there is only emheader, then we write the member (and its internal
// members). In end_struct_member(), we now know the member's size. If the member's size requires
// a nextint field, we write the nextint field and copy the contents of the member after the
// nextint field. We also need to come back and write the emheader's contents.
// end_struct_member() also needs to update the accumulated size of the whole struct so far
// (remember to account for any paddings needed).
// So the ValueWriter class needs to track the size of the member, the location of the emheader
// so that it can come back and rewrite the member header.
// For the whole struct, it needs to track the struct size and the location of the dheader.
// When end_struct() is called, these are used to rewrite the dheader's contents.
bool Xcdr2ValueWriter::begin_struct()
{
  // The struct starts at the logical offset 0 and the initial size is 0.
  state_.push({0, 0});
  return true;
}

bool Xcdr2ValueWriter::end_struct()
{
  // TODO(sonndinh): Write the size to the dheader for appendable and mutable.
  // const size_t offset = state_.top().offset;
  // const size_t size = state_.top().size;

  state_.pop();
  return true;
}

bool Xcdr2ValueWriter::begin_struct_member(const DDS::MemberDescriptor& md)
{
  // TODO(sonndinh):
  // If the member is not nested, update the top entry to account for
  // the member's size and any paddings needed.
  // If it is nested, push a new entry and compute the member's size recursively.
  // Members of type struct, union, sequence, array are nested members, though
  // we might be able to compute the size of sequence/array of primitive types directly.
  DDS::DynamicType_var member_type = get_base_type(md->type());
  const TypeKind member_tk = member_type->get_kind();
  size_t& size = state_.top().size;

  switch (member_tk) {
  case TK_INT8:
    
    primitive_serialized_size_int8(encoding_, size);
    break;
  case TK_UINT8:
    primitive_serialized_size_uint8(encoding_, size);
    break;
  case TK_INT16:
    primitive_serialized_size(encoding_, size, CORBA::Short());
    break;
  case TK_UINT16:
    primitive_serialized_size(encoding_, size, CORBA::UShort());
    break;
  case TK_INT32:
    primitive_serialized_size(encoding_, size, CORBA::Long());
    break;
  case TK_UINT32:
    primitive_serialized_size(encoding_, size, CORBA::ULong());
    break;
  case TK_INT64:
    primitive_serialized_size(encoding_, size, CORBA::LongLong());
    break;
  case TK_UINT64:
    primitive_serialized_size(encoding_, size, CORBA::ULongLong());
    break;
  case TK_FLOAT32:
    primitive_serialized_size(encoding_, size, CORBA::Float());
    break;
  case TK_FLOAT64:
    primitive_serialized_size(encoding_, size, CORBA::Double());
    break;
  case TK_FLOAT128:
    primitive_serialized_size(encoding_, size, CORBA::LongDouble());
    break;
  case TK_CHAR8:
    primitive_serialized_size_char(encoding_, size);
    break;
#ifdef DDS_HAS_WCHAR
  case TK_CHAR16:
    primitive_serialized_size_wchar(encoding_, size);
    break;
#endif
  case TK_BYTE:
    primitive_serialized_size_octet(encoding_, size);
    break;
  case TK_BOOLEAN:
    primitive_serialized_size_boolean(encoding_, size);
    break;
  case TK_STRING8:
#ifdef DDS_HAS_WCHAR
  case TK_STRING16:
#endif
  case TK_STRUCTURE:
  case TK_UNION:
  case TK_ARRAY:
  case TK_SEQUENCE:
  default:
    if (log_level >= LogLevel::Notice) {
      ACE_ERROR((LM_NOTICE, "(%P|%t) NOTICE: Xcdr2ValueWriter::begin_struct_member:"
                 " Unsupported member type %C\n", typekind_to_string(member_tk)));
    }
    return false;
  }
  return true;
}

bool Xcdr2ValueWriter::end_struct_member()
{
  // TODO(sonndinh): Now update the member's header if there is one. In case of mutable,
  // it needs to check if a nextint field is needed. If so, the contents of the member
  // in the byte stream needs to be shifted 4 bytes to the right.
  // Then update the struct's size with the member's size and its header.
  return true;
}

bool Xcdr2ValueWriter::begin_union()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_union()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::begin_discriminator()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_discriminator()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::begin_union_member(const char* name)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_union_member()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::begin_array()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_array()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::begin_sequence()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_sequence()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::begin_element()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::end_element()
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_boolean(ACE_CDR::Boolean value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_byte(ACE_CDR::Octet value)
{
  // TODO
  return true;
}

#if OPENDDS_HAS_EXPLICIT_INTS
bool Xcdr2ValueWriter::write_int8(ACE_CDR::Int8 value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_uint8(ACE_CDR::UInt8 value)
{
  // TODO
  return true;
}
#endif

bool Xcdr2ValueWriter::write_int16(ACE_CDR::Short value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_uint16(ACE_CDR::UShort value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_int32(ACE_CDR::Long value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_uint32(ACE_CDR::ULong value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_int64(ACE_CDR::LongLong value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_uint64(ACE_CDR::ULongLong value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_float32(ACE_CDR::Float value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_float64(ACE_CDR::Double value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_float128(ACE_CDR::LongDouble value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_fixed(const OpenDDS::FaceTypes::Fixed& value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_char8(ACE_CDR::Char value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_char16(ACE_CDR::WChar value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_string(const ACE_CDR::Char* value, size_t length)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_wstring(const ACE_CDR::WChar* value, size_t length)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_enum(const char* /*name*/, ACE_CDR::Long value)
{
  // TODO
  return true;
}

bool Xcdr2ValueWriter::write_absent_value()
{
  // TODO
  return true;
}

} // namespace DCPS
} // namespace OpenDDS

OPENDDS_END_VERSIONED_NAMESPACE_DECL

#endif

#endif /* OPENDDS_DCPS_XCDR2_VALUE_WRITER_H */
