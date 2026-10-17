#ifndef HW4_APP_H
#define HW4_APP_H

#include <stdint.h>

/* Live diagnostics: inspect these globals in the CubeIDE Expressions view.
 * They deliberately require no UART and do not change the GATT protocol. */
extern volatile uint32_t hw4_diag_acc_samples;
extern volatile uint32_t hw4_diag_xyz_dequeued;
extern volatile uint32_t hw4_diag_motion_irqs;
extern volatile uint8_t  hw4_diag_acc_init_status;

void HW4_AppInit(void);
void HW4_Lsm6dslInit(void);
void HW4_Lsm6dslInt1Init(void);
void HW4_AccelTask(void *argument);
void HW4_BleTask(void *argument);
void HW4_DiagLedInit(void);
void HW4_DiagHeartbeatTask(void *argument);
void HW4_DiagFaultLoop(void);
void HW4_MotionExtiCallback(void);
void HW4_SetBleTaskHandle(void *handle);

#endif
