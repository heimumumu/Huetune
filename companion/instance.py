"""Cooperative shutdown so the installer can release the USB port."""
import ctypes
from ctypes import wintypes

MUTEX = r"Local\ESP32MediaController"
EXIT_EVENT = MUTEX + ".Exit"


def api(name, result, *arguments):
    function = getattr(ctypes.WinDLL("kernel32", use_last_error=True), name)
    function.argtypes = arguments
    function.restype = result
    return function


def close(handle):
    api("CloseHandle", wintypes.BOOL, wintypes.HANDLE)(handle)


def running():
    handle = api("OpenMutexW", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL,
                 wintypes.LPCWSTR)(0x00100000, False, MUTEX)
    if handle:
        close(handle)
    return bool(handle)


def request_exit():
    handle = api("OpenEventW", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL,
                 wintypes.LPCWSTR)(0x0002, False, EXIT_EVENT)
    if not handle:
        return False
    try:
        return bool(signal(handle))
    finally:
        close(handle)


def signal(handle):
    return api("SetEvent", wintypes.BOOL, wintypes.HANDLE)(handle)


def create_exit_event():
    handle = api("CreateEventW", wintypes.HANDLE, ctypes.c_void_p, wintypes.BOOL,
                 wintypes.BOOL, wintypes.LPCWSTR)(None, False, False, EXIT_EVENT)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    return handle


def wait_for_exit(handle):
    return api("WaitForSingleObject", wintypes.DWORD, wintypes.HANDLE,
               wintypes.DWORD)(handle, 0xffffffff) == 0
