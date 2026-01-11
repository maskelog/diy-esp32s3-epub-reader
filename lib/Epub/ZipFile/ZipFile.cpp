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

// read a file from the zip file allocating the required memory for the data
uint8_t *ZipFile::read_file_to_memory(const char *filename, size_t *size)
{
  File fp = SD.open(m_filename.c_str(), FILE_READ);
  if (!fp)
  {
    ESP_LOGE(TAG, "Failed to open zip file %s", m_filename.c_str());
    return nullptr;
  }
  size_t file_size = fp.size();

  // open up the epub file using miniz
  mz_zip_archive *zip_archive = (mz_zip_archive *)calloc(1, sizeof(mz_zip_archive));
  if (!zip_archive)
  {
    ESP_LOGE(TAG, "Failed to allocate zip archive");
    fp.close();
    return nullptr;
  }
  zip_archive->m_pRead = sd_read_callback;
  zip_archive->m_pIO_opaque = &fp;
  bool status = mz_zip_reader_init(zip_archive, file_size, 0);
  if (!status)
  {
    ESP_LOGE(TAG, "mz_zip_reader_init_mem() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    free(zip_archive);
    fp.close();
    return nullptr;
  }
  // find the file
  mz_uint32 file_index = 0;
  if (!mz_zip_reader_locate_file_v2(zip_archive, filename, nullptr, 0, &file_index))
  {
    ESP_LOGE(TAG, "Could not find file %s", filename);
    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    fp.close();
    return nullptr;
  }
  // get the file size - we do this all manually so we can add a null terminator to any strings
  mz_zip_archive_file_stat file_stat;
  if (!mz_zip_reader_file_stat(zip_archive, file_index, &file_stat))
  {
    ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    fp.close();
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
    mz_zip_reader_end(zip_archive);
    fp.close();
    return nullptr;
  }
  ZipExtractContext ctx = {
      .buffer = file_data,
      .size = uncomp_size,
      .yield_bytes = 0,
  };
  status = mz_zip_reader_extract_to_callback(zip_archive, file_index, zip_extract_callback, &ctx, 0);
  if (!status)
  {
    ESP_LOGE(TAG, "mz_zip_reader_extract_to_callback() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    free(file_data);
    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    fp.close();
    return nullptr;
  }
  // Close the archive, freeing any resources it was using
  mz_zip_reader_end(zip_archive);
  free(zip_archive);
  fp.close();
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
  File fp = SD.open(m_filename.c_str(), FILE_READ);
  if (!fp)
  {
    ESP_LOGE(TAG, "Failed to open zip file %s", m_filename.c_str());
    return false;
  }
  size_t file_size = fp.size();

  mz_zip_archive *zip_archive = (mz_zip_archive *)calloc(1, sizeof(mz_zip_archive));
  if (!zip_archive)
  {
    ESP_LOGE(TAG, "Failed to allocate zip archive");
    fp.close();
    return false;
  }
  zip_archive->m_pRead = sd_read_callback;
  zip_archive->m_pIO_opaque = &fp;
  bool status = mz_zip_reader_init(zip_archive, file_size, 0);
  if (!status)
  {
    ESP_LOGE(TAG, "mz_zip_reader_init_mem() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    free(zip_archive);
    fp.close();
    return false;
  }

  mz_uint32 file_index = 0;
  if (!mz_zip_reader_locate_file_v2(zip_archive, filename, nullptr, 0, &file_index))
  {
    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    fp.close();
    return false;
  }

  mz_zip_archive_file_stat file_stat;
  if (!mz_zip_reader_file_stat(zip_archive, file_index, &file_stat))
  {
    ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    mz_zip_reader_end(zip_archive);
    free(zip_archive);
    fp.close();
    return false;
  }

  *size = file_stat.m_uncomp_size;
  mz_zip_reader_end(zip_archive);
  free(zip_archive);
  fp.close();
  return true;
}

bool ZipFile::read_file_to_file(const char *filename, const char *dest)
{
  File fp = SD.open(m_filename.c_str(), FILE_READ);
  if (!fp)
  {
    ESP_LOGE(TAG, "Failed to open zip file %s", m_filename.c_str());
    return false;
  }
  size_t file_size = fp.size();

  mz_zip_archive *zip_archive = (mz_zip_archive *)calloc(1, sizeof(mz_zip_archive));
  if (!zip_archive)
  {
    ESP_LOGE(TAG, "Failed to allocate zip archive");
    fp.close();
    return false;
  }
  zip_archive->m_pRead = sd_read_callback;
  zip_archive->m_pIO_opaque = &fp;
  bool status = mz_zip_reader_init(zip_archive, file_size, 0);
  if (!status)
  {
    ESP_LOGE(TAG, "mz_zip_reader_init_mem() failed!\n");
    ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
    free(zip_archive);
    fp.close();
    return false;
  }
  // Run through the archive and find the requiested file
  for (int i = 0; i < (int)mz_zip_reader_get_num_files(zip_archive); i++)
  {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(zip_archive, i, &file_stat))
    {
      ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
      ESP_LOGE(TAG, "Error %s\n", mz_zip_get_error_string(zip_archive->m_last_error));
      mz_zip_reader_end(zip_archive);
      free(zip_archive);
      fp.close();
      return false;
    }
    // is this the file we're looking for?
    if (strcmp(filename, file_stat.m_filename) == 0)
    {
      ESP_LOGI(TAG, "Extracting %s\n", file_stat.m_filename);
      // since we are using the memory based reader, we need to extract to memory first
      size_t uncomp_size;
      void *p = mz_zip_reader_extract_to_heap(zip_archive, i, &uncomp_size, 0);
      if (!p)
      {
        ESP_LOGE(TAG, "mz_zip_reader_extract_to_heap() failed\n");
        mz_zip_reader_end(zip_archive);
        free(zip_archive);
        fp.close();
        return false;
      }
      File dest_fp = SD.open(dest, FILE_WRITE);
      if (!dest_fp)
      {
        ESP_LOGE(TAG, "Failed to open destination file %s", dest);
        mz_free(p);
        mz_zip_reader_end(zip_archive);
        free(zip_archive);
        fp.close();
        return false;
      }
      dest_fp.write((uint8_t *)p, uncomp_size);
      dest_fp.close();
      mz_free(p);
      mz_zip_reader_end(zip_archive);
      free(zip_archive);
      fp.close();
      return true;
    }
  }
  mz_zip_reader_end(zip_archive);
  free(zip_archive);
  fp.close();
  return false;
}
