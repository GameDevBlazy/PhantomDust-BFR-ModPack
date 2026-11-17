// A do-nothing DLL (no C runtime, no imports but KERNEL32's DllMain contract) to test whether a Store-installed
// Phantom Dust accepts ANY unsigned DLL in its process. Loaded through CwoLaunch --dll.
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
