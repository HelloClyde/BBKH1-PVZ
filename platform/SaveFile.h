#pragma once
#include <string>
#include <cstddef>
bool H1WriteSaveFile(const std::string& path,const void* data,std::size_t size);
void H1RecoverSaveFile(const std::string& path);
