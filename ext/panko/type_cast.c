#include "type_cast.h"
#include "time_conversion.h"

static ID type_cast_from_database_id = 0;
static ID to_s_id = 0;
static ID to_i_id = 0;

// Caching ActiveRecord Types
static VALUE ar_string_type = Qundef;
static VALUE ar_text_type = Qundef;
static VALUE ar_float_type = Qundef;
static VALUE ar_integer_type = Qundef;
static VALUE ar_boolean_type = Qundef;
static VALUE ar_date_time_type = Qundef;
static VALUE ar_time_zone_converter = Qundef;

static VALUE ar_pg_integer_type = Qundef;
static VALUE ar_pg_float_type = Qundef;
static VALUE ar_pg_uuid_type = Qundef;
static VALUE ar_pg_json_type = Qundef;
static VALUE ar_pg_date_time_type = Qundef;

static int initiailized = 0;

VALUE cache_postgres_type_lookup(VALUE ar) {
  VALUE ar_connection_adapters =
      rb_const_get_at(ar, rb_intern("ConnectionAdapters"));
  if (ar_connection_adapters == Qundef) {
    return Qfalse;
  }

  VALUE ar_postgresql =
      rb_const_get_at(ar_connection_adapters, rb_intern("PostgreSQL"));
  if (ar_postgresql == Qundef) {
    return Qfalse;
  }

  VALUE ar_oid = rb_const_get_at(ar_postgresql, rb_intern("OID"));
  if (ar_oid == Qundef) {
    return Qfalse;
  }

  ar_pg_integer_type = rb_const_get_at(ar_oid, rb_intern("Integer"));
  ar_pg_float_type = rb_const_get_at(ar_oid, rb_intern("Float"));
  ar_pg_uuid_type = rb_const_get_at(ar_oid, rb_intern("Uuid"));
  ar_pg_json_type = rb_const_get_at(ar_oid, rb_intern("Json"));
  ar_pg_date_time_type = rb_const_get_at(ar_oid, rb_intern("DateTime"));

  return Qtrue;
}

VALUE cache_time_zone_type_lookup(VALUE ar) {
  // ActiveRecord::AttributeMethods
  VALUE ar_attr_methods = rb_const_get_at(ar, rb_intern("AttributeMethods"));
  if (ar_attr_methods == Qundef) {
    return Qfalse;
  }

  // ActiveRecord::AttributeMethods::TimeZoneConversion
  VALUE ar_time_zone_conversion =
      rb_const_get_at(ar_attr_methods, rb_intern("TimeZoneConversion"));
  if (ar_time_zone_conversion == Qundef) {
    return Qfalse;
  }

  ar_time_zone_converter =
      rb_const_get_at(ar_time_zone_conversion, rb_intern("TimeZoneConverter"));

  return Qtrue;
}

void cache_type_lookup() {
  if (initiailized == 1) {
    return;
  }

  initiailized = 1;

  VALUE ar = rb_const_get_at(rb_cObject, rb_intern("ActiveRecord"));

  // ActiveRecord::Type
  VALUE ar_type = rb_const_get_at(ar, rb_intern("Type"));

  ar_string_type = rb_const_get_at(ar_type, rb_intern("String"));
  ar_text_type = rb_const_get_at(ar_type, rb_intern("Text"));
  ar_float_type = rb_const_get_at(ar_type, rb_intern("Float"));
  ar_integer_type = rb_const_get_at(ar_type, rb_intern("Integer"));
  ar_boolean_type = rb_const_get_at(ar_type, rb_intern("Boolean"));
  ar_date_time_type = rb_const_get_at(ar_type, rb_intern("DateTime"));

  // TODO: if we get error or not, add this to some debug log
  int isErrored;
  rb_protect(cache_postgres_type_lookup, ar, &isErrored);

  rb_protect(cache_time_zone_type_lookup, ar, &isErrored);
}

bool is_string_or_text_type(VALUE type_klass) {
  return type_klass == ar_string_type || type_klass == ar_text_type ||
         (ar_pg_uuid_type != Qundef && type_klass == ar_pg_uuid_type);
}

VALUE cast_string_or_text_type(VALUE value) {
  if (RB_TYPE_P(value, T_STRING)) {
    return value;
  }

  if (value == Qtrue) {
    return rb_str_new_cstr("t");
  }

  if (value == Qfalse) {
    return rb_str_new_cstr("f");
  }

  return rb_funcall(value, to_s_id, 0);
}

bool is_float_type(VALUE type_klass) {
  return type_klass == ar_float_type ||
         (ar_pg_float_type != Qundef && type_klass == ar_pg_float_type);
}

VALUE cast_float_type(VALUE value) {
  if (RB_TYPE_P(value, T_FLOAT)) {
    return value;
  }

  if (RB_TYPE_P(value, T_STRING)) {
    const char* val = StringValuePtr(value);
    return rb_float_new(strtod(val, NULL));
  }

  return Qundef;
}

bool is_integer_type(VALUE type_klass) {
  return type_klass == ar_integer_type ||
         (ar_pg_integer_type != Qundef && type_klass == ar_pg_integer_type);
}

VALUE cast_integer_type(VALUE value) {
  if (RB_INTEGER_TYPE_P(value)) {
    return value;
  }

  if (RB_TYPE_P(value, T_STRING)) {
    const char* val = StringValuePtr(value);
    if (strlen(val) == 0) {
      return Qnil;
    }
    return rb_cstr2inum(val, 10);
  }

  if (RB_FLOAT_TYPE_P(value)) {
    // We are calling the `to_i` here, because ruby internal
    // `flo_to_i` is not accessible
    return rb_funcall(value, to_i_id, 0);
  }

  if (value == Qtrue) {
    return INT2NUM(1);
  }

  if (value == Qfalse) {
    return INT2NUM(0);
  }

  // At this point, we handled integer, float, string and booleans
  // any thing other than this (array, hashes, etc) should result in nil
  return Qnil;
}

bool is_json_type(VALUE type_klass) {
  return ar_pg_json_type != Qundef && type_klass == ar_pg_json_type;
}

VALUE cast_json_type(VALUE value) {
  if (!RB_TYPE_P(value, T_STRING)) {
    return value;
  }

  // TODO: instead of parsing the json, let's signal to "write_value"
  // to use "push_json" instead of "push_value"
  return Qundef;
}

bool is_boolean_type(VALUE type_klass) {
  return type_klass == ar_boolean_type;
}

VALUE cast_boolean_type(VALUE value) {
  if (value == Qtrue || value == Qfalse) {
    return value;
  }

  if (value == Qnil || RSTRING_LEN(value) == 0) {
    return Qnil;
  }

  const char* val = StringValuePtr(value);
  bool isFalseValue =
      (*val == '0' || (*val == 'f' || *val == 'F') ||
       (strcmp(val, "false") == 0 || strcmp(val, "FALSE") == 0) ||
       (strcmp(val, "off") == 0 || strcmp(val, "OFF") == 0));

  return isFalseValue ? Qfalse : Qtrue;
}

bool is_date_time_type(VALUE type_klass) {
  return (type_klass == ar_date_time_type) ||
         (ar_pg_date_time_type != Qundef &&
          type_klass == ar_pg_date_time_type) ||
         (ar_time_zone_converter != Qundef &&
          type_klass == ar_time_zone_converter);
}

VALUE cast_date_time_type(VALUE value) {
  // Instead of take strings to comparing them to time zones
  // and then comparing them back to string
  // We will just make sure we have string on ISO8601 and it's utc
  if (RB_TYPE_P(value, T_STRING)) {
    const char* val = StringValuePtr(value);
    // 'Z' in ISO8601 says it's UTC
    if (val[strlen(val) - 1] == 'Z' && is_iso8601_time_string(val) == Qtrue) {
      return value;
    }

    VALUE iso8601_string = iso_ar_iso_datetime_string(val);
    if (iso8601_string != Qnil) {
      return iso8601_string;
    }
  }

  return Qundef;
}

VALUE type_cast(VALUE type_metadata, VALUE value) {
  cache_type_lookup();

  VALUE type_klass = rb_obj_class(type_metadata);
  VALUE typeCastedValue = Qundef;

  TypeCast typeCast;
  for (typeCast = type_casts; typeCast->canCast != NULL; typeCast++) {
    if (typeCast->canCast(type_klass)) {
      typeCastedValue = typeCast->typeCast(value);
      break;
    }
  }

  if (typeCastedValue == Qundef) {
    return rb_funcall(type_metadata, type_cast_from_database_id, 1, value);
  }

  return typeCastedValue;
}

VALUE public_type_cast(VALUE module, VALUE type_metadata, VALUE value) {
  return type_cast(type_metadata, value);
}

void panko_init_type_cast(VALUE mPanko) {
  type_cast_from_database_id = rb_intern_const("type_cast_from_database");
  to_s_id = rb_intern_const("to_s");
  to_i_id = rb_intern_const("to_i");

  rb_define_singleton_method(mPanko, "_type_cast", public_type_cast, 2);

  panko_init_time(mPanko);
}