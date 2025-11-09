// =================================================================================================
//
// Enhanced Single-File Win32 API Testing Framework
//
// - Style: C with Classes
// - Target: 32-bit (x86) and 64-bit (x86_64) Windows
// - Environment: Supposed to be runnable under Windows as-is, must not have third-party quirks
// - Compilers: Clang
// - Purpose: Provides a simple framework to test Win32 API functions with JSON output for
//   automated conformance testing (e.g., matching Windows behavior in WineHQ/ReactOS/GreenteaOS).
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

/**
 * @brief Global array and counter for subtests (reset per test).
 */
static struct SubTest g_subtests[4096];  // Fixed size; expandable if needed
static int g_numSubtests = 0;
static bool g_testPassed = true;

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
    constexpr size_t ARRAY_SIZE = sizeof(g_subtests) / sizeof(g_subtests[0]);  // Compile-time computation
    if (*numSubtests >= (int)ARRAY_SIZE) return;  // Align with global array size
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
        if (strlen(temp) >= sizeof(subtests[*numSubtests].reason) - 12) {  // Reserve space for suffix
            strcat_s(temp, sizeof(temp), " (truncated)");
        }
        strncpy_s(subtests[*numSubtests].reason, sizeof(subtests[*numSubtests].reason), temp, _TRUNCATE);
    }
    (*numSubtests)++;
}

/**
 * @brief Asserts a boolean condition for a subtest.
 */
#define ASSERT_SUBTEST(name, condition, failMsg) \
    do { \
        bool __cond = !!(condition); \
        add_subtest(g_subtests, &g_numSubtests, name, __cond, __cond ? "" : failMsg, __FILE__, __LINE__); \
        if (!__cond) g_testPassed = false; \
    } while(0)

/**
 * @brief Converts a Win32 error code to its symbolic name using a switch.
 * @param code The DWORD error code.
 * @return The symbolic string (e.g., "ERROR_INVALID_PARAMETER") or fallback for unknowns.
 */
const char* GetErrorCodeName(DWORD code) {
    static char unknown_buf[64];
    switch (code) {
        #define CASE_ERROR_CODE(err) case err: return #err;
        CASE_ERROR_CODE(ERROR_SUCCESS)
        CASE_ERROR_CODE(ERROR_FILE_NOT_FOUND)
        CASE_ERROR_CODE(ERROR_PATH_NOT_FOUND)
        CASE_ERROR_CODE(ERROR_ACCESS_DENIED)
        CASE_ERROR_CODE(ERROR_INVALID_HANDLE)
        CASE_ERROR_CODE(ERROR_NOT_ENOUGH_MEMORY)
        CASE_ERROR_CODE(ERROR_INVALID_PARAMETER)
        CASE_ERROR_CODE(ERROR_CANNOT_FIND_WND_CLASS)
        CASE_ERROR_CODE(ERROR_CLASS_DOES_NOT_EXIST)
        CASE_ERROR_CODE(ERROR_INVALID_MENU_HANDLE)
        CASE_ERROR_CODE(ERROR_TLW_WITH_WSCHILD)
        // Extend with additional cases as tests evolve
        default:
            snprintf(unknown_buf, sizeof(unknown_buf), "UNKNOWN_ERROR_%lu", (unsigned long)code);
            return unknown_buf;
    }
}

/**
 * @brief Specialized assertion for GetLastError() comparisons.
 * @param subtests Array to append to.
 * @param numSubtests Pointer to count.
 * @param name Subtest name.
 * @param expected The expected error code (DWORD).
 * @param failMsg Base failure message (appended with actual value if failed).
 */
#define ASSERT_LAST_ERROR(name, expected, failMsg) \
    do { \
        DWORD __actual = GetLastError(); \
        bool __cond = (__actual == (expected)); \
        char __reason[512]; \
        if (__cond) { \
            __reason[0] = '\0'; \
        } else { \
            snprintf(__reason, sizeof(__reason), "%s (actual: %lu / %s, expected: %lu / %s)", failMsg, (unsigned long)__actual, GetErrorCodeName(__actual), (unsigned long)expected, GetErrorCodeName(expected)); \
        } \
        add_subtest(g_subtests, &g_numSubtests, name, __cond, __reason, __FILE__, __LINE__); \
        if (!__cond) g_testPassed = false; \
    } while(0)

/**
 * @brief Expects a crash (access violation) in the try block.
 */
#define EXPECT_CRASH(name, codeBlock) \
    do { \
        __try { \
            codeBlock; \
            add_subtest(g_subtests, &g_numSubtests, name, false, "Expected crash but did not occur", __FILE__, __LINE__); \
            g_testPassed = false; \
        } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { \
            add_subtest(g_subtests, &g_numSubtests, name, true, "", __FILE__, __LINE__); \
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
 * @return TODO True if in valid local class range (0x4000–0xBFFF, non-zero high byte).
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
    ASSERT_SUBTEST("Valid Registration: Valid Class ATOM", is_valid_class_atom(atom, hinst), "Invalid class ATOM returned");
    ASSERT_SUBTEST("Valid Registration: ERROR_SUCCESS", err == ERROR_SUCCESS, "Unexpected error code after success");
    SetLastError(0);
    BOOL unreg = (atom != 0) ? UnregisterClassW(MAKEINTATOM(atom), hinst) : FALSE;
    err = GetLastError();
    ASSERT_SUBTEST("Valid Registration: Unregister succeeds", unreg != FALSE && err == ERROR_SUCCESS, "Unregister failed");

    // Subtest 2: NULL parameter (expect crash due to invalid pointer deref)
    EXPECT_CRASH("NULL Parameter: Access Violation", RegisterClassExW(NULL); );

    // Subtest 3: Invalid cbSize (expect ERROR_INVALID_PARAMETER)
    wcex.cbSize = sizeof(WNDCLASSEXW) - 1;  // Too small
    SetLastError(0);
    atom = RegisterClassExW(&wcex);
    err = GetLastError();
    ASSERT_SUBTEST("Invalid cbSize: Fails with ERROR_INVALID_PARAMETER", atom == 0 && err == ERROR_INVALID_PARAMETER, "Did not fail with expected error");
}

void test_user32_CreateWindowEx() {
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
    ASSERT_SUBTEST("Setup: Class Registration", setupAtom != 0 && err == ERROR_SUCCESS, "Setup registration failed");

    // Subtest: Successful creation
    SetLastError(0);
    HWND hwnd = CreateWindowExW(
        0, szClassName, L"Test Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hinst, NULL
    );
    err = GetLastError();
    ASSERT_SUBTEST("Valid Creation: Valid HWND", is_valid_hwnd(hwnd), "Invalid HWND returned");
    ASSERT_SUBTEST("Valid Creation: ERROR_SUCCESS", err == ERROR_SUCCESS || err == 0, "Unexpected error code after success");
    SetLastError(0);
    BOOL destroy = (hwnd != NULL) ? DestroyWindow(hwnd) : FALSE;
    err = GetLastError();
    ASSERT_SUBTEST("Valid Creation: Destroy succeeds", destroy != FALSE && err == ERROR_SUCCESS, "DestroyWindow failed");

    // Subtest: Invalid dwStyle (isolated parameter validation)
    // Tests sequential checks: dwStyle validation occurs before class lookup; WS_CHILD requires non-NULL hWndParent.
    SetLastError(0);
    HWND hwndStyleInvalid = CreateWindowExW(
        0,                          // dwExStyle
        szClassName,                // lpClassName (registered to isolate style check)
        L"Style Invalid",
        WS_CHILD | WS_VISIBLE,      // dwStyle (invalid: child window without parent)
        0, 0,                       // x, y (relative to parent, but irrelevant due to failure)
        100, 100,                   // nWidth, nHeight (concrete)
        NULL,                       // hWndParent (NULL triggers invalid config)
        NULL, hinst, NULL
    );
    ASSERT_SUBTEST("Invalid dwStyle: Invalid HWND",
                   !is_valid_hwnd(hwndStyleInvalid), "Valid HWND for invalid style");
    ASSERT_LAST_ERROR("Invalid dwStyle: ERROR_TLW_WITH_WSCHILD",
                     ERROR_TLW_WITH_WSCHILD, "Unexpected error code");

    // Subtest: Non-Existent Class (with valid style/dims)
    SetLastError(0);
    HWND hwndInvalid = CreateWindowExW(
        0,                              // dwExStyle
        L"ThisClassDoesNotExist123",    // lpClassName (unregistered)
        L"Invalid",
        WS_OVERLAPPEDWINDOW,            // dwStyle (valid)
        0, 0,                           // x, y (concrete)
        100, 100,                       // nWidth, nHeight
        NULL, NULL, hinst, NULL
    );
    ASSERT_SUBTEST("Non-Existent Class: Invalid HWND", !is_valid_hwnd(hwndInvalid), "Valid HWND for invalid class");
    ASSERT_LAST_ERROR("Non-Existent Class: ERROR_CANNOT_FIND_WND_CLASS",
                     ERROR_CANNOT_FIND_WND_CLASS, "Unexpected error code");

    // Subtest: Invalid hInstance (module mismatch)
    ATOM classAtom = setupAtom;  // From setup
    SetLastError(0);
    HINSTANCE hInvalidInst = (HINSTANCE)0xDEADBEEF;  // Arbitrary invalid module
    hwnd = CreateWindowExW(
        0,
        (LPCWSTR)MAKEINTATOM(classAtom),  // lpClassName as atom (bypasses string/module string lookup)
        L"Test",
        WS_OVERLAPPEDWINDOW,
        0, 0,
        100, 100,
        NULL, NULL,
        hInvalidInst,  // Invalid hInstance
        NULL
    );
    ASSERT_SUBTEST("Invalid hInstance: Invalid HWND", !is_valid_hwnd(hwnd), "Valid HWND for invalid hInstance");
    ASSERT_LAST_ERROR("Invalid hInstance: ERROR_CANNOT_FIND_WND_CLASS",
                     ERROR_CANNOT_FIND_WND_CLASS, "Unexpected error code");  // Module mismatch as class not found

    // Teardown: Unregister
    UnregisterClassW(szClassName, hinst);
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
    int testCount = sizeof(g_tests) / sizeof(TestEntry);
    printf("Available tests: %d tests like", testCount);
    for (int i = 0; i < testCount; ++i) {
        printf(" %s", g_tests[i].testName);

        if (i > 10) break; // Let's not pollute the screen
    }
    printf(" etc...\nRun all tests with: .\\win32_test_framework.exe all\n");
    printf("Run specific tests with: .\\win32_test_framework.exe test_name_1 test_name_2 ...\n");
    printf("Output is JSON; redirect with > test.json for parsing.\n");
}

void run_test(const TestEntry* test) {
    fflush(stdout);
    restore_initial_state();
    g_currentTestName = test->testName;

    // Reset globals for this test
    memset(g_subtests, 0, sizeof(g_subtests));
    g_numSubtests = 0;
    g_testPassed = true;

    __try {
        test->testFunction();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        add_subtest(g_subtests, &g_numSubtests, "Overall Execution", false, "Test crashed unexpectedly", __FILE__, __LINE__);
        g_testPassed = false;
    }

    // Output JSON
    output_test_json(g_currentTestName, g_testPassed, g_subtests, g_numSubtests);

    if (!g_testPassed) {
        g_Failed++;
        strncpy_s(g_failed_test_names[g_num_failed_tests], 128, g_currentTestName, _TRUNCATE);
        g_num_failed_tests++;
    }
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

    fflush(0);

    return g_Failed > 0 ? 1 : 0;  // Exit code for CI
}
