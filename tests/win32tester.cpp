// =================================================================================================
//
// Single-File Win32 API Testing Framework
//
// - Style: C with Classes
// - Target: 32-bit (x86) and 64-bit (x86_64) Windows
// - Compilers: Clang
// - Purpose: Provides a simple framework to test Win32 API functions
//
// =================================================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>  // For srand
#include <wchar.h>   // For wcscmp
#include <io.h>      // For _dup, _dup2, _fileno
#include <fcntl.h>   // For _O_TEXT

#pragma comment(lib, "user32.lib")

// --- Globals ---
// Holds the name of the test currently being executed.
const char* g_currentTestName = "NONE";

// =================================================================================================
// State Management
// =================================================================================================

// A structure to hold the initial, clean state of the environment.
typedef struct {
    // 1. Windows API State
    UINT initialConsoleCP;
    HANDLE initialStdOutHandle;
    HANDLE initialStdErrHandle;
    HANDLE initialStdInHandle;

    // 2. CRT Stream State (For I/O Redirection)
    int initialStdOutFD;
    int initialStdErrFD;
    int initialStdInFD;

} InitialState;

static InitialState saved_state = {0};

/**
 * @brief Saves the necessary initial system state before any test runs.
 * This should be called once, before the entire test suite starts.
 */
void save_initial_state() {
    // --- Windows API I/O Handles ---
    saved_state.initialStdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    saved_state.initialStdErrHandle = GetStdHandle(STD_ERROR_HANDLE);
    saved_state.initialStdInHandle = GetStdHandle(STD_INPUT_HANDLE);

    // --- Console Output Code Page ---
    saved_state.initialConsoleCP = GetConsoleOutputCP();

    // --- CRT Stream File Descriptors (FD) ---
    // Duplicate the descriptors so we can restore them later.
    saved_state.initialStdOutFD = _dup(_fileno(stdout));
    saved_state.initialStdErrFD = _dup(_fileno(stderr));
    saved_state.initialStdInFD = _dup(_fileno(stdin));
}

/**
 * @brief Restores the necessary system state to its initial, clean condition.
 * This is called by run_test() before the test function executes.
 */
void restore_initial_state() {
    // 1. Reset Last Error (Clear state from previous Win32 API calls)
    SetLastError(0);

    // 2. Reset C-Runtime Random Seed (for reproducible random sequences)
    srand(123);

    // 3. Reset Console Output Code Page (important if a test changed encoding)
    SetConsoleOutputCP(saved_state.initialConsoleCP);

    // 4. Reset Thread Priority (If a test boosted or lowered its priority)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    return; // TODO crash

    // 5. Reset Standard Stream File Descriptors (If a test redirected streams)

    // First, restore the CRT streams using the saved file descriptors (FDs)
    if (saved_state.initialStdOutFD != -1) {
        _dup2(saved_state.initialStdOutFD, _fileno(stdout));
        _close(saved_state.initialStdOutFD);
        _setmode(_fileno(stdout), _O_TEXT);
    }

    if (saved_state.initialStdErrFD != -1) {
        _dup2(saved_state.initialStdErrFD, _fileno(stderr));
        _close(saved_state.initialStdErrFD);
        _setmode(_fileno(stderr), _O_TEXT);
    }

    if (saved_state.initialStdInFD != -1) {
        _dup2(saved_state.initialStdInFD, _fileno(stdin));
        _close(saved_state.initialStdInFD);
        _setmode(_fileno(stdin), _O_TEXT);
    }

    // Second, restore the Windows API handles
    SetStdHandle(STD_OUTPUT_HANDLE, saved_state.initialStdOutHandle);
    SetStdHandle(STD_ERROR_HANDLE, saved_state.initialStdErrHandle);
    SetStdHandle(STD_INPUT_HANDLE, saved_state.initialStdInHandle);
}

// =================================================================================================
// Printing and Logging Utilities
//
// These functions are the only approved way to print output from within a test. They ensure
// consistent formatting and output to both the console and the debug stream.
// =================================================================================================

/**
 * @brief Generic output function that prints to stdout and the debug output.
 * @param formattedString The wide character string to print.
 */
void test_print_output(const WCHAR* formattedString) {
    // Print to standard output (console)
    wprintf(L"%s\n", formattedString);
    // Print to the Windows debug output stream (visible with a debugger like WinDbg or DebugView)
    OutputDebugStringW(formattedString);
    OutputDebugStringW(L"\n");
}

/**
 * @brief Prints an informational string within a test run.
 * @param message The string message to print.
 */
void test_print_info(const char* message) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  %hs", message);
    test_print_output(buffer);
}

/**
 * @brief Prints a standard C string (char*) value with a descriptive label.
 * @param label Description of the value being printed.
 * @param value The string value to print. Handles NULL pointers.
 */
void test_print_string(const char* label, const char* value) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  [%hs] %-25hs: %hs", g_currentTestName, label, value ? value : "NULL");
    test_print_output(buffer);
}

/**
 * @brief Prints a wide C string (wchar_t*) value with a descriptive label.
 * @param label Description of the value being printed.
 * @param value The wide string value to print. Handles NULL pointers.
 */
void test_print_wstring(const char* label, const WCHAR* value) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  [%hs] %-25hs: %ls", g_currentTestName, label, value ? value : L"NULL");
    test_print_output(buffer);
}

/**
 * @brief Prints a 32-bit unsigned integer value in both decimal and hex.
 * @param label Description of the value being printed.
 * @param value The uint32_t value.
 */
void test_print_uint32(const char* label, UINT32 value) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  [%hs] %-25hs: %u (0x%08X)", g_currentTestName, label, value, value);
    test_print_output(buffer);
}

/**
 * @brief Prints a 32-bit signed integer value in both decimal and hex.
 * @param label Description of the value being printed.
 * @param value The int32_t value.
 */
void test_print_int32(const char* label, INT32 value) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  [%hs] %-25hs: %d (0x%08X)", g_currentTestName, label, value, value);
    test_print_output(buffer);
}

/**
 * @brief Prints a boolean value.
 * @param label Description of the value being printed.
 * @param value The boolean value.
 */
void test_print_bool(const char* label, BOOL value) {
    WCHAR buffer[256];
    swprintf_s(buffer, 256, L"  [%hs] %-25hs: %s", g_currentTestName, label, value ? "TRUE" : "FALSE");
    test_print_output(buffer);
}

/**
 * @brief Prints a generic pointer value with context for its magnitude.
 * @param label Description of the value being printed.
 * @param ptr The pointer value.
 */
void test_print_ptr(const char* label, const void* ptr) {
    WCHAR buffer[256];
    if (ptr == NULL) {
        swprintf_s(buffer, 256, L"  [%hs] %-25hs: NULL", g_currentTestName, label);
    } else {
        #ifdef _WIN64
            // On 64-bit, we can check if the pointer is in the lower 4GB, which is sometimes notable.
            if ((UINT_PTR)ptr < 0xFFFFFFFF) {
                swprintf_s(buffer, 256, L"  [%hs] %-25hs: %p (<4GB)", g_currentTestName, label, ptr);
            } else {
                swprintf_s(buffer, 256, L"  [%hs] %-25hs: %p (>4GB)", g_currentTestName, label, ptr);
            }
        #else
            // On 32-bit, user space is typically <2GB.
            if ((UINT_PTR)ptr < 0x7FFFFFFF) {
                swprintf_s(buffer, 256, L"  [%hs] %-25hs: %p (<2GB)", g_currentTestName, label, ptr);
            } else {
                swprintf_s(buffer, 256, L"  [%hs] %-25hs: %p (>2GB)", g_currentTestName, label, ptr);
            }
        #endif
    }
    test_print_output(buffer);
}

/**
 * @brief Prints a Win32 HANDLE value with context for common special values.
 * @param label Description of the value being printed.
 * @param handle The handle value.
 */
void test_print_handle(const char* label, HANDLE handle) {
    WCHAR buffer[256];
    if (handle == NULL) {
        swprintf_s(buffer, 256, L"  [%hs] %-25hs: NULL_HANDLE", g_currentTestName, label);
    } else if (handle == INVALID_HANDLE_VALUE) {
        swprintf_s(buffer, 256, L"  [%hs] %-25hs: INVALID_HANDLE_VALUE", g_currentTestName, label);
    } else if (handle == HWND_DESKTOP) {
        swprintf_s(buffer, 256, L"  [%hs] %-25hs: HWND_DESKTOP (%p)", g_currentTestName, label, handle);
    } else if (handle == HWND_MESSAGE) {
		swprintf_s(buffer, 256, L"  [%hs] %-25hs: HWND_MESSAGE (%p)", g_currentTestName, label, handle);
	}
    else {
        swprintf_s(buffer, 256, L"  [%hs] %-25hs: VALID_HANDLE (%p)", g_currentTestName, label, handle);
    }
    test_print_output(buffer);
}

// =================================================================================================
// Test Cases
// =================================================================================================

// --- Dummy Window Procedure ---
LRESULT CALLBACK TestWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/**

// =================================================================================================
// Test Runner Infrastructure
// =================================================================================================

// A structure to map a test name to its function pointer.
struct TestEntry {
    const char* testName;
    void (*testFunction)();
};

/**
 * @brief Prints the list of all available tests.
 */
void print_available_tests() {
    printf("Available tests:\n");
    int testCount = sizeof(g_tests) / sizeof(TestEntry);
    for (int i = 0; i < testCount; ++i) {
        printf("  %s\n", g_tests[i].testName);
    }
    printf("\nRun all tests with: .\\win32_test_framework.exe all\n");
    printf("Run specific tests with: .\\win32_test_framework.exe test_name_1 test_name_2 ...\n");
}

}

// =================================================================================================
// Main Entry Point
// =================================================================================================
int wmain(int argc, wchar_t* argv[]) {
    return 0;
}
