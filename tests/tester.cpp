// =================================================================================================
//
// Enhanced Single-File Win32 API Testing Framework
//
// - Style: C with Classes
// - Target: 32-bit (x86) and 64-bit (x86_64) Windows
// - Compilers: Clang
// - Purpose: Provides a simple framework to test Win32 API functions with JSON output for
//   automated conformance testing (e.g., matching Windows behavior in WineHQ/ReactOS).
//
// =================================================================================================

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>  // For srand
#include <wchar.h>   // For wcscmp
#include <io.h>      // For _dup, _dup2, _fileno
#include <fcntl.h>   // For _O_TEXT
#include <string.h>  // For strcmp, etc.

#pragma comment(lib, "user32.lib")

// --- Globals ---
const char* g_currentTestName = "NONE";
int g_Failed = 0;
#define MAX_FAILED_TESTS 20
char g_failed_test_names[MAX_FAILED_TESTS][128];
int g_num_failed_tests = 0;

/**
 * @brief Structure for a subtest result.
 */
struct SubTest {
    char name[128];
    bool passed;
    char reason[512];
};

// =================================================================================================
// JSON Output Utilities
// =================================================================================================

/**
 * @brief Enhanced JSON string escaper (handles quotes, backslashes, newlines, etc.).
 * @param dest Output buffer.
 * @param src Input string.
 * @param destsz Size of dest.
 * @return Length written.
 */
int json_escape_string(char* dest, const char* src, size_t destsz) {
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < destsz - 2) {
        unsigned char c = src[i];
        if (c == '"' || c == '\\') {
            dest[j++] = '\\';
            dest[j++] = c;
        } else if (c == '\n') {
            dest[j++] = '\\';
            dest[j++] = 'n';
        } else if (c == '\r') {
            dest[j++] = '\\';
            dest[j++] = 'r';
        } else if (c == '\t') {
            dest[j++] = '\\';
            dest[j++] = 't';
        } else if (c < 32) {
            dest[j++] = '?';  // Placeholder for other controls
        } else {
            dest[j++] = c;
        }
        i++;
    }
    dest[j] = '\0';
    return (int)j;
}

/**
 * @brief Outputs a complete JSON object for a test result via printf.
 * @param testName Name of the test.
 * @param overallPassed True if test passed.
 * @param subtests Array of subtest results.
 * @param numSubtests Number of subtests.
 */
void output_test_json(const char* testName, bool overallPassed, const struct SubTest* subtests, int numSubtests) {
    char escapedName[256];
    json_escape_string(escapedName, testName, sizeof(escapedName));

    char escapedReason[512];
    printf("{\n");
    printf("    \"test\": \"%s\",\n", escapedName);
    printf("    \"result\": \"%s\",\n", overallPassed ? "passed" : "failed");
    printf("    \"subtests\": [\n");
    for (int i = 0; i < numSubtests; i++) {
        json_escape_string(escapedReason, subtests[i].reason, sizeof(escapedReason));
        printf("      {\n");
        printf("        \"name\": \"%s\",\n", subtests[i].name);
        printf("        \"result\": \"%s\",\n", subtests[i].passed ? "passed" : "failed");
        printf("        \"reason\": \"%s\"\n", escapedReason);
        printf("      }%s\n", (i < numSubtests - 1) ? "," : "");
    }
    printf("    ]\n");
    printf("  }");
}

// =================================================================================================
// Assertion and Subtest Management
// =================================================================================================

/**
 * @brief Adds a subtest result to the array (assumes pre-allocated array).
 * @param subtests Array to append to.
 * @param numSubtests Pointer to current count (incremented).
 * @param name Subtest name.
 * @param passed True if passed.
 * @param reason Explanation (empty if passed).
 * @param file Source file (for failure location).
 * @param line Source line (for failure location).
 */
void add_subtest(struct SubTest* subtests, int* numSubtests, const char* name, bool passed, const char* reason, const char* file, int line) {
    if (*numSubtests >= 10) return;  // Fixed limit; expand as needed
    strncpy_s(subtests[*numSubtests].name, sizeof(subtests[*numSubtests].name), name, _TRUNCATE);
    subtests[*numSubtests].passed = passed;
    if (passed) {
        subtests[*numSubtests].reason[0] = '\0';
    } else {
        char temp[512];
        if (reason && reason[0] != '\0') {
            snprintf(temp, sizeof(temp), "%s at %s:%d", reason, file ? file : "unknown", line);
        } else {
            snprintf(temp, sizeof(temp), "Failed at %s:%d", file ? file : "unknown", line);
        }
        strncpy_s(subtests[*numSubtests].reason, sizeof(subtests[*numSubtests].reason), temp, _TRUNCATE);
    }
    (*numSubtests)++;
}

/**
 * @brief Asserts a boolean condition for a subtest.
 */
#define ASSERT_SUBTEST(subtests, numSubtests, name, condition, failMsg) \
    do { \
        bool __cond = !!(condition); \
        add_subtest(subtests, numSubtests, name, __cond, __cond ? "" : failMsg, __FILE__, __LINE__); \
    } while(0)

/**
 * @brief Specialized assertion for GetLastError() comparisons.
 * @param subtests Array to append to.
 * @param numSubtests Pointer to count.
 * @param name Subtest name.
 * @param expected The expected error code (DWORD).
 * @param failMsg Base failure message (appended with actual value if failed).
 */
#define ASSERT_LAST_ERROR(subtests, numSubtests, name, expected, failMsg) \
    do { \
        DWORD __actual = GetLastError(); \
        bool __cond = (__actual == (expected)); \
        char __reason[512]; \
        if (__cond) { \
            __reason[0] = '\0'; \
        } else { \
            snprintf(__reason, sizeof(__reason), "%s (actual: %lu / 0x%08lX, expected: %lu / 0x%08lX)", failMsg, (unsigned long)__actual, (unsigned long)__actual, (unsigned long)expected, (unsigned long)expected); \
        } \
        add_subtest(subtests, numSubtests, name, __cond, __reason, __FILE__, __LINE__); \
    } while(0)

/**
 * @brief Expects a crash (access violation) in the try block.
 */
#define EXPECT_CRASH(subtests, numSubtests, name, codeBlock) \
    do { \
        __try { \
            codeBlock; \
            add_subtest(subtests, numSubtests, name, false, "Expected crash but did not occur", __FILE__, __LINE__); \
        } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { \
            add_subtest(subtests, numSubtests, name, true, "", __FILE__, __LINE__); \
        } \
    } while(0)

// =================================================================================================
// Validation Helpers
// =================================================================================================

/**
 * @brief Validates a class ATOM using bit-level checks per Win32 spec.
 * @param atom The ATOM to validate.
 * @param hinst The instance handle.
 * @return True if valid registered class.
 */
bool is_valid_class_atom(ATOM atom, HINSTANCE hinst = GetModuleHandle(NULL)) {
    if (atom == 0) return false;
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    return GetClassInfoExW(hinst, MAKEINTATOM(atom), &wc) != 0;
    // TODO
    // if (atom == 0) return false;
    // UINT16 uatom = (UINT16)atom;
    // printf("\nuatom %d\n", uatom);
    // return (uatom >= 0x0001) && (uatom <= 0xBFFF);
    // if (uatom < 0x4000 || uatom > 0xBFFF) return false;
    // if ((uatom >> 8) == 0) return false;  // High byte must be non-zero
    // return true;
}

/**
 * @brief Validates an HWND using bit-level checks per Win32 spec.
 * @param hwnd The HWND to validate.
 * @return True if non-reserved, low index non-zero.
 */
bool is_valid_hwnd(HWND hwnd) {
    if (hwnd == NULL || hwnd == INVALID_HANDLE_VALUE) return false;
    // Exclude standard reserved HWND constants
    if (hwnd == HWND_TOP || hwnd == HWND_BOTTOM || hwnd == HWND_TOPMOST || hwnd == HWND_NOTOPMOST || hwnd == HWND_MESSAGE) return false;
    // Exclude desktop (dynamic, but approximate via low bits)
    HWND desktop = GetDesktopWindow();
    if (hwnd == desktop) return false;
    // Low 16 bits (index) must be non-zero for user windows
    if (LOWORD((DWORD_PTR)hwnd) == 0) return false;
    return true;
}

// =================================================================================================
// State Management
// =================================================================================================

typedef struct {
    UINT initialConsoleCP;
    HANDLE initialStdOutHandle;
    HANDLE initialStdErrHandle;
    HANDLE initialStdInHandle;
    int initialStdOutFD;
    int initialStdErrFD;
    int initialStdInFD;
} InitialState;

static InitialState saved_state = {0};

void save_initial_state() {
    saved_state.initialStdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    saved_state.initialStdErrHandle = GetStdHandle(STD_ERROR_HANDLE);
    saved_state.initialStdInHandle = GetStdHandle(STD_INPUT_HANDLE);
    saved_state.initialConsoleCP = GetConsoleOutputCP();
    saved_state.initialStdOutFD = _dup(_fileno(stdout));
    saved_state.initialStdErrFD = _dup(_fileno(stderr));
    saved_state.initialStdInFD = _dup(_fileno(stdin));
}

void restore_initial_state() {
    SetLastError(0);
    srand(123);
    SetConsoleOutputCP(saved_state.initialConsoleCP);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    // Drain message queue
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return; // TODO

    // Restore CRT streams
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

    // Restore Win32 handles
    SetStdHandle(STD_OUTPUT_HANDLE, saved_state.initialStdOutHandle);
    SetStdHandle(STD_ERROR_HANDLE, saved_state.initialStdErrHandle);
    SetStdHandle(STD_INPUT_HANDLE, saved_state.initialStdInHandle);
}

// =================================================================================================
// Test Cases
// =================================================================================================

LRESULT CALLBACK TestWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void test_user32_RegisterClassEx() {
    struct SubTest subtests[10];
    int numSubtests = 0;
    DWORD err = 0;

    HINSTANCE hinst = GetModuleHandle(NULL);
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = TestWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hinst;
    wcex.hIcon = NULL;
    wcex.hCursor = NULL;
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"MyTestClass";
    wcex.hIconSm = NULL;

    // Subtest 1: Successful registration
    SetLastError(0);
    ATOM atom = RegisterClassExW(&wcex);
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Registration: Valid Class ATOM", is_valid_class_atom(atom, hinst), "Invalid class ATOM returned");
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Registration: ERROR_SUCCESS", err == ERROR_SUCCESS, "Unexpected error code after success");
    SetLastError(0);
    BOOL unreg = (atom != 0) ? UnregisterClassW(MAKEINTATOM(atom), hinst) : FALSE;
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Registration: Unregister succeeds", unreg != FALSE && err == ERROR_SUCCESS, "Unregister failed");

    // Subtest 2: NULL parameter (expect crash due to invalid pointer deref)
    EXPECT_CRASH(subtests, &numSubtests, "NULL Parameter: Access Violation", RegisterClassExW(NULL); );

    // Subtest 3: Invalid cbSize (expect ERROR_INVALID_PARAMETER)
    wcex.cbSize = sizeof(WNDCLASSEXW) - 1;  // Too small
    SetLastError(0);
    atom = RegisterClassExW(&wcex);
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Invalid cbSize: Fails with ERROR_INVALID_PARAMETER", atom == 0 && err == ERROR_INVALID_PARAMETER, "Did not fail with expected error");

    bool overallPassed = true;
    for (int i = 0; i < numSubtests; i++) {
        if (!subtests[i].passed) {
            overallPassed = false;
            break;
        }
    }
    if (!overallPassed) {
        g_Failed++;
        strncpy_s(g_failed_test_names[g_num_failed_tests], 128, g_currentTestName, _TRUNCATE);
        g_num_failed_tests++;
    }
    output_test_json(g_currentTestName, overallPassed, subtests, numSubtests);
}

void test_user32_CreateWindowEx() {
    struct SubTest subtests[10];
    int numSubtests = 0;
    DWORD err = 0;

    const WCHAR* szClassName = L"MyWindowCreationTestClass";
    HINSTANCE hinst = GetModuleHandle(NULL);

    // Setup: Register class
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.lpfnWndProc = TestWndProc;
    wcex.hInstance = hinst;
    wcex.lpszClassName = szClassName;
    SetLastError(0);
    ATOM setupAtom = RegisterClassExW(&wcex);
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Setup: Class Registration", setupAtom != 0 && err == ERROR_SUCCESS, "Setup registration failed");

    // Subtest: Successful creation
    SetLastError(0);
    HWND hwnd = CreateWindowExW(
        0, szClassName, L"Test Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hinst, NULL
    );
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Creation: Valid HWND", is_valid_hwnd(hwnd), "Invalid HWND returned");
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Creation: ERROR_SUCCESS", err == ERROR_SUCCESS || err == 0, "Unexpected error code after success");
    SetLastError(0);
    BOOL destroy = (hwnd != NULL) ? DestroyWindow(hwnd) : FALSE;
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Valid Creation: Destroy succeeds", destroy != FALSE && err == ERROR_SUCCESS, "DestroyWindow failed");

    // Subtest: Non-Existent Class
    SetLastError(0);
    HWND hwndInvalid = CreateWindowExW(0, L"ThisClassDoesNotExist123", L"Invalid", 0, 0, 0, 0, 0, NULL, NULL, hinst, NULL);
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "Non-Existent Class: Invalid HWND", !is_valid_hwnd(hwndInvalid), "Valid HWND for invalid class");
    ASSERT_SUBTEST(subtests, &numSubtests, "Non-Existent Class: ERROR_CLASS_DOES_NOT_EXIST", err == ERROR_CLASS_DOES_NOT_EXIST, "Unexpected error code");

    // Subtest: NULL hInstance
    SetLastError(0);
    hwnd = CreateWindowExW(0, szClassName, L"Test", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    err = GetLastError();
    ASSERT_SUBTEST(subtests, &numSubtests, "NULL hInstance: Fails with ERROR_INVALID_PARAMETER", !is_valid_hwnd(hwnd) && err == ERROR_INVALID_PARAMETER, "Did not fail with expected error");

    // Teardown: Unregister
    UnregisterClassW(szClassName, hinst);

    bool overallPassed = true;
    for (int i = 0; i < numSubtests; i++) {
        if (!subtests[i].passed) {
            overallPassed = false;
            break;
        }
    }
    if (!overallPassed) {
        g_Failed++;
        strncpy_s(g_failed_test_names[g_num_failed_tests], 128, g_currentTestName, _TRUNCATE);
        g_num_failed_tests++;
    }
    output_test_json(g_currentTestName, overallPassed, subtests, numSubtests);
}

// =================================================================================================
// Test Runner Infrastructure
// =================================================================================================

struct TestEntry {
    const char* testName;
    void (*testFunction)();
};

TestEntry g_tests[] = {
    { "test_user32_RegisterClassEx", test_user32_RegisterClassEx },
    { "test_user32_CreateWindowEx",  test_user32_CreateWindowEx },
};

void print_available_tests() {
    printf("Available tests:\n");
    int testCount = sizeof(g_tests) / sizeof(TestEntry);
    for (int i = 0; i < testCount; ++i) {
        printf("  %s\n", g_tests[i].testName);
    }
    printf("\nRun all tests with: .\\win32_test_framework.exe all\n");
    printf("Run specific tests with: .\\win32_test_framework.exe test_name_1 test_name_2 ...\n");
    printf("Output is JSON; redirect with > test.json for parsing.\n");
}

void run_test(const TestEntry* test) {
    fflush(stdout);
    restore_initial_state();
    g_currentTestName = test->testName;

    __try {
        test->testFunction();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // If test crashes outside subtests, output a failure JSON
        struct SubTest subtests[1];
        int numSubtests = 0;
        add_subtest(subtests, &numSubtests, "Overall Execution", false, "Test crashed unexpectedly", __FILE__, __LINE__);
        output_test_json(g_currentTestName, false, subtests, numSubtests);
        g_Failed++;
        strncpy_s(g_failed_test_names[g_num_failed_tests], 128, g_currentTestName, _TRUNCATE);
        g_num_failed_tests++;
        return;
    }

    // Note: Individual tests output their own JSON via the function.
    // Summary handled in main.
}

// =================================================================================================
// Main Entry Point
// =================================================================================================

int wmain(int argc, wchar_t* argv[]) {
    save_initial_state();

    if (argc < 2) {
        print_available_tests();
        return 0;
    }

    // Begin of JSON
    printf("{\n");
    printf("  \"tests\": [");

    int testCount = sizeof(g_tests) / sizeof(TestEntry);
    g_Failed = 0;
    g_num_failed_tests = 0;
    int total = 0;

    bool runAll = (argc == 2 && wcscmp(argv[1], L"all") == 0);
    if (runAll) {
        total = testCount;
        for (int i = 0; i < testCount; ++i) {
            run_test(&g_tests[i]);
            if (i < testCount - 1) printf(", ");
        }
    } else {
        total = argc - 1;
        for (int i = 1; i < argc; ++i) {
            char testNameArg[128];
            size_t convertedChars = 0;
            wcstombs_s(&convertedChars, testNameArg, 128, argv[i], _TRUNCATE);

            bool foundTest = false;
            for (int j = 0; j < testCount; ++j) {
                if (strcmp(g_tests[j].testName, testNameArg) == 0) {
                    run_test(&g_tests[j]);
                    foundTest = true;
                    break;
                }
            }
            if (!foundTest) {
                // Output JSON for unknown test
                struct SubTest subtests[1];
                int numSubtests = 0;
                add_subtest(subtests, &numSubtests, "Test Discovery", false, "Test not found", __FILE__, __LINE__);
                output_test_json(testNameArg, false, subtests, numSubtests);
                g_Failed++;
                strncpy_s(g_failed_test_names[g_num_failed_tests], 128, testNameArg, _TRUNCATE);
                g_num_failed_tests++;
            }
        }
    }

    // Final summary JSON
    printf("],\n");
    printf("  \"summary\": {\n");
    printf("    \"total\": %d,\n", total);
    printf("    \"passed\": %d,\n", total - g_Failed);
    printf("    \"failed\": %d,\n", g_Failed);
    printf("    \"failed_tests\": [\n");
    for (int i = 0; i < g_num_failed_tests; i++) {
        printf("      \"%s\"%s\n", g_failed_test_names[i], (i < g_num_failed_tests - 1) ? "," : "");
    }
    printf("    ]\n");
    printf("  }\n");
    printf("}\n");

    return g_Failed > 0 ? 1 : 0;  // Exit code for CI
}
