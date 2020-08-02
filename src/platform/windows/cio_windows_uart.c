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
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

#include "cio_compiler.h"
#include "cio_error_code.h"
#include "cio_eventloop_impl.h"
#include "cio_uart.h"
#include "cio_util.h"

static void try_free(struct cio_uart *port)
{
	if ((port->impl.read_event.overlapped_operations_in_use == 0) && (port->impl.write_event.overlapped_operations_in_use == 0)) {
		CloseHandle(port->impl.fd);
		if (port->close_hook != NULL) {
			port->close_hook(port);
		}
	}
}

static void read_callback(struct cio_event_notifier *ev)
{
	struct cio_uart_impl *impl = cio_container_of(ev, struct cio_uart_impl, read_event);
	struct cio_uart *port = cio_container_of(impl, struct cio_uart, impl);

	DWORD recv_bytes;
	BOOL rc = GetOverlappedResult(impl->fd, &ev->overlapped, &recv_bytes, FALSE);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code;
	if (cio_unlikely(rc == FALSE)) {
		DWORD error = GetLastError();
		if (error == ERROR_OPERATION_ABORTED) {
			try_free(port);
			return;
		}

		error_code = (enum cio_error)-(LONG)(error);
	} else {
		if (recv_bytes == 0) {
			error_code = CIO_EOF;
		} else {
			port->stream.read_buffer->add_ptr += (size_t)recv_bytes;
			error_code = CIO_SUCCESS;
		}
	}

	port->stream.read_handler(&port->stream, port->stream.read_handler_context, error_code, port->stream.read_buffer);
}

static void write_callback(struct cio_event_notifier *ev)
{
	struct cio_uart_impl *impl = cio_container_of(ev, struct cio_uart_impl, write_event);
	struct cio_uart *port = cio_container_of(impl, struct cio_uart, impl);

	DWORD bytes_sent;
	BOOL rc = GetOverlappedResult(impl->fd, &ev->overlapped, &bytes_sent, FALSE);
	ev->overlapped_operations_in_use--;
	enum cio_error error_code = CIO_SUCCESS;
	if (cio_unlikely(rc == FALSE)) {
		int error = GetLastError();
		if (error == ERROR_OPERATION_ABORTED) {
			try_free(port);
			return;
		}

		bytes_sent = 0;
		error_code = (enum cio_error)-(LONG)(-error);
	}

	port->stream.write_handler(&port->stream, port->stream.write_handler_context, port->stream.write_buffer, error_code, (size_t)bytes_sent);
}

static enum cio_error stream_read(struct cio_io_stream *stream, struct cio_read_buffer *buffer, cio_io_stream_read_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);
	uart->stream.read_buffer = buffer;
	uart->stream.read_handler = handler;
	uart->stream.read_handler_context = handler_context;

	memset(&uart->impl.read_event.overlapped, 0, sizeof(uart->impl.read_event.overlapped));
	size_t space_available = cio_read_buffer_space_available(buffer);
	BOOL ret = ReadFile(uart->impl.fd, buffer->add_ptr, (DWORD)space_available, NULL, &uart->impl.read_event.overlapped);
	if (ret == FALSE) {
		DWORD error = GetLastError();
		if (cio_unlikely(error != ERROR_IO_PENDING)) {
			return (enum cio_error)-(LONG)(error);
		}
	}

	uart->impl.read_event.overlapped_operations_in_use++;
	return CIO_SUCCESS;
}

static enum cio_error stream_write(struct cio_io_stream *stream, struct cio_write_buffer *buffer, cio_io_stream_write_handler_t handler, void *handler_context)
{
	if (cio_unlikely((stream == NULL) || (buffer == NULL) || (handler == NULL))) {
		return CIO_INVALID_ARGUMENT;
	}

	struct cio_uart *uart = cio_container_of(stream, struct cio_uart, stream);
	uart->stream.write_handler = handler;
	uart->stream.write_handler_context = handler_context;
	uart->stream.write_buffer = buffer;

	size_t chain_length = cio_write_buffer_get_num_buffer_elements(buffer);

	size_t total_write_length = 0;
	struct cio_write_buffer *wb = buffer->next;
	for (size_t i = 0; i < chain_length; i++) {
		total_write_length += wb->data.element.length;
		wb = wb->next;
	}

	LPBYTE buf = (LPBYTE)_malloca(total_write_length);
	if (cio_unlikely(buf == NULL)) {
		return CIO_NO_BUFFER_SPACE;
	}

	wb = buffer->next;
	size_t write_index = 0;
	for (size_t i = 0; i < chain_length; i++) {
		memcpy(buf + write_index, wb->data.element.const_data, wb->data.element.length);
		write_index += wb->data.element.length;
		wb = wb->next;
	}

	memset(&uart->impl.write_event.overlapped, 0, sizeof(uart->impl.write_event.overlapped));

	BOOL ret = WriteFile(uart->impl.fd, buf, (DWORD)total_write_length, NULL, &uart->impl.write_event.overlapped);
	if (ret == FALSE) {
		DWORD error = GetLastError();
		if (cio_unlikely(error != ERROR_IO_PENDING)) {
			_freea(buf);
			return (enum cio_error)-(LONG)(error);
		}
	}

	uart->impl.write_event.overlapped_operations_in_use++;
	_freea(buf);
	return CIO_SUCCESS;
}

static enum cio_error stream_close(struct cio_io_stream *stream)
{
	struct cio_uart *port = cio_container_of(stream, struct cio_uart, stream);
	return cio_uart_close(port);
}

static char *get_string_value_form_registry(HDEVINFO dev_info_set, SP_DEVINFO_DATA dev_info)
{
	HKEY dev_key = SetupDiOpenDevRegKey(dev_info_set, &dev_info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
	if (cio_unlikely(dev_key == INVALID_HANDLE_VALUE)) {
		return NULL;
	}

	//Get the length of the registry entry
	DWORD entry_type = 0;
	DWORD value_size = 0;
	LONG err = RegQueryValueExW(dev_key, L"PortName", NULL, &entry_type, NULL, &value_size);
	if (cio_unlikely(err != ERROR_SUCCESS)) {
		goto out;
	}

	if (cio_unlikely(entry_type != REG_SZ)) {
		goto out;	
	}

	LPTSTR port_name = (LPTSTR)_malloca(value_size);
	if (cio_unlikely(port_name == NULL)) {
		goto out;
	}

	DWORD return_size = value_size;
	err = RegQueryValueEx(dev_key, TEXT("PortName"), NULL, &entry_type, (LPBYTE)port_name, &return_size);
	if (cio_unlikely(err != ERROR_SUCCESS)) {
		goto free_port_name_mem;
	}

	RegCloseKey(dev_key);
	int utf8_len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, port_name, return_size, NULL, 0, NULL, NULL);
	if (cio_unlikely(utf8_len == 0)) {
		goto free_port_name_mem;	
	}

	char* utf8_port_name = malloc((size_t)utf8_len + 1);
	if (cio_unlikely(utf8_port_name == NULL)) {
		goto free_port_name_mem;
	}

	utf8_len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, port_name, return_size, utf8_port_name, utf8_len + 1, NULL, NULL);
	if (cio_unlikely(utf8_len == 0)) {
		goto free_utf8_port_name_mem;
	}

	_freea(port_name);
	return utf8_port_name;

free_utf8_port_name_mem:
	_freea(utf8_port_name);
free_port_name_mem:
	_freea(port_name);
out:
	RegCloseKey(dev_key);
	return NULL;
}

static bool is_com_port(const char *port_name)
{
	size_t len = strlen(port_name);
	if (len > 3) {
		if (strncmp(port_name, "COM", 3) == 0) {
			if (isdigit(port_name[3]) != 0) {
				return true;
			}
		}
	}

	return false;
}

static char *get_device_property(HDEVINFO dev_info, PSP_DEVINFO_DATA dev_data, DWORD property)
{
	DWORD buff_size = 0;
	SetupDiGetDeviceRegistryPropertyW(dev_info, dev_data, property, NULL, NULL, 0, &buff_size);
	
	LPTSTR buff = (LPTSTR)malloc(buff_size);
	if (cio_unlikely(buff == NULL)) {
		return NULL;
	}

	if (SetupDiGetDeviceRegistryPropertyW(dev_info, dev_data, property, NULL, (PBYTE)buff, buff_size, NULL)) {
		int utf8_len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buff, buff_size, NULL, 0, NULL, NULL);
		if (cio_unlikely(utf8_len == 0)) {
			goto free_buffer;
		}

		char *utf8_name = malloc((size_t)utf8_len + 1);
		if (cio_unlikely(utf8_name == NULL)) {
			goto free_buffer;
		}

		utf8_len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buff, buff_size, utf8_name, utf8_len + 1, NULL, NULL);
		if (cio_unlikely(utf8_len == 0)) {
			free(utf8_name);
			goto free_buffer;
		}

		return utf8_name;
	}

free_buffer:
	free(buff);
	return NULL;
}

static enum cio_error set_comm_settings(const struct cio_uart *port, DCB *settings)
{
	BOOL ret = SetCommState(port->impl.fd, settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	return CIO_SUCCESS;
}

size_t cio_uart_get_number_of_uarts(void)
{
	DWORD num_guids = 0;
	SetupDiClassGuidsFromName(TEXT("Ports"), NULL, 0, &num_guids);

	GUID *guid_buffer = (GUID *)_malloca(num_guids * sizeof(*guid_buffer));
	if (cio_unlikely(guid_buffer == NULL)) {
		return 0;
	}

	if (SetupDiClassGuidsFromNameW(L"Ports", guid_buffer, num_guids, &num_guids) == FALSE) {
		goto free_guid_buffer;
	}

	HDEVINFO dev_info_set = SetupDiGetClassDevs(guid_buffer, NULL, NULL, DIGCF_PRESENT);
	if (dev_info_set == INVALID_HANDLE_VALUE) {
		goto free_guid_buffer;
	}

	SP_DEVINFO_DATA dev_info;
	dev_info.cbSize = sizeof(dev_info);
	DWORD member_index = 0;
	size_t detected_ports = 0;
	BOOL more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);

	while (more_items) {
		char *port_name = get_string_value_form_registry(dev_info_set, dev_info);
		if (cio_likely(port_name != NULL)) {
			if (is_com_port(port_name)) {
				detected_ports++;
			}

			free(port_name);
		}

		member_index++;
		dev_info.cbSize = sizeof(dev_info);
		more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);
	}

	SetupDiDestroyDeviceInfoList(dev_info_set);
	return detected_ports;

free_guid_buffer:
	_freea(guid_buffer);
	return 0;
}

enum cio_error cio_uart_get_ports(struct cio_uart ports[], size_t num_ports_entries, size_t *num_detected_ports)
{
	if (cio_unlikely(num_ports_entries == 0)) {
		*num_detected_ports = 0;
		return CIO_SUCCESS;
	}

	GUID *guid_buffer = (GUID *)_malloca(num_ports_entries * sizeof(*guid_buffer));
	if (cio_unlikely(guid_buffer == NULL)) {
		return CIO_NO_MEMORY;
	}

	enum cio_error error = CIO_SUCCESS;
	DWORD num_guids = (DWORD)num_ports_entries;
	if (SetupDiClassGuidsFromName(TEXT("Ports"), guid_buffer, num_guids, &num_guids) == FALSE) {
		error = (enum cio_error) - (signed int)GetLastError();
		goto free_guid_buffer;
	}
	
	HDEVINFO dev_info_set = SetupDiGetClassDevs(guid_buffer, NULL, NULL, DIGCF_PRESENT);
	if (dev_info_set == INVALID_HANDLE_VALUE) {
		error = (enum cio_error) - (signed int)GetLastError();
		goto free_guid_buffer;
	}

	SP_DEVINFO_DATA dev_info;
	dev_info.cbSize = sizeof(dev_info);
	DWORD member_index = 0;
	size_t detected_ports = 0;
	BOOL more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);

	while (more_items) {
		char *port_name = get_string_value_form_registry(dev_info_set, dev_info);
		if (cio_likely(port_name != NULL)) {
			if (is_com_port(port_name)) {
				strncpy_s(ports[detected_ports].impl.name, sizeof(ports[detected_ports].impl.name), port_name, _TRUNCATE);

				char *friendly_name = get_device_property(dev_info_set, &dev_info, SPDRP_FRIENDLYNAME);
				if (friendly_name != NULL) {
					strncpy_s(ports[detected_ports].impl.friendly_name, sizeof(ports[detected_ports].impl.friendly_name), friendly_name, _TRUNCATE);
				} else {
					ports[detected_ports].impl.friendly_name[0] = '\0';
				}

				detected_ports++;
			}

			free(port_name);
			if (detected_ports >= num_ports_entries) {
				goto out;
			}
		}

		member_index++;
		dev_info.cbSize = sizeof(dev_info);
		more_items = SetupDiEnumDeviceInfo(dev_info_set, member_index, &dev_info);
	}

out:
	*num_detected_ports = detected_ports;
	SetupDiDestroyDeviceInfoList(dev_info_set);
free_guid_buffer:
	_freea(guid_buffer);
	return CIO_SUCCESS;
}

enum cio_error cio_uart_init(struct cio_uart *port, struct cio_eventloop *loop, cio_uart_close_hook_t close_hook)
{
	if (cio_unlikely(port == NULL) || (loop == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	char file_name[_MAX_PATH + 7] = "\\\\.\\";
	strcat_s(file_name, sizeof(file_name), port->impl.name);

	int wchar_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, file_name, -1, NULL, 0);
	if (cio_unlikely(wchar_len == 0)) {
		return (enum cio_error) (-(signed int)GetLastError());
	}

	size_t wchar_buffer_size = (size_t)wchar_len + sizeof(WCHAR);
	LPWSTR wchar_name = malloc(wchar_buffer_size);
	if (cio_unlikely(wchar_name == NULL)) {
		return CIO_NO_MEMORY;
	}

	enum cio_error err = CIO_SUCCESS;
	wchar_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, file_name, -1, wchar_name, (int)wchar_buffer_size);
	if (cio_unlikely(wchar_len == 0)) {
		err = (enum cio_error)(-(signed int)GetLastError());
		goto free_wchar_buffer;
	}

	HANDLE comm = CreateFileW(wchar_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (cio_unlikely(comm == INVALID_HANDLE_VALUE)) {
		err = (enum cio_error)(-(signed int)GetLastError());
		goto free_wchar_buffer;
	}

	//free(wchar_name); TODO

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(comm, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		err = (enum cio_error)(-(LONG)GetLastError());
		goto close_handle;
	}

	current_settings.BaudRate = CBR_115200;
	current_settings.ByteSize = 8;
	current_settings.Parity = NOPARITY;
	current_settings.StopBits = ONESTOPBIT;
	current_settings.fInX = FALSE;
	current_settings.fOutX = FALSE;
	current_settings.fTXContinueOnXoff = FALSE;
	current_settings.fOutxCtsFlow = FALSE;
	current_settings.fRtsControl = RTS_CONTROL_DISABLE; // RTS_CONTROL_ENABLE ??
	current_settings.fDtrControl = DTR_CONTROL_DISABLE; // DTR_CONTROL_ENABLE ??
	current_settings.fOutxDsrFlow = FALSE;
	current_settings.fDsrSensitivity = FALSE;

	port->impl.fd = comm;
	err = set_comm_settings(port, &current_settings);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		goto close_handle;
	}

	COMMTIMEOUTS current_timeouts = {0};
	ret = GetCommTimeouts(port->impl.fd, &current_timeouts);
	if (cio_unlikely(ret == FALSE)) {
		err = (enum cio_error)(-(LONG)GetLastError());
		goto close_handle;
	}

	current_timeouts.ReadTotalTimeoutConstant = 0;
	current_timeouts.ReadTotalTimeoutMultiplier = 0;
	current_timeouts.WriteTotalTimeoutConstant = 0;
	current_timeouts.WriteTotalTimeoutMultiplier = 0;

	ret = SetCommTimeouts(port->impl.fd, &current_timeouts);
	if (cio_unlikely(ret == FALSE)) {
		err = (enum cio_error)(-(LONG)GetLastError());
		goto close_handle;
	}

	err = cio_windows_add_handle_to_completion_port(comm, loop, &port->impl);
	if (cio_unlikely(err != CIO_SUCCESS)) {
		CloseHandle(comm);
		goto close_handle;
	}

	port->impl.loop = loop;
	port->close_hook = close_hook;

	port->impl.read_event.callback = read_callback;
	port->impl.read_event.overlapped_operations_in_use = 0;
	port->impl.write_event.callback = write_callback;
	port->impl.write_event.overlapped_operations_in_use = 0;

	port->stream.read_some = stream_read;
	port->stream.write_some = stream_write;
	port->stream.close = stream_close;

	return err;

close_handle:
	CloseHandle(comm);
free_wchar_buffer:
	free(wchar_name);
	return err;
}

enum cio_error cio_uart_close(struct cio_uart *port)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	CancelIo(port->impl.fd);
	try_free(port);

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_parity(const struct cio_uart *port, enum cio_uart_parity parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (parity) {
	case CIO_UART_PARITY_NONE:
		current_settings.Parity = NOPARITY;
		break;
	case CIO_UART_PARITY_ODD:
		current_settings.Parity = ODDPARITY;
		break;
	case CIO_UART_PARITY_EVEN:
		current_settings.Parity = EVENPARITY;
		break;
	case CIO_UART_PARITY_MARK:
		current_settings.Parity = MARKPARITY;
		break;
	case CIO_UART_PARITY_SPACE:
		current_settings.Parity = SPACEPARITY;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	return set_comm_settings(port, &current_settings);
}

enum cio_error cio_uart_get_parity(const struct cio_uart *port, enum cio_uart_parity *parity)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (current_settings.Parity) {
	case NOPARITY:
		*parity = CIO_UART_PARITY_NONE;
		break;
	case ODDPARITY:
		*parity = CIO_UART_PARITY_ODD;
		break;
	case EVENPARITY:
		*parity = CIO_UART_PARITY_EVEN;
		break;
	case MARKPARITY:
		*parity = CIO_UART_PARITY_MARK;
		break;
	case SPACEPARITY:
		*parity = CIO_UART_PARITY_SPACE;
		break;
	default:
		return CIO_PROTOCOL_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (num_stop_bits) {
	case CIO_UART_ONE_STOP_BIT:
		current_settings.StopBits = ONESTOPBIT;
		break;
	case CIO_UART_TWO_STOP_BITS:
		current_settings.StopBits = TWOSTOPBITS;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	return set_comm_settings(port, &current_settings);
}

enum cio_error cio_uart_get_num_stop_bits(const struct cio_uart *port, enum cio_uart_num_stop_bits *num_stop_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (current_settings.StopBits) {
	case ONESTOPBIT:
		*num_stop_bits = CIO_UART_ONE_STOP_BIT;
		break;
	case TWOSTOPBITS:
		*num_stop_bits = CIO_UART_TWO_STOP_BITS;
		break;
	default:
		return CIO_OPERATION_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits num_data_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (num_data_bits) {
	case CIO_UART_5_DATA_BITS:
		current_settings.ByteSize = 5;
		break;
	case CIO_UART_6_DATA_BITS:
		current_settings.ByteSize = 6;
		break;
	case CIO_UART_7_DATA_BITS:
		current_settings.ByteSize = 7;
		break;
	case CIO_UART_8_DATA_BITS:
		current_settings.ByteSize = 8;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	return set_comm_settings(port, &current_settings);
}

enum cio_error cio_uart_get_num_data_bits(const struct cio_uart *port, enum cio_uart_num_data_bits *num_data_bits)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (current_settings.ByteSize) {
	case 5:
		*num_data_bits = CIO_UART_5_DATA_BITS;
		break;
	case 6:
		*num_data_bits = CIO_UART_6_DATA_BITS;
		break;
	case 7:
		*num_data_bits = CIO_UART_7_DATA_BITS;
		break;
	case 8:
		*num_data_bits = CIO_UART_8_DATA_BITS;
		break;
	default:
		return CIO_PROTOCOL_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
}

enum cio_error cio_uart_set_flow_control(const struct cio_uart *port, enum cio_uart_flow_control flow_control)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	return CIO_SUCCESS;
	//return CIO_OPERATION_NOT_SUPPORTED;
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

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (baud_rate) {
	case CIO_UART_BAUD_RATE_110:
		current_settings.BaudRate = CBR_110;
		break;
	case CIO_UART_BAUD_RATE_300:
		current_settings.BaudRate = CBR_300;
		break;
	case CIO_UART_BAUD_RATE_600:
		current_settings.BaudRate = CBR_600;
		break;
	case CIO_UART_BAUD_RATE_1200:
		current_settings.BaudRate = CBR_1200;
		break;
	case CIO_UART_BAUD_RATE_2400:
		current_settings.BaudRate = CBR_2400;
		break;
	case CIO_UART_BAUD_RATE_4800:
		current_settings.BaudRate = CBR_4800;
		break;
	case CIO_UART_BAUD_RATE_9600:
		current_settings.BaudRate = CBR_9600;
		break;
	case CIO_UART_BAUD_RATE_19200:
		current_settings.BaudRate = CBR_19200;
		break;
	case CIO_UART_BAUD_RATE_38400:
		current_settings.BaudRate = CBR_38400;
		break;
	case CIO_UART_BAUD_RATE_57600:
		current_settings.BaudRate = CBR_57600;
		break;
	case CIO_UART_BAUD_RATE_115200:
		current_settings.BaudRate = CBR_115200;
		break;
	default:
		return CIO_INVALID_ARGUMENT;
	}

	return set_comm_settings(port, &current_settings);
}

enum cio_error cio_uart_get_baud_rate(const struct cio_uart *port, enum cio_uart_baud_rate *baud_rate)
{
	if (cio_unlikely(port == NULL)) {
		return CIO_INVALID_ARGUMENT;
	}

	DCB current_settings = {0};
	current_settings.DCBlength = sizeof(current_settings);
	BOOL ret = GetCommState(port->impl.fd, &current_settings);
	if (cio_unlikely(ret == FALSE)) {
		return (enum cio_error)(-(signed int)GetLastError());
	}

	switch (current_settings.BaudRate) {
	case CBR_110:
		*baud_rate = CIO_UART_BAUD_RATE_110;
		break;
	case CBR_300:
		*baud_rate = CIO_UART_BAUD_RATE_300;
		break;
	case CBR_600:
		*baud_rate = CIO_UART_BAUD_RATE_600;
		break;
	case CBR_1200:
		*baud_rate = CIO_UART_BAUD_RATE_1200;
		break;
	case CBR_2400:
		*baud_rate = CIO_UART_BAUD_RATE_2400;
		break;
	case CBR_4800:
		*baud_rate = CIO_UART_BAUD_RATE_4800;
		break;
	case CBR_9600:
		*baud_rate = CIO_UART_BAUD_RATE_9600;
		break;
	case CBR_19200:
		*baud_rate = CIO_UART_BAUD_RATE_19200;
		break;
	case CBR_38400:
		*baud_rate = CIO_UART_BAUD_RATE_38400;
		break;
	case CBR_57600:
		*baud_rate = CIO_UART_BAUD_RATE_57600;
		break;
	case CBR_115200:
		*baud_rate = CIO_UART_BAUD_RATE_115200;
		break;
	default:
		return CIO_PROTOCOL_NOT_SUPPORTED;
	}

	return CIO_SUCCESS;
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

	if (port->impl.friendly_name[0] != '\0') {
		return port->impl.friendly_name;
	} else {
		return port->impl.name;
	}
}
