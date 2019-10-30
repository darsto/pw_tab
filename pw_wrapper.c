/*-
 * The MIT License
 *
 * Copyright 2019 Darek Stojaczyk
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

static HMODULE
inject_dll(DWORD pid, char *path_to_dll)
{
	HANDLE proc;
	HANDLE thr;
	char buf[64];
	LPVOID ext_path_to_dll;
	LPVOID load_lib_winapi_addr;
	HMODULE injected_dll = NULL;
	DWORD thr_state;
	int rc;

	proc = OpenProcess(PROCESS_ALL_ACCESS, 0,pid);
	if(proc == NULL) {
		goto err;
	}

	load_lib_winapi_addr = (LPVOID)GetProcAddress(
		GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	if (load_lib_winapi_addr == NULL) {
		goto err;
	}

	ext_path_to_dll = (LPVOID)VirtualAllocEx(proc, NULL,
		strlen(path_to_dll), MEM_RESERVE|MEM_COMMIT,
		PAGE_READWRITE);
	if (ext_path_to_dll == NULL) {
		goto err;
	}

	rc = WriteProcessMemory(proc, (LPVOID)ext_path_to_dll,
		path_to_dll, strlen(path_to_dll), NULL);
	if (rc == 0) {
		goto err_free;
	}

	thr = CreateRemoteThread(proc, NULL, 0,
		(LPTHREAD_START_ROUTINE)load_lib_winapi_addr,
		(LPVOID)ext_path_to_dll, 0, NULL);
	if (thr == NULL) {
		goto err_free;
	}

	while(GetExitCodeThread(thr, &thr_state)) {
		if(thr_state != STILL_ACTIVE) {
			injected_dll = (HMODULE)thr_state;
			break;
		}
	}

	CloseHandle(thr);
	CloseHandle(proc);

	return injected_dll;

err_free:
	VirtualFreeEx(proc, ext_path_to_dll, 0, MEM_RELEASE);
err:
	snprintf(buf, sizeof(buf), "Failed to open PW process. "
		"Was it closed prematurely?");
	MessageBoxA(NULL, buf, "Loader", MB_OK);
	return 0;
}

int WINAPI
WinMain(HINSTANCE _unused, HINSTANCE _unused2,
		 LPTSTR args, int _unused3)

{
	STARTUPINFO pw_proc_startup_info = {0};
	PROCESS_INFORMATION pw_proc_info = {0};
	BOOL result = FALSE;

	pw_proc_startup_info.cb = sizeof(STARTUPINFO);
	result = CreateProcess(NULL,  "game.exe"
		" startbypatcher game:cpw console:1 nocheck",
		NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL,
		&pw_proc_startup_info, &pw_proc_info);
	if(!result) {
		MessageBox(0, "Could not start the PW process", "Error", MB_ICONERROR);
		return -1;
	}

	inject_dll(pw_proc_info.dwProcessId, "pw_tab.dll");
	ResumeThread(pw_proc_info.hThread);
	return 0;
}
