#pragma once

#if _WIN32
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT
#endif

#define CA_TRUE 1
#define CA_FALSE 0

#define ca_max(x, y) (((x) > (y)) ? (x) : (y))
#define ca_min(x, y) (((x) < (y)) ? (x) : (y))
#define ca_abs(x) (((x) > 0) ? (x) : -(x))
#define ca_cast(type, value) *(type *)&value
#define ca_zero_memory(ptr) memset(ptr, 0, sizeof(*ptr))

typedef enum
{
  ca_result_success = 0,
  ca_result_invalid_args = -1,
  ca_result_seek_failed = -2,
  ca_result_read_failed = -3,
} ca_result;

typedef int ca_bool;

typedef unsigned long long ca_uint64;
typedef unsigned int ca_uint32;
typedef int ca_int32;

typedef enum
{
  ca_read_result_success = 0,
  ca_read_result_at_end = -1,
  ca_read_result_failed = -2,
} ca_read_result;

typedef enum
{
  ca_seek_result_success = 0,
  ca_seek_result_unsupported = -1,
  ca_seek_result_failed = -2,
} ca_seek_result;

typedef enum
{
  ca_sample_format_unknown = 0,
  ca_sample_format_u8 = 1,
  ca_sample_format_s16 = 2,
  ca_sample_format_s24 = 3,
  ca_sample_format_s32 = 4,
  ca_sample_format_f32 = 5,
} ca_sample_format;

typedef struct
{
  ca_uint32 channels;
  ca_uint32 sample_rate;
  ca_sample_format sample_foramt;
  struct
  {
    int format_id;
  } apple;
} ca_audio_format;

typedef enum
{
  ca_seek_origin_start,
  ca_seek_origin_current,
} ca_seek_origin;
