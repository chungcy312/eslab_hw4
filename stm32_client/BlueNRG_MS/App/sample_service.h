/* ESLAB HW4 STM32 Central / GATT client, B-L475E-IOT01A1, X-CUBE-BLE1 7.0.0 */
#ifndef SAMPLE_SERVICE_H
#define SAMPLE_SERVICE_H
#include <stdint.h>
#include "bluenrg_gap.h"
#include "bluenrg_aci_const.h"
#include "hci.h"
#include "hci_le.h"
#include "sm.h"
#include "role_type.h"

#define IDB04A1 0
#define IDB05A1 1

/* Called by ST SampleApp's BlueNRG host glue. */
tBleStatus Add_Sample_Service(void); /* not used by the client */
void Make_Connection(void);
void BLE_Client_Process(void);
/* USER button: stops the timed 5/2-Hz demo and toggles manual 5/20-Hz mode. */
void BLE_Client_ToggleFrequency(void);
/* Manual override (1..100 Hz). Connection/reconnect restarts the timed demo. */
void BLE_Client_SetFrequency(uint16_t hz);
void user_notify(void *pData);
#endif
