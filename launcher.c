#include <windows.h>
#include <stdio.h>
int WINAPI
WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
	STARTUPINFOA si = { sizeof(si) };
	PROCESS_INFORMATION pi;
	char msg[256];
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput  = NULL;
	si.hStdOutput = NULL;
	si.hStdError  = NULL;
	CreateProcessA("hashsumr.exe", NULL, NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	WaitForSingleObject(pi.hProcess, 5000);
	DWORD code;
	GetExitCodeProcess(pi.hProcess, &code);
	snprintf(msg, sizeof(msg), "Exit Code: 0x%08x (%d)", code, code);
	MessageBox(NULL, msg, "Exit Code", MB_OK | MB_ICONINFORMATION);
	// code should be 0 or 1, NOT 0xC0000005 (access violation / crash)
	return code;
}
