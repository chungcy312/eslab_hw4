#ifndef HW4_SERVICE_H
#define HW4_SERVICE_H

#include <stdint.h>

/* Live BLE/GATT diagnostics for CubeIDE Expressions.  A non-zero
 * hw4_diag_xyz_last_status is the controller return code from the last
 * aci_gatt_update_char_value() attempt. */
extern volatile uint32_t hw4_diag_xyz_notify_attempts;
extern volatile uint32_t hw4_diag_xyz_notify_successes;
extern volatile uint32_t hw4_diag_motion_notify_attempts;
extern volatile uint32_t hw4_diag_motion_notify_successes;
extern volatile uint32_t hw4_diag_frequency_writes;
extern volatile uint8_t  hw4_diag_xyz_cccd_enabled;
extern volatile uint8_t  hw4_diag_motion_cccd_enabled;
extern volatile uint8_t  hw4_diag_xyz_last_status;
extern volatile uint8_t  hw4_diag_motion_last_status;

/* All functions that issue an ACI command must run in TASK_BLE. */
void HW4Svc_Init(void);
void HW4Svc_NotifyXYZ(const int16_t xyz_mg[3]);
void HW4Svc_NotifyMotion(void);
uint16_t HW4Svc_GetFrequencyHz(void);
uint8_t HW4Svc_XYZNotifyEnabled(void);
uint8_t HW4Svc_MotionNotifyEnabled(void);

#endif
