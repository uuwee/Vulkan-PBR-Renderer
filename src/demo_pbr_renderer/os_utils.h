// OS utilities

bool OS_FileLastModificationTime(STR_View filepath, uint64_t* out_modtime);

void OS_SleepMilliseconds(uint32_t ms);

void OS_MessageBox(STR_View message);

bool OS_ReadEntireFile(DS_Arena* arena, STR_View filepath, STR_View* out_data);
