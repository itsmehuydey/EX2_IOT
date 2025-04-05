#ifndef SCHEDULER_H
#define SCHEDULER_H

#define SCH_MAX_TASKS 10  // Số lượng task tối đa
#define NO_TASK_ID 0xFFFFFFFF

#ifdef __cplusplus
extern "C" {
#endif

void SCH_Init(void);
void SCH_Update(void);
uint32_t SCH_Add_Task(void (*pFunction)(), uint32_t DELAY, uint32_t PERIOD);
uint8_t SCH_Delete_Task(uint32_t taskID);
void SCH_Dispatch_Tasks(void);

#ifdef __cplusplus
}
#endif

#endif