#include "rts.h"
#include "buf.h"

/* Code to read (S)LEB128 to ints (traps if does not fit in return type) */

export uint32_t read_u32_of_leb128(buf *buf) {
  uint32_t r = 0;
  unsigned int s = 0;
  uint8_t b;
  do {
    b = read_byte(buf);
    if (s > 0 && b == 0x00) {
        // The high 7 bits is all zeros, this is not a shortest encoding
        idl_trap_with("not shortest encoding");
    }
    if (s == 28 && !((b & (uint8_t)0xF0) == 0x00)) {
        // the 5th byte needs to be the last, and it must contribute at most 4 bits
        // else we have an int overflow
        idl_trap_with("int overflow");
    }
    r += (b & (uint8_t)0x7f) << s;
    s += 7;
  } while (b & (uint8_t)0x80);
  return r;
}

export int32_t read_i32_of_sleb128(buf *buf) {
  uint32_t r = 0;
  unsigned int s = 0;
  uint8_t b;
  bool last_sign_bit_set = 0;
  do {
    b = read_byte(buf);
    if (s == 28 && !((b & (uint8_t)0xF0) == 0x00 || (b & (uint8_t)0xF0) == 0x70)) {
        // the 5th byte needs to be the last, and it must contribute at most 4 bits
        // else we have an int overflow
        idl_trap_with("int overflow");
    }
    if (s > 0 && ((!last_sign_bit_set && b == 0x00) || (last_sign_bit_set && b == 0x7F))) {
        // The high 8 bits are all zeros or ones, so this is not a shortest encoding
        idl_trap_with("not shortest encoding");
    }
    last_sign_bit_set = (b & (uint8_t)0x40);
    r += (b & (uint8_t)0x7f) << s;
    s += 7;
  } while (b & (uint8_t)0x80);
  // sign extend
  if (s < 32 && last_sign_bit_set) {
    r |= ((~(uint32_t)0) << s);
  }
  return r;
}

/*
 * IDL constants
 */
#define IDL_PRIM_null     (-1)
#define IDL_PRIM_bool     (-2)
#define IDL_PRIM_nat      (-3)
#define IDL_PRIM_int      (-4)
#define IDL_PRIM_nat8     (-5)
#define IDL_PRIM_nat16    (-6)
#define IDL_PRIM_nat32    (-7)
#define IDL_PRIM_nat64    (-8)
#define IDL_PRIM_int8     (-9)
#define IDL_PRIM_int16    (-10)
#define IDL_PRIM_int32    (-11)
#define IDL_PRIM_int64    (-12)
#define IDL_PRIM_float32  (-13)
#define IDL_PRIM_float64  (-14)
#define IDL_PRIM_text     (-15)
#define IDL_PRIM_reserved (-16)
#define IDL_PRIM_empty    (-17)

#define IDL_PRIM_lowest   (-17)

#define IDL_CON_opt       (-18)
#define IDL_CON_vec       (-19)
#define IDL_CON_record    (-20)
#define IDL_CON_variant   (-21)
#define IDL_CON_func      (-22)
#define IDL_CON_service   (-23)


/*
 * This function parses the IDL magic header and type description. It
 *  * traps if the type description is not well-formed. In particular, it traps if
 *    any index into the type description table is out of bounds, so that
 *    subsequent code can trust these values
 *  * returns a pointer to the first byte after the IDL header (via return)
 *  * allocates a type description table, and returns it
 *    (via pointer argument, for lack of multi-value returns in C)
 *  * returns a pointer to the beginning of the list of main types
 *    (again via pointer argument, for lack of multi-value returns in C)
 */
export void parse_idl_header(buf *buf, uint8_t ***typtbl_out, uint8_t **main_types_out) {
  // Magic bytes (DIDL)
  if (read_word(buf) != 0x4C444944) idl_trap_with("missing magic bytes");

  // Create a table for the type description
  int32_t n_types = read_u32_of_leb128(buf);

  // read_u32_of_leb128 returns an uint32_t, we want an int32_t here so that the
  // comparisons below work, so let's make sure we did not wrap around in the
  // conversion.
  if (n_types < 0) { idl_trap_with("overflow in number of types"); }

  // Early sanity check
  if (&buf->p[n_types] >= buf->e) { idl_trap_with("too many types"); }

  // Go through the table
  uint8_t **typtbl = (uint8_t **)alloc(n_types * sizeof(uint8_t*));
  for (int i = 0; i < n_types; i++) {
    typtbl[i] = buf->p;
    int ty = read_i32_of_sleb128(buf);
    if (ty >= IDL_PRIM_lowest) {
      idl_trap_with("type index too high"); // illegal
    } else if (ty == IDL_CON_opt) {
      int32_t t = read_i32_of_sleb128(buf);
      if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
    } else if (ty == IDL_CON_vec) {
      int32_t t = read_i32_of_sleb128(buf);
      if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
    } else if (ty == IDL_CON_record) {
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        read_u32_of_leb128(buf);
        int32_t t = read_i32_of_sleb128(buf);
        if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
      }
    } else if (ty == IDL_CON_variant) {
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        read_u32_of_leb128(buf);
        int32_t t = read_i32_of_sleb128(buf);
        if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
      }
    } else if (ty == IDL_CON_func) {
      // arg types
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        int32_t t = read_i32_of_sleb128(buf);
        if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
      }
      // ret types
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        int32_t t = read_i32_of_sleb128(buf);
        if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
      }
      // annotations
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        (buf->p)++;
      }
    } else if (ty == IDL_CON_service) {
      for (int n = read_u32_of_leb128(buf); n > 0; n--) {
        // name
        unsigned int size = read_u32_of_leb128(buf);
        (buf->p) += size;
        // type
        int32_t t = read_i32_of_sleb128(buf);
        if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
      }
    } else {
      // no support for future types yet
      idl_trap_with("future type");
    }
  }
  // Now read the main types
  *main_types_out = buf->p;
  for (int n = read_u32_of_leb128(buf); n > 0; n--) {
    int32_t t = read_i32_of_sleb128(buf);
    if (t < IDL_PRIM_lowest || t >= n_types) idl_trap_with("type index out of range");
  }

  *typtbl_out = typtbl;
}