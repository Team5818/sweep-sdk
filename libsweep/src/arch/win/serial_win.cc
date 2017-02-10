#include "serial.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <windows.h>
#include <tchar.h>

namespace sweep {
namespace serial {

typedef struct error {
  const char* what; // always literal, do not free
} error;

typedef struct device {
  HANDLE fd;
  OVERLAPPED ro_;
  OVERLAPPED wo_;
} device;

// Constructor hidden from users
static error_s error_construct(const char* what) {
  SWEEP_ASSERT(what);

  auto out = new error{what};
  return out;
}

const char* error_message(error_s error) {
  SWEEP_ASSERT(error);

  return error->what;
}

void error_destruct(error_s error) {
  SWEEP_ASSERT(error);

  delete error;
}

static int32_t detail_get_port_number(const char* port, error_s* error) {
  SWEEP_ASSERT(port);
  SWEEP_ASSERT(error);

  if (strlen(port) <= 3) {
    *error = error_construct("invalid port name");
    return -1;
  }

  char* end;
  long parsed_int = std::strtoll(port + 3, &end, 10);

  if (parsed_int >= 0 && parsed_int <= 255)
    return parsed_int;

  *error = error_construct("invalid port name");
  return -1;
}

device_s device_construct(const char* port, int32_t bitrate, error_s* error) {
  SWEEP_ASSERT(port);
  SWEEP_ASSERT(bitrate > 0);
  SWEEP_ASSERT(error);

  if (bitrate != 115200) {
    *error = error_construct("baud rate is not supported");
    return nullptr;
  }

  // read port number from the port name
  error_s port_num_error = nullptr;
  int port_num = detail_get_port_number(port, &port_num_error);

  if (port_num_error) {
    *error = port_num_error;
    return nullptr;
  }

  TCHAR port_name[32];
  _stprintf_s(port_name, sizeof(port_name) / sizeof(TCHAR), _T("\\\\.\\COM%d"), port_num);

  // try to open the port
  HANDLE hComm = CreateFile(port_name,                    // port name
                            GENERIC_READ | GENERIC_WRITE, // read/write
                            0,                            // No Sharing (serial ports can't be shared)
                            NULL,                         // No Security
                            OPEN_EXISTING,                // Open existing port only
                            0,                            // Non Overlapped I/O
                            NULL);                        // Null for serial ports

  if (hComm == INVALID_HANDLE_VALUE) {
    *error = error_construct("opening serial port failed");
    return nullptr;
  }

  // retrieve the current comm state
  DCB dcb_serial_params;
  dcb_serial_params.DCBlength = sizeof(dcb_serial_params);
  if (!GetCommState(hComm, &dcb_serial_params)) {
    *error = error_construct("retrieving current serial port state failed");
    CloseHandle(hComm);
    return nullptr;
  }

  // set the parameters to match the uART settings from the Sweep Comm Protocol
  dcb_serial_params.BaudRate = CBR_115200; // BaudRate = 115200 (115.2kb/s)
  dcb_serial_params.ByteSize = 8;          // ByteSize = 8
  dcb_serial_params.StopBits = ONESTOPBIT; // # StopBits = 1
  dcb_serial_params.Parity = NOPARITY;     // Parity = None
  dcb_serial_params.fDtrControl = DTR_CONTROL_DISABLE;

  // set the serial port parameters to the specified values
  if (!SetCommState(hComm, &dcb_serial_params)) {
    *error = error_construct("setting serial port parameters failed");
    CloseHandle(hComm);
    return nullptr;
  }

  // specify timeouts (all values in milliseconds)
  COMMTIMEOUTS timeouts;
  if (!GetCommTimeouts(hComm, &timeouts)) {
    *error = error_construct("retrieving current serial port timeouts failed");
    CloseHandle(hComm);
    return nullptr;
  }
  timeouts.ReadIntervalTimeout = 50;         // max time between arrival of two bytes before ReadFile() returns
  timeouts.ReadTotalTimeoutConstant = 50;    // used to calculate total time-out period for read operations
  timeouts.ReadTotalTimeoutMultiplier = 10;  // used to calculate total time-out period for read operations
  timeouts.WriteTotalTimeoutConstant = 50;   // used to calculate total time-out period for write operations
  timeouts.WriteTotalTimeoutMultiplier = 10; // used to calculate total time-out period for write operations

  // set the timeouts
  if (!SetCommTimeouts(hComm, &timeouts)) {
    *error = error_construct("setting serial port timeouts failed");
    CloseHandle(hComm);
    return nullptr;
  }

  // create the overlapped os reader
  OVERLAPPED os_reader = {0};
  OVERLAPPED os_write = {0};

  // Create the overlapped read event. Must be closed before exiting
  os_reader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (os_reader.hEvent == NULL) {
    *error = error_construct("creating overlapped read event failed");
    CloseHandle(hComm);
    return nullptr;
  }
  // Create the overlapped write event. Must be closed before exiting
  os_write.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (os_write.hEvent == NULL) {
    *error = error_construct("creating overlapped write event failed");
    CloseHandle(hComm);
    return nullptr;
  }

  // set the comm mask
  if (!SetCommMask(hComm, EV_RXCHAR | EV_ERR)) {
    *error = error_construct("setting comm mask failed");
    CloseHandle(hComm);
    CloseHandle(os_reader.hEvent);
    CloseHandle(os_write.hEvent);
    return nullptr;
  }

  // purge the comm port of any pre-existing data or errors
  if (!PurgeComm(hComm, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR)) {
    *error = error_construct("flushing serial port failed during serial device construction");
    CloseHandle(hComm);
    CloseHandle(os_reader.hEvent);
    CloseHandle(os_write.hEvent);
    return nullptr;
  }

  // create the serial device
  auto out = new device{hComm, os_reader, os_write};

  return out;
}

void device_destruct(device_s serial) {
  SWEEP_ASSERT(serial);

  error_s ignore = nullptr;
  device_flush(serial, &ignore);

  // try to close the serial port
  if (!CloseHandle(serial->fd)) {
    ignore = error_construct("closing serial port failed");
  }

  // try to close the overlapped read event
  if (!CloseHandle(serial->ro_.hEvent)) {
    ignore = error_construct("closing serial port overlapped read event failed");
  }

  // try to close the overlapped write event
  if (!CloseHandle(serial->wo_.hEvent)) {
    ignore = error_construct("closing serial port overlapped write event failed");
  }

  (void)ignore; // nothing we can do here

  delete serial;
}

void device_read(device_s serial, void* to, int32_t len, error_s* error) {
  SWEEP_ASSERT(serial);
  SWEEP_ASSERT(to);
  SWEEP_ASSERT(len >= 0);
  SWEEP_ASSERT(error);

  DWORD rx_read = 0;
  DWORD total_num_bytes_read = 0;

  // read bytes until "len" bytes have been read
  while (total_num_bytes_read < (DWORD)len) {

    if (!ReadFile(serial->fd, (char*)to + total_num_bytes_read, len - total_num_bytes_read, &rx_read, &(serial->ro_))) {
      *error = error_construct("reading from serial device failed");
      return;
    }
    total_num_bytes_read += rx_read;
  }

  SWEEP_ASSERT(total_num_bytes_read == (DWORD)len && "reliable read failed to read requested number of bytes");
}

void device_write(device_s serial, const void* from, int32_t len, error_s* error) {
  SWEEP_ASSERT(serial);
  SWEEP_ASSERT(from);
  SWEEP_ASSERT(len >= 0);
  SWEEP_ASSERT(error);

  DWORD err;
  DWORD tx_written = 0;
  DWORD total_num_bytes_written = 0;

  // clear the comm errors, and purge if need be
  if (ClearCommError(serial->fd, &err, NULL) && err > 0) {
    if (!PurgeComm(serial->fd, PURGE_TXABORT | PURGE_TXCLEAR)) {
      *error = error_construct("purging tx buffer failed during seiral port write");
      return;
    }
  }

  // write bytes until "len" bytes have been written
  while (tx_written < (DWORD)len) {
    if (!WriteFile(serial->fd,                                  // Handle to the Serial port
                   (const char*)from + total_num_bytes_written, // Data to be written to the port
                   len - total_num_bytes_written,               // No of bytes to write
                   &tx_written,                                 // Bytes written
                   &(serial->wo_))) {
      *error = error_construct("writing to serial device failed");
      return;
    }
    total_num_bytes_written += tx_written;
  }

  SWEEP_ASSERT(((int32_t)total_num_bytes_written == len) && "reliable write failed to write requested number of bytes");
}

void device_flush(device_s serial, error_s* error) {
  SWEEP_ASSERT(serial);
  SWEEP_ASSERT(error);

  // TODO: Before flushing/purging the port, should check for
  // and handle or explicitly ignore any pending port erros

  // flush serial port
  // - Empty Tx and Rx buffers
  // - Abort any pending read/write operations
  if (!PurgeComm(serial->fd, PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR)) {
    *error = error_construct("flushing serial port failed");
  }
}

} // ns serial
} // ns sweep