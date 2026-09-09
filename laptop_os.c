#include <stdio.h>
#include <string.h>

#define MAX_PROCESSES 10
#define MAX_FILES 10
#define MAX_DEVICES 6
#define TOTAL_RAM 8192
#define TOTAL_STORAGE 512

// ---------------- PROCESS ----------------

struct Process {
    int pid;
    char name[30];
    int burstTime;
    int memory;
    int status;   // 1 = Running, 2 = Ready, 3 = Suspended
};

struct Process processes[MAX_PROCESSES];
int processCount = 0;
int nextPID = 101;

// ---------------- FILE ----------------

struct File {
    char name[30];
    int size;
};

struct File files[MAX_FILES];
int fileCount = 0;
int usedStorage = 0;

// ---------------- DEVICE ----------------

struct Device {
    char name[30];
    int status;   // 1 = Connected, 0 = Disconnected
};

struct Device devices[MAX_DEVICES] = {
    {"Keyboard", 1},
    {"Touchpad", 1},
    {"Display", 1},
    {"Wi-Fi", 1},
    {"Bluetooth", 0},
    {"Audio", 1}
};

// ---------------- SYSTEM ----------------

int usedRAM = 0;
int battery = 72;
char powerMode[20] = "Balanced";

// =====================================================
// PROCESS MANAGEMENT
// =====================================================

void createProcess() {

    if (processCount >= MAX_PROCESSES) {
        printf("\nProcess limit reached!\n");
        return;
    }

    struct Process p;

    printf("\nEnter application name: ");
    scanf("%s", p.name);

    printf("Enter CPU burst time: ");
    scanf("%d", &p.burstTime);

    printf("Enter memory required (MB): ");
    scanf("%d", &p.memory);

    if (usedRAM + p.memory > TOTAL_RAM) {
        printf("\nNot enough RAM available!\n");
        return;
    }

    p.pid = nextPID++;
    p.status = 2;  // Ready

    processes[processCount] = p;
    processCount++;

    usedRAM += p.memory;

    printf("\nProcess created successfully!");
    printf("\nPID: %d\n", p.pid);
}

void displayProcesses() {

    if (processCount == 0) {
        printf("\nNo processes available.\n");
        return;
    }

    printf("\n%-8s %-20s %-12s %-12s\n",
           "PID", "Application", "Burst Time", "Memory");

    printf("--------------------------------------------------\n");

    for (int i = 0; i < processCount; i++) {

        printf("%-8d %-20s %-12d %-12d MB\n",
               processes[i].pid,
               processes[i].name,
               processes[i].burstTime,
               processes[i].memory);
    }
}

void terminateProcess() {

    int pid;
    int found = 0;

    printf("\nEnter PID to terminate: ");
    scanf("%d", &pid);

    for (int i = 0; i < processCount; i++) {

        if (processes[i].pid == pid) {

            usedRAM -= processes[i].memory;

            for (int j = i; j < processCount - 1; j++) {
                processes[j] = processes[j + 1];
            }

            processCount--;
            found = 1;

            printf("\nProcess %d terminated successfully.\n", pid);
            break;
        }
    }

    if (!found)
        printf("\nProcess not found!\n");
}

void processManager() {

    int choice;

    do {

        printf("\n\n========== PROCESS MANAGER ==========\n");
        printf("1. Create Process\n");
        printf("2. Terminate Process\n");
        printf("3. Display Processes\n");
        printf("4. Back\n");

        printf("\nEnter choice: ");
        scanf("%d", &choice);

        switch (choice) {

            case 1:
                createProcess();
                break;

            case 2:
                terminateProcess();
                break;

            case 3:
                displayProcesses();
                break;

            case 4:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 4);
}

// =====================================================
// FCFS CPU SCHEDULING
// =====================================================

void fcfsScheduling() {

    if (processCount == 0) {
        printf("\nNo processes available for scheduling.\n");
        return;
    }

    int waitingTime = 0;
    int totalWaiting = 0;
    int totalTurnaround = 0;

    printf("\n\n========== FCFS CPU SCHEDULING ==========\n");

    printf("\nExecution Order:\n");

    for (int i = 0; i < processCount; i++) {

        printf("| P%d ", processes[i].pid);

        int turnaroundTime =
            waitingTime + processes[i].burstTime;

        printf("\nP%d -> Waiting Time: %d ms",
               processes[i].pid,
               waitingTime);

        printf(", Turnaround Time: %d ms\n",
               turnaroundTime);

        totalWaiting += waitingTime;
        totalTurnaround += turnaroundTime;

        waitingTime += processes[i].burstTime;
    }

    printf("\nAverage Waiting Time: %.2f ms",
           (float)totalWaiting / processCount);

    printf("\nAverage Turnaround Time: %.2f ms\n",
           (float)totalTurnaround / processCount);
}

void cpuScheduler() {

    int choice;

    do {

        printf("\n\n========== CPU SCHEDULER ==========\n");
        printf("1. FCFS Scheduling\n");
        printf("2. Back\n");

        printf("\nEnter choice: ");
        scanf("%d", &choice);

        switch (choice) {

            case 1:
                fcfsScheduling();
                break;

            case 2:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 2);
}

// =====================================================
// MEMORY MANAGEMENT
// =====================================================

void memoryManager() {

    int choice;
    int amount;

    do {

        printf("\n\n========== MEMORY MANAGER ==========\n");

        printf("Total RAM : %d MB\n", TOTAL_RAM);
        printf("Used RAM  : %d MB\n", usedRAM);
        printf("Free RAM  : %d MB\n", TOTAL_RAM - usedRAM);

        printf("\n1. Allocate Memory\n");
        printf("2. Free Memory\n");
        printf("3. Memory Status\n");
        printf("4. Back\n");

        printf("\nEnter choice: ");
        scanf("%d", &choice);

        switch (choice) {

            case 1:

                printf("\nEnter memory to allocate (MB): ");
                scanf("%d", &amount);

                if (usedRAM + amount <= TOTAL_RAM) {
                    usedRAM += amount;
                    printf("\n%d MB allocated successfully.\n", amount);
                } else {
                    printf("\nNot enough memory available!\n");
                }

                break;

            case 2:

                printf("\nEnter memory to free (MB): ");
                scanf("%d", &amount);

                if (amount <= usedRAM) {
                    usedRAM -= amount;
                    printf("\n%d MB memory released.\n", amount);
                } else {
                    printf("\nInvalid amount!\n");
                }

                break;

            case 3:

                printf("\nTotal RAM : %d MB\n", TOTAL_RAM);
                printf("Used RAM  : %d MB\n", usedRAM);
                printf("Free RAM  : %d MB\n",
                       TOTAL_RAM - usedRAM);

                break;

            case 4:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 4);
}

// =====================================================
// STORAGE MANAGEMENT
// =====================================================

void storageManager() {

    int choice;

    do {

        printf("\n\n========== STORAGE MANAGER ==========\n");

        printf("Total Storage : %d GB\n", TOTAL_STORAGE);
        printf("Used Storage  : %d GB\n", usedStorage);
        printf("Free Storage  : %d GB\n",
               TOTAL_STORAGE - usedStorage);

        printf("\n1. Create File\n");
        printf("2. Delete File\n");
        printf("3. Display Files\n");
        printf("4. Back\n");

        printf("\nEnter choice: ");
        scanf("%d", &choice);

        switch (choice) {

            case 1:

                if (fileCount >= MAX_FILES) {
                    printf("\nFile limit reached!\n");
                    break;
                }

                printf("\nEnter file name: ");
                scanf("%s", files[fileCount].name);

                printf("Enter file size (GB): ");
                scanf("%d", &files[fileCount].size);

                if (usedStorage +
                    files[fileCount].size <= TOTAL_STORAGE) {

                    usedStorage += files[fileCount].size;
                    fileCount++;

                    printf("\nFile created successfully.\n");

                } else {

                    printf("\nNot enough storage!\n");
                }

                break;

            case 2: {

                char name[30];
                int found = 0;

                printf("\nEnter file name to delete: ");
                scanf("%s", name);

                for (int i = 0; i < fileCount; i++) {

                    if (strcmp(files[i].name, name) == 0) {

                        usedStorage -= files[i].size;

                        for (int j = i;
                             j < fileCount - 1;
                             j++) {

                            files[j] = files[j + 1];
                        }

                        fileCount--;
                        found = 1;

                        printf("\nFile deleted successfully.\n");
                        break;
                    }
                }

                if (!found)
                    printf("\nFile not found!\n");

                break;
            }

            case 3:

                if (fileCount == 0) {
                    printf("\nNo files available.\n");
                    break;
                }

                printf("\n%-20s %-10s\n",
                       "File Name", "Size");

                printf("-------------------------------\n");

                for (int i = 0; i < fileCount; i++) {

                    printf("%-20s %d GB\n",
                           files[i].name,
                           files[i].size);
                }

                break;

            case 4:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 4);
}

// =====================================================
// DEVICE MANAGEMENT
// =====================================================

void deviceManager() {

    printf("\n\n========== DEVICE MANAGER ==========\n");

    printf("\n%-15s %-15s\n",
           "Device", "Status");

    printf("------------------------------\n");

    for (int i = 0; i < MAX_DEVICES; i++) {

        printf("%-15s %-15s\n",
               devices[i].name,
               devices[i].status ?
               "Connected" :
               "Disconnected");
    }
}

// =====================================================
// POWER MANAGEMENT
// =====================================================

void powerManager() {

    int choice;

    do {

        printf("\n\n========== POWER MANAGER ==========\n");

        printf("Battery      : %d%%\n", battery);
        printf("Power Mode   : %s\n", powerMode);

        printf("\n1. Battery Saver\n");
        printf("2. Balanced Mode\n");
        printf("3. Performance Mode\n");
        printf("4. Sleep\n");
        printf("5. Hibernate\n");
        printf("6. Back\n");

        printf("\nEnter choice: ");
        scanf("%d", &choice);

        switch (choice) {

            case 1:
                strcpy(powerMode, "Battery Saver");
                printf("\nBattery Saver enabled.\n");
                break;

            case 2:
                strcpy(powerMode, "Balanced");
                printf("\nBalanced Mode enabled.\n");
                break;

            case 3:
                strcpy(powerMode, "Performance");
                printf("\nPerformance Mode enabled.\n");
                break;

            case 4:
                printf("\nSystem entering Sleep Mode...\n");
                break;

            case 5:
                printf("\nSystem entering Hibernate Mode...\n");
                break;

            case 6:
                break;

            default:
                printf("\nInvalid choice!\n");
        }

    } while (choice != 6);
}

// =====================================================
// SYSTEM STATUS
// =====================================================

void systemStatus() {

    int cpuUsage = processCount * 10;

    if (cpuUsage > 100)
        cpuUsage = 100;

    printf("\n\n========================================\n");
    printf("            SYSTEM STATUS\n");
    printf("========================================\n");

    printf("CPU Usage       : %d%%\n", cpuUsage);

    printf("RAM Usage       : %d / %d MB\n",
           usedRAM, TOTAL_RAM);

    printf("Storage         : %d / %d GB\n",
           usedStorage, TOTAL_STORAGE);

    printf("Running Processes: %d\n",
           processCount);

    printf("Battery         : %d%%\n",
           battery);

    printf("Power Mode      : %s\n",
           powerMode);

    printf("========================================\n");
}

// =====================================================
// MAIN PROGRAM
// =====================================================

int main() {

    int choice;

    printf("\n========================================\n");
    printf("     LAPTOP OS RESOURCE MANAGER\n");
    printf("========================================\n");

    do {

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
        scanf("%d", &choice);

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
                printf("\nShutting down Laptop OS Simulator...\n");
                break;

            default:
                printf("\nInvalid choice! Please try again.\n");
        }

    } while (choice != 8);

    return 0;
}
