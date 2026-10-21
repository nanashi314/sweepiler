// Included first in every web build source: what the core takes from MSVC.
#pragma once
#ifdef __cplusplus
#include <cstddef>
#include <cstdlib>
#include <cstring>
// The core reads its debug switches (IMS_GRID_DUMP) with MSVC's getenv_s.
inline int getenv_s(std::size_t* size,char* buffer,std::size_t count,const char* name) {
    const char* v=std::getenv(name); *size=v?std::strlen(v)+1:0;
    if(v && buffer && count) {std::strncpy(buffer,v,count-1); buffer[count-1]=0;}
    return 0;
}
#endif
