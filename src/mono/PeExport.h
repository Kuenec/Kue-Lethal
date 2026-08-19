#ifndef KUE_MONO_PE_EXPORT_H
#define KUE_MONO_PE_EXPORT_H

namespace kue::mono::pe {

void* moduleBase(const char* moduleSubstring);
void* getExport(void* base, const char* name);

}

#endif
