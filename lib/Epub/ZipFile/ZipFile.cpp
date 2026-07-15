#ifndef UNIT_TEST
#include <esp_log.h>
#include <SD.h>
#include <SPI.h>
#if defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif
#include "ZipFile.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#include "../../miniz-3.1.0/miniz.c"

#define TAG "ZIP"

static size_t sd_read_callback(void *opaque, mz_uint64 file_ofs, void *pBuf, size_t n)
{
  if (!opaque || !pBuf || n == 0)
  {
    return 0;
  }
  File *fp = static_cast<File *>(opaque);
  if (!fp->seek(file_ofs))
  {
    return 0;
  }
  return fp->read(static_cast<uint8_t *>(pBuf), n);
}

struct ZipExtractContext
{
  uint8_t *buffer;
  size_t size;
  size_t yield_bytes;
};

static size_t zip_extract_callback(void *opaque, mz_uint64 file_ofs, const void *pBuf, size_t n)
{
  if (!opaque || !pBuf || n == 0)
  {
    return 0;
  }
  ZipExtractContext *ctx = static_cast<ZipExtractContext *>(opaque);
  if (!ctx->buffer || file_ofs >= ctx->size)
  {
    return 0;
  }
  size_t remaining = ctx->size - static_cast<size_t>(file_ofs);
  size_t to_copy = n > remaining ? remaining : n;
  memcpy(ctx->buffer + file_ofs, pBuf, to_copy);
  ctx->yield_bytes += to_copy;
#ifndef UNIT_TEST
  if (ctx->yield_bytes >= (16 * 1024))
  {
    ctx->yield_bytes = 0;
    vTaskDelay(1);
  }
#endif
  return to_copy;
}

namespace
{
// RAII wrapper owning the zip file handle and the miniz archive state
struct ZipArchive
{
  File fp;
  mz_zip_archive *zip = nullptr;
  bool initialized = false;

  bool open(const char *zip_path)
  {
    fp = SD.open(zip_path, FILE_READ);
    if (!fp)
    {
      ESP_LOGE(TAG, "Failed to open zip file %s", zip_path);
      return false;
    }
    zip = (mz_zip_archive *)calloc(1, sizeof(mz_zip_archive));
    if (!zip)
    {
      ESP_LOGE(TAG, "Failed to allocate zip archive");
      return false;
    }
    zip->m_pRead = sd_read_callback;
    zip->m_pIO_opaque = &fp;
    if (!mz_zip_reader_init(zip, fp.size(), 0))
    {
      ESP_LOGE(TAG, "mz_zip_reader_init_mem() failed!\n");
      ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip->m_last_error));
      return false;
    }
    initialized = true;
    return true;
  }

  bool stat_file(const char *filename, mz_uint32 *file_index, mz_zip_archive_file_stat *file_stat, bool log_missing)
  {
    if (!mz_zip_reader_locate_file_v2(zip, filename, nullptr, 0, file_index))
    {
      if (log_missing)
      {
        ESP_LOGE(TAG, "Could not find file %s", filename);
      }
      return false;
    }
    if (!mz_zip_reader_file_stat(zip, *file_index, file_stat))
    {
      ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
      ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip->m_last_error));
      return false;
    }
    return true;
  }

  ~ZipArchive()
  {
    if (initialized)
    {
      mz_zip_reader_end(zip);
    }
    if (zip)
    {
      free(zip);
    }
    if (fp)
    {
      fp.close();
    }
  }
};
} // namespace

// read a file from the zip file allocating the required memory for the data
uint8_t *ZipFile::read_file_to_memory(const char *filename, size_t *size)
{
  ZipArchive archive;
  if (!archive.open(m_filename.c_str()))
  {
    return nullptr;
  }
  // find the file and get its size - we do this all manually so we can add a null terminator to any strings
  mz_uint32 file_index = 0;
  mz_zip_archive_file_stat file_stat;
  if (!archive.stat_file(filename, &file_index, &file_stat, true))
  {
    return nullptr;
  }
  // allocate memory for the file (optionally in PSRAM)
  size_t uncomp_size = file_stat.m_uncomp_size;
  uint8_t *file_data;
#if !defined(UNIT_TEST) && defined(BOARD_HAS_PSRAM)
  file_data = (uint8_t *)heap_caps_calloc(uncomp_size + 1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  file_data = (uint8_t *)calloc(uncomp_size + 1, 1);
#endif
  if (!file_data)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for %s\n", file_stat.m_filename);
    return nullptr;
  }
  ZipExtractContext ctx = {
      .buffer = file_data,
      .size = uncomp_size,
      .yield_bytes = 0,
  };
  if (!mz_zip_reader_extract_to_callback(archive.zip, file_index, zip_extract_callback, &ctx, 0))
  {
    ESP_LOGE(TAG, "mz_zip_reader_extract_to_callback() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(archive.zip->m_last_error));
    free(file_data);
    return nullptr;
  }
  // return the size if required
  if (size)
  {
    *size = uncomp_size;
  }
  return file_data;
}

bool ZipFile::get_file_uncompressed_size(const char *filename, size_t *size)
{
  if (!size)
  {
    return false;
  }
  *size = 0;
  ZipArchive archive;
  if (!archive.open(m_filename.c_str()))
  {
    return false;
  }
  mz_uint32 file_index = 0;
  mz_zip_archive_file_stat file_stat;
  if (!archive.stat_file(filename, &file_index, &file_stat, false))
  {
    return false;
  }
  *size = file_stat.m_uncomp_size;
  return true;
}
