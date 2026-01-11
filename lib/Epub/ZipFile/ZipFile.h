#pragma once

#include <string>

class ZipFile
{
private:
  std::string m_filename;

public:
  ZipFile(const char *filename)
  {
    m_filename = filename;
  }
  ~ZipFile() {}
  // read a file from the zip file allocating the required memory for the data
  uint8_t *read_file_to_memory(const char *filename, size_t *size = nullptr);
  // read the uncompressed size of a file without extracting it
  bool get_file_uncompressed_size(const char *filename, size_t *size);
  bool read_file_to_file(const char *filename, const char *dest);
};
