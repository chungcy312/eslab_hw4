#include "hw4_app.h"
#include "hw4_service.h"
#include "hr.h"
#include "stm32l4xx_hal.h"
#include "stm32l475e_iot01_accelero.h"
#include "stm32l475e_iot01.h"
#include "lsm6dsl.h"
#include "scheduler.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h" /* selected from the FreeRTOS include path by Makefile */

/* These BSP functions are implemented by stm32l475e_iot01.c but omitted from
 * the older public header shipped in CubeL4 v1.18.2. */
extern void SENSOR_IO_Write(uint8_t Addr, uint8_t Reg, uint8_t Value);
extern uint8_t SENSOR_IO_Read(uint8_t Addr, uint8_t Reg);

#define LSM6DSL_ADDRESS             0xD4U
#define LSM6DSL_CTRL10_SIGN_MOTION  0x01U
#define LSM6DSL_MD1_SIGN_MOTION     0x01U
#define HW4_MOTION_INT_PIN          GPIO_PIN_11
#define HW4_MOTION_INT_PORT         GPIOD
/* B-L475E-IOT01A LD2 (green user LED). */
#define HW4_DIAG_LED_PIN            GPIO_PIN_14
#define HW4_DIAG_LED_PORT           GPIOB

static QueueHandle_t xyz_queue;
static TaskHandle_t ble_task_handle;
static volatile uint8_t motion_pending;
static volatile uint8_t ble_task_alive;

volatile uint32_t hw4_diag_acc_samples;
volatile uint32_t hw4_diag_xyz_dequeued;
volatile uint32_t hw4_diag_motion_irqs;
volatile uint8_t hw4_diag_acc_init_status;

void HW4_DiagLedInit(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* SystemPower_Config puts PB pins in analog mode; reclaim PB14 for LD2. */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin = HW4_DIAG_LED_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HW4_DIAG_LED_PORT, &gpio);
  HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_RESET);
}

void HW4_DiagHeartbeatTask(void *argument)
{
  (void)argument;
  for (;;) {
    /* Second flash: TASK_BLE services the transport. Third flash: the
     * controller accepted the advertising command. */
    HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(80U));
    HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(120U));
    if (ble_task_alive != 0U) {
      HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_SET);
      vTaskDelay(pdMS_TO_TICKS(80U));
      HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_RESET);
    }
    vTaskDelay(pdMS_TO_TICKS(120U));
    if (HR_AdvertisingIsActive() != 0U) {
      HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_SET);
      vTaskDelay(pdMS_TO_TICKS(80U));
      HAL_GPIO_WritePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN, GPIO_PIN_RESET);
    }
    vTaskDelay(pdMS_TO_TICKS(600U));
  }
}

void HW4_DiagFaultLoop(void)
{
  for (;;) {
    HAL_GPIO_TogglePin(HW4_DIAG_LED_PORT, HW4_DIAG_LED_PIN);
    HAL_Delay(100U);
  }
}

void HW4_Lsm6dslInt1Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOD_CLK_ENABLE();
  gpio.Pin = HW4_MOTION_INT_PIN;
  gpio.Mode = GPIO_MODE_IT_RISING;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HW4_MOTION_INT_PORT, &gpio);
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void HW4_Lsm6dslInit(void)
{
  hw4_diag_acc_init_status = BSP_ACCELERO_Init(); /* 52 Hz, +/-2 g; BSP values are int16 in mg. */
  /* Significant-motion engine, INT1 routing, threshold 62.5 mg x 2.
   * Register values follow the LSM6DSL datasheet and are independent of the
   * accelerometer sample read path provided by the BSP. */
  SENSOR_IO_Write(LSM6DSL_ADDRESS, LSM6DSL_ACC_GYRO_CTRL10_C, LSM6DSL_CTRL10_SIGN_MOTION);
  SENSOR_IO_Write(LSM6DSL_ADDRESS, LSM6DSL_ACC_GYRO_WAKE_UP_THS, 0x02U);
  SENSOR_IO_Write(LSM6DSL_ADDRESS, LSM6DSL_ACC_GYRO_MD1_CFG, LSM6DSL_MD1_SIGN_MOTION);
  HW4_Lsm6dslInt1Init();
}

void HW4_AppInit(void)
{
  xyz_queue = xQueueCreate(1, sizeof(int16_t) * 3U);
  configASSERT(xyz_queue != NULL);
  HW4_Lsm6dslInit();
}

void HW4_AccelTask(void *argument)
{
  (void)argument;
  for (;;) {
    int16_t xyz[3] = {0};
    uint16_t hz = HW4Svc_GetFrequencyHz();
    if (hz == 0U) hz = 1U;
    BSP_ACCELERO_AccGetXYZ(xyz);
    ++hw4_diag_acc_samples;
    (void)xQueueOverwrite(xyz_queue, xyz);
    vTaskDelay(pdMS_TO_TICKS(1000U / hz));
  }
}

void HW4_BleTask(void *argument)
{
  int16_t xyz[3];
  TickType_t next_advertising_retry = 0U;
  (void)argument;
  for (;;) {
    /* The legacy ST transport must be serviced continuously. All ACI writes
     * remain in this task, so no two tasks can issue HCI commands concurrently. */
    SCH_Run();
    ble_task_alive = 1U;
    if ((TickType_t)(xTaskGetTickCount() - next_advertising_retry) <
        (TickType_t)(~(TickType_t)0U / 2U)) {
      HR_EnsureAdvertising();
      next_advertising_retry = xTaskGetTickCount() + pdMS_TO_TICKS(2000U);
    }
    if (xQueueReceive(xyz_queue, xyz, 0) == pdPASS) {
      ++hw4_diag_xyz_dequeued;
      HW4Svc_NotifyXYZ(xyz);
    }
    if (motion_pending != 0U) { motion_pending = 0U; HW4Svc_NotifyMotion(); }
    /* TASK_BLE (priority 4) must block briefly.  taskYIELD() only rotates
     * equal-priority tasks, so the previous busy loop starved TASK_ACC
     * (priority 3), leaving xyz_queue permanently empty. */
    vTaskDelay(pdMS_TO_TICKS(1U));
  }
}

void HW4_MotionExtiCallback(void)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  motion_pending = 1U;
  ++hw4_diag_motion_irqs;
  if (ble_task_handle != NULL) {
    vTaskNotifyGiveFromISR(ble_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

void HW4_SetBleTaskHandle(void *handle) { ble_task_handle = (TaskHandle_t)handle; }

void vApplicationMallocFailedHook(void) { taskDISABLE_INTERRUPTS(); for (;;) {} }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{ (void)task; (void)name; taskDISABLE_INTERRUPTS(); for (;;) {} }
