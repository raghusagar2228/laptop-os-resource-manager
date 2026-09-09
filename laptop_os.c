/*
    LAPTOP OS RESOURCE MANAGER - REAL WINDOWS VERSION
    -------------------------------------------------
    Build with MinGW/GCC:
    gcc -Wall -O2 -o laptop_os.exe laptop_os.c -lwininet -lpsapi -lsetupapi -lcfgmgr32 -lwinmm

    This version performs real Windows operations where Windows permits them:
      - Real battery / AC state
      - Real CPU / RAM / C: drive information
      - Real Windows process creation; process termination is simulated
      - Real memory allocation using VirtualAlloc / VirtualFree
      - Real file creation / deletion
      - Real device enumeration
      - Real Windows power-plan changes
      - Real Sleep / Hibernate / Shutdown

    Important:
      FCFS is an OS scheduling algorithm; Windows does not expose a way for a
      normal user program to replace the Windows kernel scheduler with FCFS.
      Therefore the FCFS menu calculates FCFS for the REAL processes selected
      by the user, while Windows continues to perform the actual scheduling.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wininet.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <mmsystem.h>
#include <limits.h>

#define MAX_PROCESSES 10
#define MAX_FILES 10
#define MAX_ALLOCATIONS 10
#define PROCESS_NAME_LEN 260
#define FILE_NAME_LEN 260

typedef struct {
    DWORD pid;
    char name[PROCESS_NAME_LEN];
    DWORD burstTime;
    SIZE_T requestedMemoryMB;
    int status;                 /* 1 = running, 0 = terminated */
    HANDLE handle;              /* handle for processes launched by this program */
} ProcessRecord;

typedef struct {
    char name[FILE_NAME_LEN];
    unsigned long long sizeGB;
    int exists;
} FileRecord;

typedef struct {
    LPVOID address;
    SIZE_T sizeMB;
    int active;
} MemoryBlock;

static ProcessRecord processes[MAX_PROCESSES];
static int processCount = 0;

static FileRecord files[MAX_FILES];
static int fileCount = 0;

static MemoryBlock allocations[MAX_ALLOCATIONS];
static char powerMode[40] = "Balanced";

/* -------------------- INPUT HELPERS -------------------- */

static void clearInputLine(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

/* -------------------- BATTERY -------------------- */

static void getBatteryInfo(void)
{
    SYSTEM_POWER_STATUS s;

    if (!GetSystemPowerStatus(&s)) {
        printf("Battery       : Unavailable\n");
        return;
    }

    if (s.BatteryLifePercent <= 100)
        printf("Battery       : %u%%\n", (unsigned)s.BatteryLifePercent);
    else
        printf("Battery       : Unknown\n");

    if (s.ACLineStatus == 1)
        printf("Power Status  : Plugged In / AC Power\n");
    else if (s.ACLineStatus == 0)
        printf("Power Status  : Running on Battery\n");
    else
        printf("Power Status  : Unknown\n");

    if (s.BatteryFlag & 8)
        printf("Charging      : Yes\n");
    else
        printf("Charging      : No\n");
}

/* -------------------- CPU -------------------- */

static unsigned long long fileTimeToULL(FILETIME ft)
{
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

static double getCPUUsage(void)
{
    FILETIME idle1, kernel1, user1;
    FILETIME idle2, kernel2, user2;

    if (!GetSystemTimes(&idle1, &kernel1, &user1))
        return -1.0;

    Sleep(500);

    if (!GetSystemTimes(&idle2, &kernel2, &user2))
        return -1.0;

    unsigned long long i1 = fileTimeToULL(idle1);
    unsigned long long k1 = fileTimeToULL(kernel1);
    unsigned long long u1 = fileTimeToULL(user1);
    unsigned long long i2 = fileTimeToULL(idle2);
    unsigned long long k2 = fileTimeToULL(kernel2);
    unsigned long long u2 = fileTimeToULL(user2);

    unsigned long long idle = i2 - i1;
    unsigned long long total = (k2 - k1) + (u2 - u1);

    if (total == 0)
        return 0.0;

    if (idle > total)
        idle = total;

    return ((double)(total - idle) / (double)total) * 100.0;
}

/* -------------------- REAL RAM -------------------- */

static void getRAMInfo(void)
{
    MEMORYSTATUSEX m;
    m.dwLength = sizeof(m);

    if (!GlobalMemoryStatusEx(&m)) {
        printf("RAM Usage     : Unavailable\n");
        return;
    }

    double total = (double)m.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double available = (double)m.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
    double used = total - available;

    printf("RAM Usage     : %.2f / %.2f GB (%.1f%%)\n",
           used, total, (used / total) * 100.0);
}

/* -------------------- REAL STORAGE -------------------- */

static void getStorageInfo(void)
{
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;

    if (!GetDiskFreeSpaceExA("C:\\",
                             &freeBytes,
                             &totalBytes,
                             &totalFreeBytes)) {
        printf("Disk Usage    : Unavailable\n");
        return;
    }

    double total = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
    double free = (double)freeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
    double used = total - free;

    printf("Disk Usage    : %.2f / %.2f GB (%.1f%%)\n",
           used, total, (used / total) * 100.0);
    printf("Free Space    : %.2f GB\n", free);
}

/* -------------------- NETWORK -------------------- */

static void getNetworkStatus(void)
{
    DWORD flags = 0;

    if (InternetGetConnectedState(&flags, 0))
        printf("Network       : Connected\n");
    else
        printf("Network       : Disconnected\n");
}

/* -------------------- PROCESS HELPERS -------------------- */

static void cleanupProcessHandles(void)
{
    int i;
    for (i = 0; i < processCount; ++i) {
        if (processes[i].handle) {
            CloseHandle(processes[i].handle);
            processes[i].handle = NULL;
        }
    }
}

static void addLaunchedProcess(DWORD pid, const char *name,
                               DWORD burst, SIZE_T memoryMB,
                               HANDLE handle)
{
    if (processCount >= MAX_PROCESSES) {
        printf("Internal process record limit reached.\n");
        if (handle) CloseHandle(handle);
        return;
    }

    processes[processCount].pid = pid;
    strncpy(processes[processCount].name, name, PROCESS_NAME_LEN - 1);
    processes[processCount].name[PROCESS_NAME_LEN - 1] = '\0';
    processes[processCount].burstTime = burst;
    processes[processCount].requestedMemoryMB = memoryMB;
    processes[processCount].status = 1;
    processes[processCount].handle = handle;
    processCount++;
}

/* -------------------- REAL PROCESS CREATION -------------------- */

static void createProcess(void)
{
    char command[PROCESS_NAME_LEN];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD burst;
    SIZE_T memoryMB;

    if (processCount >= MAX_PROCESSES) {
        printf("\nProcess record limit reached (%d).\n", MAX_PROCESSES);
        return;
    }

    clearInputLine();

    printf("\nEnter application/command to launch\n");
    printf("Example: notepad.exe\n");
    printf("Example: calc.exe\n");
    printf("Command: ");
    if (!fgets(command, sizeof(command), stdin))
        return;

    command[strcspn(command, "\r\n")] = '\0';

    if (command[0] == '\0') {
        printf("Invalid command.\n");
        return;
    }

    printf("Enter CPU burst time for FCFS (ms): ");
    if (scanf("%lu", &burst) != 1) {
        clearInputLine();
        printf("Invalid burst time.\n");
        return;
    }

    printf("Enter memory required for OS-model record (MB): ");
    if (scanf("%llu", (unsigned long long *)&memoryMB) != 1) {
        clearInputLine();
        printf("Invalid memory value.\n");
        return;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    /*
       CreateProcess may modify its command-line buffer, so command must be
       writable. The program launches the actual Windows process.
    */
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE,
                        0, NULL, NULL, &si, &pi)) {
        printf("\nFailed to create Windows process.\n");
        printf("Windows error: %lu\n", GetLastError());
        return;
    }

    CloseHandle(pi.hThread);

    addLaunchedProcess(pi.dwProcessId, command, burst, memoryMB, pi.hProcess);

    printf("\nREAL Windows process created successfully.\n");
    printf("PID: %lu\n", (unsigned long)pi.dwProcessId);
    printf("Use Task Manager to verify it.\n");
}

/* -------------------- REAL PROCESS TERMINATION -------------------- */

static void terminateProcess(void)
{
    DWORD pid;
    int i;

    printf("\nEnter PID to terminate (SIMULATION ONLY): ");
    if (scanf("%lu", &pid) != 1) {
        clearInputLine();
        printf("Invalid PID.\n");
        return;
    }

    /*
       SIMULATION ONLY:
       This function does NOT call OpenProcess(), TerminateProcess(),
       or any other Windows API that kills a real process.
       It only changes the status in this program's process table.
    */
    for (i = 0; i < processCount; ++i) {
        if (processes[i].pid == pid &&
            processes[i].status == 1) {

            processes[i].status = 0;

            printf("\nProcess %lu terminated successfully.\n",
                   (unsigned long)pid);
            printf("(SIMULATED - no real Windows process was terminated.)\n");
            return;
        }
    }

    printf("\nProcess not found in the program's simulated process table.\n");
}

/* -------------------- REAL PROCESS DISPLAY -------------------- */

static void displayProcesses(void)
{
    HANDLE snapshot;
    PROCESSENTRY32 pe;
    int count = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        printf("Cannot enumerate Windows processes.\n");
        return;
    }

    pe.dwSize = sizeof(pe);

    printf("\n========== REAL WINDOWS PROCESSES ==========\n");
    printf("%-8s %-35s %-12s\n", "PID", "Process", "Status");
    printf("------------------------------------------------------\n");

    if (Process32First(snapshot, &pe)) {
        do {
            printf("%-8lu %-35s %s\n",
                   (unsigned long)pe.th32ProcessID,
                   pe.szExeFile,
                   "Running");
            count++;
        } while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);

    printf("\nTotal visible processes: %d\n", count);
    printf("The list above is read from Windows in real time.\n");
}

/* -------------------- PROCESS MANAGER -------------------- */

static void processManager(void)
{
    int choice;

    do {
        printf("\n\n========== PROCESS MANAGER ==========\n");
        printf("1. Create REAL Windows Process\n");
        printf("2. Terminate Process (SIMULATION)\n");
        printf("3. Display REAL Windows Processes\n");
        printf("4. Back\n");
        printf("\nEnter choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            choice = -1;
        }

        switch (choice) {
            case 1: createProcess(); break;
            case 2: terminateProcess(); break;
            case 3: displayProcesses(); break;
            case 4: break;
            default: printf("\nInvalid choice!\n");
        }
    } while (choice != 4);
}

/* -------------------- FCFS -------------------- */

static void fcfsScheduling(void)
{
    int i;
    unsigned long long waiting = 0;
    int found = 0;

    printf("\n========== FCFS CPU SCHEDULING ==========\n");
    printf("\nFCFS is calculated using the burst times assigned to processes\n");
    printf("created through this program.\n\n");

    printf("Execution Order:\n");

    for (i = 0; i < processCount; ++i) {
        if (processes[i].status == 1) {
            if (found) printf(" -> ");
            printf("PID %lu", (unsigned long)processes[i].pid);
            found = 1;
        }
    }

    if (!found) {
        printf("No recorded running processes.\n");
        return;
    }

    printf("\n\nPID\tBurst(ms)\tWaiting(ms)\tTurnaround(ms)\n");

    for (i = 0; i < processCount; ++i) {
        if (processes[i].status == 1) {
            unsigned long long turnaround =
                waiting + processes[i].burstTime;

            printf("%lu\t%lu\t\t%llu\t\t%llu\n",
                   (unsigned long)processes[i].pid,
                   (unsigned long)processes[i].burstTime,
                   waiting,
                   turnaround);

            waiting += processes[i].burstTime;
        }
    }

    printf("\nNOTE: Windows' kernel scheduler remains in control of actual CPU\n");
    printf("execution. A normal user program cannot replace Windows scheduling\n");
    printf("with FCFS without writing a kernel scheduler/driver.\n");
}

static void cpuScheduler(void)
{
    int choice;

    do {
        printf("\n\n========== CPU SCHEDULING ==========\n");
        printf("1. FCFS Scheduling on Real Launched Processes\n");
        printf("2. Back\n");
        printf("\nEnter choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            choice = -1;
        }

        switch (choice) {
            case 1: fcfsScheduling(); break;
            case 2: break;
            default: printf("\nInvalid choice!\n");
        }
    } while (choice != 2);
}

/* -------------------- REAL MEMORY MANAGER -------------------- */

static void memoryStatus(void)
{
    MEMORYSTATUSEX m;
    m.dwLength = sizeof(m);

    if (!GlobalMemoryStatusEx(&m)) {
        printf("Unable to read RAM status.\n");
        return;
    }

    printf("\nActual Windows RAM:\n");
    printf("Total : %.2f GB\n",
           (double)m.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
    printf("Free  : %.2f GB\n",
           (double)m.ullAvailPhys / (1024.0 * 1024.0 * 1024.0));
    printf("Used  : %.2f GB\n",
           (double)(m.ullTotalPhys - m.ullAvailPhys) /
           (1024.0 * 1024.0 * 1024.0));
}

static void allocateMemory(void)
{
    unsigned long long mb;
    int i;

    printf("\nEnter REAL memory to allocate (MB): ");
    if (scanf("%llu", &mb) != 1 || mb == 0) {
        clearInputLine();
        printf("Invalid amount.\n");
        return;
    }

    for (i = 0; i < MAX_ALLOCATIONS; ++i) {
        if (!allocations[i].active)
            break;
    }

    if (i == MAX_ALLOCATIONS) {
        printf("Allocation record limit reached.\n");
        return;
    }

    if (mb > ((SIZE_T)-1) / (1024ULL * 1024ULL)) {
        printf("Amount is too large.\n");
        return;
    }

    SIZE_T bytes = (SIZE_T)(mb * 1024ULL * 1024ULL);

    LPVOID p = VirtualAlloc(NULL, bytes,
                            MEM_RESERVE | MEM_COMMIT,
                            PAGE_READWRITE);

    if (!p) {
        printf("\nREAL Windows memory allocation failed.\n");
        printf("Windows error: %lu\n", GetLastError());
        return;
    }

    allocations[i].address = p;
    allocations[i].sizeMB = (SIZE_T)mb;
    allocations[i].active = 1;

    /*
       Touch one byte per page so Windows actually backs the committed pages
       with physical memory as far as the system permits.
    */
    volatile unsigned char *memory = (volatile unsigned char *)p;
    SIZE_T page = 4096;
    SIZE_T offset;

    for (offset = 0; offset < bytes; offset += page)
        memory[offset] = 0;

    printf("\nREAL memory allocated successfully.\n");
    printf("Address: %p\n", p);
    printf("Size   : %llu MB\n", mb);
}

static void releaseMemory(void)
{
    int i;
    unsigned long long index;

    printf("\nActive allocations:\n");
    for (i = 0; i < MAX_ALLOCATIONS; ++i) {
        if (allocations[i].active) {
            printf("%d. Address=%p  Size=%llu MB\n",
                   i + 1,
                   allocations[i].address,
                   (unsigned long long)allocations[i].sizeMB);
        }
    }

    printf("\nEnter allocation number to release: ");
    if (scanf("%llu", &index) != 1 ||
        index < 1 || index > MAX_ALLOCATIONS) {
        clearInputLine();
        printf("Invalid allocation number.\n");
        return;
    }

    i = (int)index - 1;

    if (!allocations[i].active) {
        printf("That allocation is not active.\n");
        return;
    }

    if (VirtualFree(allocations[i].address, 0, MEM_RELEASE)) {
        printf("\nREAL Windows memory released successfully.\n");
        allocations[i].address = NULL;
        allocations[i].sizeMB = 0;
        allocations[i].active = 0;
    } else {
        printf("\nFailed to release memory.\n");
        printf("Windows error: %lu\n", GetLastError());
    }
}

static void memoryManager(void)
{
    int choice;

    do {
        printf("\n\n========== MEMORY MANAGER ==========\n");
        memoryStatus();

        printf("\n1. Allocate REAL Memory\n");
        printf("2. Release REAL Memory\n");
        printf("3. Back\n");
        printf("\nEnter choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            choice = -1;
        }

        switch (choice) {
            case 1: allocateMemory(); break;
            case 2: releaseMemory(); break;
            case 3: break;
            default: printf("\nInvalid choice!\n");
        }
    } while (choice != 3);
}

/* -------------------- REAL FILE MANAGEMENT -------------------- */

static int getFileSizeGB(const char *path, unsigned long long *gb)
{
    HANDLE h = CreateFileA(path, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;

    if (h == INVALID_HANDLE_VALUE)
        return 0;

    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return 0;
    }

    CloseHandle(h);

    *gb = (unsigned long long)size.QuadPart /
          (1024ULL * 1024ULL * 1024ULL);
    return 1;
}

static void createRealFile(void)
{
    char name[FILE_NAME_LEN];
    unsigned long long sizeGB;
    HANDLE h;
    LARGE_INTEGER distance;

    if (fileCount >= MAX_FILES) {
        printf("\nFile record limit reached.\n");
        return;
    }

    clearInputLine();
    printf("\nEnter file name/path (example: os_test.bin): ");
    if (!fgets(name, sizeof(name), stdin))
        return;

    name[strcspn(name, "\r\n")] = '\0';

    if (name[0] == '\0') {
        printf("Invalid file name.\n");
        return;
    }

    printf("Enter file size (GB): ");
    if (scanf("%llu", &sizeGB) != 1) {
        clearInputLine();
        printf("Invalid size.\n");
        return;
    }

    if (sizeGB == 0) {
        printf("Use a size greater than 0.\n");
        return;
    }

    printf("\nWARNING: this creates a REAL file on your disk.\n");
    printf("Continue? (1=Yes, 0=No): ");

    int confirm;
    if (scanf("%d", &confirm) != 1 || confirm != 1) {
        clearInputLine();
        printf("Cancelled.\n");
        return;
    }

    h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        printf("\nFailed to create real file.\n");
        printf("Windows error: %lu\n", GetLastError());
        return;
    }

    if (sizeGB > (unsigned long long)LLONG_MAX /
                 (1024ULL * 1024ULL * 1024ULL)) {
        CloseHandle(h);
        DeleteFileA(name);
        printf("Requested file is too large.\n");
        return;
    }

    distance.QuadPart =
        (LONGLONG)(sizeGB * 1024ULL * 1024ULL * 1024ULL);

    if (!SetFilePointerEx(h, distance, NULL, FILE_BEGIN) ||
        !SetEndOfFile(h)) {
        printf("\nFailed to set real file size.\n");
        printf("Windows error: %lu\n", GetLastError());
        CloseHandle(h);
        DeleteFileA(name);
        return;
    }

    CloseHandle(h);

    strncpy(files[fileCount].name, name, FILE_NAME_LEN - 1);
    files[fileCount].name[FILE_NAME_LEN - 1] = '\0';
    files[fileCount].sizeGB = sizeGB;
    files[fileCount].exists = 1;
    fileCount++;

    printf("\nREAL file created successfully: %s\n", name);
}

static void deleteRealFile(void)
{
    char name[FILE_NAME_LEN];

    clearInputLine();
    printf("\nEnter REAL file name/path to delete: ");
    if (!fgets(name, sizeof(name), stdin))
        return;

    name[strcspn(name, "\r\n")] = '\0';

    if (name[0] == '\0') {
        printf("Invalid file name.\n");
        return;
    }

    printf("Delete REAL file '%s'? (1=Yes, 0=No): ", name);

    int confirm;
    if (scanf("%d", &confirm) != 1 || confirm != 1) {
        clearInputLine();
        printf("Cancelled.\n");
        return;
    }

    if (DeleteFileA(name)) {
        printf("\nREAL file deleted successfully.\n");

        for (int i = 0; i < fileCount; ++i) {
            if (_stricmp(files[i].name, name) == 0) {
                for (int j = i; j < fileCount - 1; ++j)
                    files[j] = files[j + 1];
                fileCount--;
                break;
            }
        }
    } else {
        printf("\nFailed to delete file.\n");
        printf("Windows error: %lu\n", GetLastError());
    }
}

static void displayFiles(void)
{
    WIN32_FIND_DATAA data;
    HANDLE find;
    char search[FILE_NAME_LEN];
    unsigned long long sizeGB;

    printf("\n========== REAL FILES IN CURRENT FOLDER ==========\n");

    find = FindFirstFileA("*", &data);

    if (find == INVALID_HANDLE_VALUE) {
        printf("Cannot read current folder.\n");
        return;
    }

    int n = 0;

    do {
        if (strcmp(data.cFileName, ".") != 0 &&
            strcmp(data.cFileName, "..") != 0 &&
            !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {

            snprintf(search, sizeof(search), "%s", data.cFileName);

            if (getFileSizeGB(search, &sizeGB)) {
                printf("%-45s %llu GB\n", search, sizeGB);
                n++;
            }
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);

    if (n == 0)
        printf("No files found.\n");

    printf("\nThese entries are read directly from the Windows file system.\n");
}

static void storageManager(void)
{
    int choice;

    do {
        printf("\n\n========== STORAGE MANAGER ==========\n");
        printf("\nActual C: Drive Storage\n");
        getStorageInfo();

        printf("\n1. Create REAL File\n");
        printf("2. Delete REAL File\n");
        printf("3. Display REAL Files\n");
        printf("4. Back\n");
        printf("\nEnter choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            choice = -1;
        }

        switch (choice) {
            case 1: createRealFile(); break;
            case 2: deleteRealFile(); break;
            case 3: displayFiles(); break;
            case 4: break;
            default: printf("\nInvalid choice!\n");
        }
    } while (choice != 4);
}

/* -------------------- REAL DEVICE MANAGER -------------------- */

static int countPresentDevices(void)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD index = 0;
    int count = 0;

    set = SetupDiGetClassDevsA(NULL, NULL, NULL,
                               DIGCF_ALLCLASSES | DIGCF_PRESENT);

    if (set == INVALID_HANDLE_VALUE)
        return -1;

    data.cbSize = sizeof(data);

    while (SetupDiEnumDeviceInfo(set, index, &data)) {
        count++;
        index++;
    }

    SetupDiDestroyDeviceInfoList(set);
    return count;
}

static int countPresentUSBDevices(void)
{
    HDEVINFO set;
    SP_DEVINFO_DATA data;
    DWORD index = 0;
    int count = 0;

    set = SetupDiGetClassDevsA(NULL, "USB", NULL,
                               DIGCF_ALLCLASSES | DIGCF_PRESENT);

    if (set == INVALID_HANDLE_VALUE)
        return -1;

    data.cbSize = sizeof(data);

    while (SetupDiEnumDeviceInfo(set, index, &data)) {
        count++;
        index++;
    }

    SetupDiDestroyDeviceInfoList(set);
    return count;
}

static void deviceManager(void)
{
    int totalDevices = countPresentDevices();
    int usbDevices = countPresentUSBDevices();
    UINT audioDevices = waveOutGetNumDevs();

    printf("\n\n========== REAL DEVICE MANAGEMENT ==========\n");

    printf("\nKeyboard : ");
    if (GetKeyboardType(0) != 0)
        printf("Detected\n");
    else
        printf("Not detected\n");

    printf("Display  : %s\n",
           GetSystemMetrics(SM_CMONITORS) > 0 ? "Detected" : "Not detected");

    printf("Mouse/Touchpad interface : %s\n",
           GetSystemMetrics(SM_MOUSEPRESENT) ? "Detected" : "Not detected");

    printf("Audio Output Devices : %u\n", (unsigned)audioDevices);

    if (usbDevices >= 0)
        printf("USB Devices Present : %d\n", usbDevices);
    else
        printf("USB Devices Present : Unavailable\n");

    if (totalDevices >= 0)
        printf("Present PnP Devices  : %d\n", totalDevices);
    else
        printf("Present PnP Devices  : Unavailable\n");

    getNetworkStatus();

    printf("\nDevice information is obtained from Windows APIs.\n");
}

/* -------------------- REAL POWER PLAN -------------------- */

static int runCommand(const char *command)
{
    int result = system(command);
    return result == 0;
}

static void setPowerPlan(const char *scheme, const char *displayName)
{
    char command[256];

    snprintf(command, sizeof(command),
             "powercfg /setactive %s", scheme);

    if (runCommand(command)) {
        strncpy(powerMode, displayName, sizeof(powerMode) - 1);
        powerMode[sizeof(powerMode) - 1] = '\0';
        printf("\nREAL Windows power plan changed to: %s\n", displayName);
    } else {
        printf("\nFailed to change Windows power plan.\n");
        printf("Try running the program as Administrator.\n");
    }
}

static void realSleep(void)
{
    printf("\nPutting laptop into REAL Sleep mode...\n");
    printf("The program will stop here while Windows sleeps.\n");

    /*
       This is the same Windows command that can be tested in Command Prompt.
    */
    if (!runCommand("rundll32.exe powrprof.dll,SetSuspendState 0,1,0")) {
        printf("Failed to request Sleep mode.\n");
        printf("Windows command returned an error.\n");
    }
}

static void realHibernate(void)
{
    printf("\nEnabling Windows hibernation...\n");

    if (!runCommand("powercfg /hibernate on")) {
        printf("Could not enable hibernation.\n");
        return;
    }

    printf("Putting laptop into REAL Hibernate mode...\n");

    if (!runCommand("shutdown /h")) {
        printf("Failed to request Hibernate mode.\n");
    }
}

static void realShutdown(void)
{
    printf("\nREAL Windows shutdown requested.\n");
    printf("Windows will shut down immediately.\n");

    if (!runCommand("shutdown /s /t 0")) {
        printf("Failed to request shutdown.\n");
    }
}

static void powerManager(void)
{
    int choice;

    do {
        printf("\n\n========== POWER MANAGEMENT ==========\n");
        getBatteryInfo();
        printf("Power Mode    : %s\n", powerMode);

        printf("\n1. Battery Saver (Power Saver plan)\n");
        printf("2. Balanced\n");
        printf("3. Performance (High Performance plan)\n");
        printf("4. Sleep\n");
        printf("5. Hibernate\n");
        printf("6. Back\n");
        printf("\nEnter choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            choice = -1;
        }

        switch (choice) {
            case 1:
                /* SCHEME_MAX is normally High Performance;
                   SCHEME_MIN is Power Saver. */
                setPowerPlan("SCHEME_MIN", "Battery Saver");
                break;

            case 2:
                setPowerPlan("SCHEME_BALANCED", "Balanced");
                break;

            case 3:
                setPowerPlan("SCHEME_MAX", "Performance");
                break;

            case 4:
                realSleep();
                break;

            case 5:
                realHibernate();
                break;

            case 6:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 6);
}

/* -------------------- LIVE SYSTEM STATUS -------------------- */

static int getRunningProcessCount(void)
{
    HANDLE snapshot;
    PROCESSENTRY32 pe;
    int count = 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return -1;

    pe.dwSize = sizeof(pe);

    if (Process32First(snapshot, &pe)) {
        do {
            count++;
        } while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return count;
}

static void systemStatus(void)
{
    double cpu = getCPUUsage();
    int processCountNow = getRunningProcessCount();

    printf("\n\n========================================\n");
    printf("       LIVE WINDOWS SYSTEM STATUS\n");
    printf("========================================\n\n");

    if (cpu >= 0)
        printf("CPU Usage     : %.1f%%\n", cpu);
    else
        printf("CPU Usage     : Unavailable\n");

    getRAMInfo();
    getStorageInfo();

    if (processCountNow >= 0)
        printf("Processes     : %d REAL Windows processes\n",
               processCountNow);
    else
        printf("Processes     : Unavailable\n");

    getBatteryInfo();
    getNetworkStatus();

    printf("Power Mode    : %s\n", powerMode);

    if (cpu >= 90.0)
        printf("System Status : HIGH CPU USAGE\n");
    else
        printf("System Status : NORMAL\n");
}

/* -------------------- CLEANUP -------------------- */

static void releaseAllMemory(void)
{
    int i;

    for (i = 0; i < MAX_ALLOCATIONS; ++i) {
        if (allocations[i].active) {
            VirtualFree(allocations[i].address, 0, MEM_RELEASE);
            allocations[i].address = NULL;
            allocations[i].sizeMB = 0;
            allocations[i].active = 0;
        }
    }
}

/* -------------------- MAIN -------------------- */

int main(void)
{
    int choice;

    printf("========================================\n");
    printf("     LAPTOP OS RESOURCE MANAGER\n");
    printf("        REAL WINDOWS VERSION\n");
    printf("========================================\n");

    while (1) {
        printf("\n\n========== MAIN MENU ==========\n");
        printf("1. Process Management\n");
        printf("2. CPU Scheduling\n");
        printf("3. Memory Management\n");
        printf("4. Storage Management\n");
        printf("5. Device Management\n");
        printf("6. Power Management\n");
        printf("7. System Status\n");
        printf("8. Shutdown\n");

        printf("\nEnter your choice: ");

        if (scanf("%d", &choice) != 1) {
            clearInputLine();
            printf("\nInvalid input!\n");
            continue;
        }

        switch (choice) {
            case 1:
                processManager();
                break;

            case 2:
                cpuScheduler();
                break;

            case 3:
                memoryManager();
                break;

            case 4:
                storageManager();
                break;

            case 5:
                deviceManager();
                break;

            case 6:
                powerManager();
                break;

            case 7:
                systemStatus();
                break;

            case 8:
                printf("\n1. Exit program only\n");
                printf("2. Shut down Windows NOW\n");
                printf("Enter choice: ");

                int shutdownChoice;
                if (scanf("%d", &shutdownChoice) != 1) {
                    clearInputLine();
                    printf("Invalid choice.\n");
                    break;
                }

                if (shutdownChoice == 1) {
                    releaseAllMemory();
                    cleanupProcessHandles();
                    printf("\nExiting Laptop OS Resource Manager.\n");
                    return 0;
                } else if (shutdownChoice == 2) {
                    releaseAllMemory();
                    cleanupProcessHandles();
                    realShutdown();
                    return 0;
                } else {
                    printf("Invalid choice.\n");
                }
                break;

            default:
                printf("\nInvalid choice! Please try again.\n");
        }
    }

    return 0;
}
