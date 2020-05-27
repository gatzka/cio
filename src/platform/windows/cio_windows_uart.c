/*
 * SPDX-License-Identifier: MIT
 *
 * The MIT License (MIT)
 *
 * Copyright (c) <2020> <Stephan Gatzka>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Windows.h>
#include <setupapi.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <tchar.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_uart.h"
#include "cio_util.h"


static enum cio_error enumerate_com_ports(unsigned int *pNumber, char *pPortName, int strMaxLen, char *pFriendName)
{
	unsigned int i, jj;

	TCHAR *pTempPortName;

	GUID *pGuid;
	HDEVINFO hDevInfoSet;

	typedef HKEY(__stdcall SetupDiOpenDevRegKeyFunType)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, DWORD, DWORD, REGSAM);
	typedef BOOL(__stdcall SetupDiClassGuidsFromNameFunType)(LPCTSTR, LPGUID, DWORD, PDWORD);
	typedef BOOL(__stdcall SetupDiDestroyDeviceInfoListFunType)(HDEVINFO);
	typedef BOOL(__stdcall SetupDiEnumDeviceInfoFunType)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
	typedef HDEVINFO(__stdcall SetupDiGetClassDevsFunType)(LPGUID, LPCTSTR, HWND, DWORD);
	typedef BOOL(__stdcall SetupDiGetDeviceRegistryPropertyFunType)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
	SetupDiOpenDevRegKeyFunType *SetupDiOpenDevRegKeyFunPtr;

	SetupDiClassGuidsFromNameFunType *SetupDiClassGuidsFromNameFunPtr;
	SetupDiGetClassDevsFunType *SetupDiGetClassDevsFunPtr;
	SetupDiGetDeviceRegistryPropertyFunType *SetupDiGetDeviceRegistryPropertyFunPtr;

	SetupDiDestroyDeviceInfoListFunType *SetupDiDestroyDeviceInfoListFunPtr;
	SetupDiEnumDeviceInfoFunType *SetupDiEnumDeviceInfoFunPtr;

	BOOL bMoreItems;
	SP_DEVINFO_DATA devInfo;

	jj = 0;

	TCHAR szFullPath[_MAX_PATH];
	szFullPath[0] = _T('\0');
	//Get the Windows System32 directory
	if (cio_unlikely(GetSystemDirectory(szFullPath, sizeof(szFullPath)) == 0)) {
		return (enum cio_error) - (signed int)GetLastError();	
	}

	//Setup the full path and delegate to LoadLibrary
	_tcscat_s(szFullPath, sizeof(szFullPath), _T("\\"));
	_tcscat_s(szFullPath, sizeof(szFullPath), TEXT("SETUPAPI.DLL"));
	HMODULE hLibrary = LoadLibrary(szFullPath);
	if (cio_unlikely(hLibrary == NULL)) {
		return (enum cio_error) - (signed int)GetLastError();	
	}

	SetupDiOpenDevRegKeyFunPtr = (SetupDiOpenDevRegKeyFunType *)GetProcAddress(hLibrary, "SetupDiOpenDevRegKey");

	SetupDiDestroyDeviceInfoListFunPtr = (SetupDiDestroyDeviceInfoListFunType *)GetProcAddress(hLibrary, "SetupDiDestroyDeviceInfoList");
	SetupDiEnumDeviceInfoFunPtr = (SetupDiEnumDeviceInfoFunType *)GetProcAddress(hLibrary, "SetupDiEnumDeviceInfo");

	DWORD guids = 0;
	SetupDiClassGuidsFromName(TEXT("Ports"), NULL, 0, &guids);

	return CIO_SUCCESS;
	
#if 0


#if defined _UNICODE
	SetupDiClassGuidsFromNameFunPtr = (SetupDiClassGuidsFromNameFunType *)
	    GetProcAddress(hLibrary, "SetupDiClassGuidsFromNameW");
	SetupDiGetClassDevsFunPtr =
	    (SetupDiGetClassDevsFunType *)GetProcAddress(hLibrary, "SetupDiGetClassDevsW");
	SetupDiGetDeviceRegistryPropertyFunPtr = (SetupDiGetDeviceRegistryPropertyFunType *)GetProcAddress(hLibrary, "SetupDiGetDeviceRegistryPropertyW");
#else
	SetupDiClassGuidsFromNameFunPtr = (SetupDiClassGuidsFromNameFunType *)
	    GetProcAddress(hLibrary, "SetupDiClassGuidsFromNameA");
	SetupDiGetClassDevsFunPtr = (SetupDiGetClassDevsFunType *)
	    GetProcAddress(hLibrary, "SetupDiGetClassDevsA");
	SetupDiGetDeviceRegistryPropertyFunPtr = (SetupDiGetDeviceRegistryPropertyFunType *)
	    GetProcAddress(hLibrary, "SetupDiGetDeviceRegistryPropertyA");
#endif




	int ret = FALSE;
	if (0 == dwGuids)
		return FALSE;

	//Allocate the needed memory
	pGuid = (GUID *)HeapAlloc(GetProcessHeap(),
	                          HEAP_GENERATE_EXCEPTIONS, dwGuids * sizeof(GUID));

	if (NULL == pGuid)
		return FALSE;

	//Call the function again

	if (FALSE == SetupDiClassGuidsFromNameFunPtr(TEXT("Ports"),
	                                             pGuid, dwGuids, &dwGuids)) {
		return FALSE;
	} /*if*/

	HDEVINFOhDevInfoSet = SetupDiGetClassDevsFunPtr(pGuid, NULL, NULL,
	                                        DIGCF_PRESENT /*| DIGCF_DEVICEINTERFACE*/);

	if (INVALID_HANDLE_VALUE == hDevInfoSet) {
		//Set the error to report
		_tprintf(TEXT("error SetupDiGetClassDevsFunPtr, %d"), GetLastError());
		return FALSE;
	} /*if */

	//bMoreItems = TRUE;
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	i = 0;
	jj = 0;

	do {
		HKEY hDeviceKey;
		BOOL isFound;

		isFound = FALSE;
		bMoreItems = SetupDiEnumDeviceInfoFunPtr(hDevInfoSet, i, &devInfo);
		if (FALSE == bMoreItems)
			break;

		i++;

		hDeviceKey = SetupDiOpenDevRegKeyFunPtr(hDevInfoSet, &devInfo,
		                                        DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);

		if (INVALID_HANDLE_VALUE != hDeviceKey) {
			int nPort;
			size_t nLen;
			LPTSTR pszPortName;

			nPort = 0;
			pszPortName = NULL;

			{
				//First query for the size of the registry value
				DWORD dwType;
				DWORD dwDataSize;
				LONG err;
				DWORD dwAllocatedSize;
				DWORD dwReturnedSize;
				dwType = 0;
				dwDataSize = 0;

				err = RegQueryValueEx(hDeviceKey, TEXT("PortName"), NULL,
				                      &dwType, NULL, &dwDataSize);

				if (ERROR_SUCCESS != err)
					continue;

				//Ensure the value is a string
				if (dwType != REG_SZ)
					continue;

				//Allocate enough bytes for the return value
				dwAllocatedSize = dwDataSize + sizeof(TCHAR);

				/* +sizeof(TCHAR) is to allow us to NULL terminate 
     the data if it is not null terminated in the registry
    */

				pszPortName = (LPTSTR)LocalAlloc(LMEM_FIXED, dwAllocatedSize);

				if (pszPortName == NULL)
					continue;

				//Recall RegQueryValueEx to return the data
				pszPortName[0] = TEXT('\0');
				dwReturnedSize = dwAllocatedSize;

				err = RegQueryValueEx(hDeviceKey, TEXT("PortName"), NULL,
				                      &dwType, (LPBYTE)pszPortName, &dwReturnedSize);

				if (ERROR_SUCCESS != err) {
					LocalFree(pszPortName);
					pszPortName = NULL;
					continue;
				}

				//Handle the case where the data just returned is the same size as the allocated size. This could occur where the data
				//has been updated in the registry with a non null terminator between the two calls to ReqQueryValueEx above. Rather than
				//return a potentially non-null terminated block of data, just fail the method call
				if (dwReturnedSize >= dwAllocatedSize)
					continue;

				//NULL terminate the data if it was not returned NULL terminated because it is not stored null terminated in the registry
				if (pszPortName[dwReturnedSize / sizeof(TCHAR) - 1] != _T('\0'))
					pszPortName[dwReturnedSize / sizeof(TCHAR)] = _T('\0');
			} /*local varable*/

			//If it looks like "COMX" then
			//add it to the array which will be returned
			nLen = _tcslen(pszPortName);

			if (3 < nLen) {
				if (0 == _tcsnicmp(pszPortName, TEXT("COM"), 3)) {
					if (FALSE == isdigit(pszPortName[3]))
						continue;

					//Work out the port number
					_tcsncpy(pPortName + jj * strMaxLen, pszPortName,
					         _tcsnlen(pszPortName, strMaxLen));

					//_stprintf_s(&portName[jj][0], strMaxLen, TEXT("%s"), pszPortName);
				} else {
					continue;
				} /*if 0 == _tcsnicmp(pszPortName, TEXT("COM"), 3)*/

			} /*if 3 < nLen*/

			LocalFree(pszPortName);
			isFound = TRUE;

			//Close the key now that we are finished with it
			RegCloseKey(hDeviceKey);
		} /*INVALID_HANDLE_VALUE != hDeviceKey*/

		if (FALSE == isFound)
			continue;

		//If the port was a serial port, then also try to get its friendly name
		{
			TCHAR szFriendlyName[1024];
			DWORD dwSize;
			DWORD dwType;
			szFriendlyName[0] = _T('\0');
			dwSize = sizeof(szFriendlyName);
			dwType = 0;

			if ((TRUE == SetupDiGetDeviceRegistryPropertyFunPtr(hDevInfoSet, &devInfo,
			                                                    SPDRP_DEVICEDESC, &dwType, (PBYTE)(szFriendlyName),
			                                                    dwSize, &dwSize)) &&
			    (REG_SZ == dwType)) {
				_tcsncpy(pFriendName + jj * strMaxLen, &szFriendlyName[0],
				         _tcsnlen(&szFriendlyName[0], strMaxLen));
			} else {
				_stprintf_s(pFriendName + jj * strMaxLen, strMaxLen, TEXT(""));
			} /*if SetupDiGetDeviceRegistryPropertyFunPtr */
		} /*local variable */

		jj++;
	} while (1);

	HeapFree(GetProcessHeap(), 0, pGuid);

	*pNumber = jj;

	if (0 < jj)
		ret = TRUE;

	return ret;
#endif
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);

	return CIO_SUCCESS;
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);
	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);
	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_uart *port = cio_container_of(stream, struct cio_uart, stream);
	return cio_uart_close(port);
}

size_t cio_uart_get_number_of_uarts(void)
{
	DWORD guids = 0;
	SetupDiClassGuidsFromName(TEXT("Ports"), NULL, 0, &guids);
	return (size_t)guids;
}

enum cio_error cio_uart_get_ports(struct cio_uart ports[], size_t num_ports_entries, size_t *num_detected_ports)
{
	GUID *guid_buffer = (GUID *)_malloca(num_ports_entries * sizeof(*guid_buffer));
	if (cio_unlikely(guid_buffer == NULL)) {
		return CIO_NO_MEMORY;
	}

	DWORD num_guids = (DWORD)num_ports_entries;
	if (SetupDiClassGuidsFromName(TEXT("Ports"), guid_buffer, num_guids, &num_guids) == FALSE) {
		return (enum cio_error) - (signed int)GetLastError();
	}
	
	if (cio_unlikely(num_guids != num_ports_entries)) {
		return CIO_INVALID_ARGUMENT;
	}

	HDEVINFO dev_info_set = SetupDiGetClassDevs(guid_buffer, NULL, NULL, DIGCF_PRESENT);
	if (dev_info_set == INVALID_HANDLE_VALUE) {
		return (enum cio_error) - (signed int)GetLastError();
	}

	SP_DEVINFO_DATA dev_info;
	dev_info.cbSize = sizeof(dev_info);
	DWORD member_index = 0;
	size_t detected_ports = 0;
	BOOL more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);
	enum cio_error error = CIO_SUCCESS;
	while (more_items) {
		HKEY dev_key = SetupDiOpenDevRegKey(dev_info_set, &dev_info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
		if (dev_key != INVALID_HANDLE_VALUE) {
			//Get the length of the registry entry
			DWORD entry_type = 0;
			DWORD value_size = 0;
			LONG err = RegQueryValueEx(dev_key, TEXT("PortName"), NULL, &entry_type, NULL, &value_size);
			if (cio_likely((err == ERROR_SUCCESS)) && (entry_type == REG_SZ)) {
				DWORD alloc_size = value_size + sizeof(TCHAR);
				LPTSTR port_name = (LPTSTR)_malloca(alloc_size);
				if (port_name == NULL) {
					error = CIO_NO_MEMORY;
					RegCloseKey(dev_key);
					goto out;
				}

				//Recall RegQueryValueEx to return the data
				port_name[0] = TEXT('\0');
				DWORD return_size = alloc_size;
				err = RegQueryValueEx(dev_key, TEXT("PortName"), NULL, &entry_type, (LPBYTE)port_name, &return_size);
				RegCloseKey(dev_key);
				if (cio_likely(err == ERROR_SUCCESS)) {
					detected_ports++; //TODO
				}
			}
		}

		member_index++;
		dev_info.cbSize = sizeof(dev_info);
		more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);
	}

	*num_detected_ports = detected_ports;
out:
	SetupDiDestroyDeviceInfoList(dev_info_set); 
	return CIO_SUCCESS;
}

enum cio_error cio_uart_init(struct cio_uart *port, struct cio_eventloop *loop, cio_uart_close_hook_t close_hook)
{
	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_close(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_parity(const struct cio_uart *port, enum cio_uart_parity parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_get_parity(const struct cio_uart *port, enum cio_uart_parity *parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_set_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_get_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits *num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_set_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits num_data_bits)
{
	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_get_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits *num_data_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_set_flow_control(const struct cio_uart *port, enum cio_uart_flow_control flow_control)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_get_flow_control(const struct cio_uart *port, enum cio_uart_flow_control *flow_control)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_set_baud_rate(const struct cio_uart *port, enum cio_uart_baud_rate baud_rate)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_OPERATION_NOT_SUPPORTED;
}

enum cio_error cio_uart_get_baud_rate(const struct cio_uart *port, enum cio_uart_baud_rate *baud_rate)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}
	return CIO_OPERATION_NOT_SUPPORTED;
}

struct cio_io_stream *cio_uart_get_io_stream(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return NULL;
	}

	return &port->stream;
}

const char *cio_uart_get_name(const struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return NULL;
	}

	return NULL;
}
