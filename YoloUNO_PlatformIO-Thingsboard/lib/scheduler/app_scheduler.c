#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "app_scheduler.h"

#define INTERRUPT_CYCLE 10 // 10 milliseconds

typedef struct {
    void (*pTask)(void);
    uint32_t Delay;
    uint32_t Period;
    uint8_t RunMe;
    uint32_t TaskID;
} sTask;

static sTask SCH_tasks_G[SCH_MAX_TASKS];
static uint32_t newTaskID = 0;
static TimerHandle_t timerHandle;

static uint32_t Get_New_Task_ID(void);
static void TIMER_Init(void);
static void TimerCallback(TimerHandle_t xTimer);

void SCH_Init(void) {
    TIMER_Init();
}

void SCH_Update(void) {
    if (SCH_tasks_G[0].pTask && SCH_tasks_G[0].RunMe == 0) {
        if (SCH_tasks_G[0].Delay > 0) {
            SCH_tasks_G[0].Delay--;
        }
        if (SCH_tasks_G[0].Delay == 0) {
            SCH_tasks_G[0].RunMe = 1;
        }
    }
}

uint32_t SCH_Add_Task(void (*pFunction)(), uint32_t DELAY, uint32_t PERIOD) {
    uint8_t newTaskIndex = 0;
    uint32_t sumDelay = 0;
    uint32_t newDelay = 0;

    for (newTaskIndex = 0; newTaskIndex < SCH_MAX_TASKS; newTaskIndex++) {
        sumDelay += SCH_tasks_G[newTaskIndex].Delay;
        if (sumDelay > DELAY) {
            newDelay = DELAY - (sumDelay - SCH_tasks_G[newTaskIndex].Delay);
            SCH_tasks_G[newTaskIndex].Delay = sumDelay - DELAY;
            for (uint8_t i = SCH_MAX_TASKS - 1; i > newTaskIndex; i--) {
                SCH_tasks_G[i] = SCH_tasks_G[i - 1];
            }
            SCH_tasks_G[newTaskIndex].pTask = pFunction;
            SCH_tasks_G[newTaskIndex].Delay = newDelay;
            SCH_tasks_G[newTaskIndex].Period = PERIOD;
            SCH_tasks_G[newTaskIndex].RunMe = (newDelay == 0) ? 1 : 0;
            SCH_tasks_G[newTaskIndex].TaskID = Get_New_Task_ID();
            return SCH_tasks_G[newTaskIndex].TaskID;
        } else if (SCH_tasks_G[newTaskIndex].pTask == NULL) {
            SCH_tasks_G[newTaskIndex].pTask = pFunction;
            SCH_tasks_G[newTaskIndex].Delay = DELAY - sumDelay;
            SCH_tasks_G[newTaskIndex].Period = PERIOD;
            SCH_tasks_G[newTaskIndex].RunMe = (SCH_tasks_G[newTaskIndex].Delay == 0) ? 1 : 0;
            SCH_tasks_G[newTaskIndex].TaskID = Get_New_Task_ID();
            return SCH_tasks_G[newTaskIndex].TaskID;
        }
    }
    return 0;
}

uint8_t SCH_Delete_Task(uint32_t taskID) {
    uint8_t Return_code = 0;
    if (taskID != NO_TASK_ID) {
        for (uint8_t taskIndex = 0; taskIndex < SCH_MAX_TASKS; taskIndex++) {
            if (SCH_tasks_G[taskIndex].TaskID == taskID) {
                Return_code = 1;
                if (taskIndex < SCH_MAX_TASKS - 1 && SCH_tasks_G[taskIndex + 1].pTask) {
                    SCH_tasks_G[taskIndex + 1].Delay += SCH_tasks_G[taskIndex].Delay;
                }
                for (uint8_t j = taskIndex; j < SCH_MAX_TASKS - 1; j++) {
                    SCH_tasks_G[j] = SCH_tasks_G[j + 1];
                }
                SCH_tasks_G[SCH_MAX_TASKS - 1].pTask = NULL;
                SCH_tasks_G[SCH_MAX_TASKS - 1].Delay = 0;
                SCH_tasks_G[SCH_MAX_TASKS - 1].Period = 0;
                SCH_tasks_G[SCH_MAX_TASKS - 1].RunMe = 0;
                SCH_tasks_G[SCH_MAX_TASKS - 1].TaskID = 0;
                return Return_code;
            }
        }
    }
    return Return_code;
}

void SCH_Dispatch_Tasks(void) {
    if (SCH_tasks_G[0].RunMe > 0) {
        (*SCH_tasks_G[0].pTask)();
        SCH_tasks_G[0].RunMe = 0;
        sTask temtask = SCH_tasks_G[0];
        SCH_Delete_Task(temtask.TaskID);
        if (temtask.Period != 0) {
            SCH_Add_Task(temtask.pTask, temtask.Period, temtask.Period);
        }
    }
}

static void TIMER_Init(void) {
    timerHandle = xTimerCreate("SchedulerTimer", pdMS_TO_TICKS(INTERRUPT_CYCLE), pdTRUE, NULL, TimerCallback);
    xTimerStart(timerHandle, 0);
}

static void TimerCallback(TimerHandle_t xTimer) {
    SCH_Update();
}

static uint32_t Get_New_Task_ID(void) {
    newTaskID++;
    if (newTaskID == NO_TASK_ID) {
        newTaskID++;
    }
    return newTaskID;
}