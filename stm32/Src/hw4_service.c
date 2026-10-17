#include "hw4_service.h"
#include "ble_lib.h"
#include "blesvc.h"
#include "svc_ctl.h"
#include "svc_private.h"

/* BlueNRG receives 128-bit UUIDs least-significant byte first. */
static const uint8_t HW4_SERVICE_UUID[16] = {0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
static const uint8_t HW4_XYZ_UUID[16]     = {0xf1,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
static const uint8_t HW4_FREQ_UUID[16]    = {0xf2,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
static const uint8_t HW4_MOTION_UUID[16]  = {0xf3,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};

static uint16_t service_handle, xyz_handle, frequency_handle, motion_handle;
static volatile uint16_t frequency_hz = 10;
static volatile uint8_t xyz_notify_enabled, motion_notify_enabled;

volatile uint32_t hw4_diag_xyz_notify_attempts;
volatile uint32_t hw4_diag_xyz_notify_successes;
volatile uint32_t hw4_diag_motion_notify_attempts;
volatile uint32_t hw4_diag_motion_notify_successes;
volatile uint32_t hw4_diag_frequency_writes;
volatile uint8_t hw4_diag_xyz_cccd_enabled;
volatile uint8_t hw4_diag_motion_cccd_enabled;
volatile uint8_t hw4_diag_xyz_last_status;
volatile uint8_t hw4_diag_motion_last_status;

static SVCCTL_EvtAckStatus_t HW4Svc_EventHandler(void *event)
{
  hci_event_pckt *hci = (hci_event_pckt *)(((hci_uart_pckt *)event)->data);
  if (hci->evt == EVT_VENDOR) {
    evt_blue_aci *blue = (evt_blue_aci *)hci->data;
    if (blue->ecode == EVT_BLUE_GATT_ATTRIBUTE_MODIFIED) {
      evt_gatt_attr_modified *modified = (evt_gatt_attr_modified *)blue->data;
      if (modified->attr_handle == frequency_handle + 1U && modified->data_length == 2U) {
        uint16_t hz = (uint16_t)modified->att_data[0] | ((uint16_t)modified->att_data[1] << 8);
        if (hz >= 1U && hz <= 100U) {
          frequency_hz = hz;
          ++hw4_diag_frequency_writes;
        }
      } else if (modified->attr_handle == xyz_handle + 2U && modified->data_length >= 1U) {
        xyz_notify_enabled = (modified->att_data[0] & COMSVC_Notification) != 0U;
        hw4_diag_xyz_cccd_enabled = xyz_notify_enabled;
      } else if (modified->attr_handle == motion_handle + 2U && modified->data_length >= 1U) {
        motion_notify_enabled = (modified->att_data[0] & COMSVC_Notification) != 0U;
        hw4_diag_motion_cccd_enabled = motion_notify_enabled;
      }
    }
  }
  return SVCCTL_EvtNotAck;
}

void HW4Svc_Init(void)
{
  SVCCTL_RegisterSvcHandler(HW4Svc_EventHandler);
  (void)aci_gatt_add_serv(UUID_TYPE_128, HW4_SERVICE_UUID, PRIMARY_SERVICE, 10, &service_handle);
  (void)aci_gatt_add_char(service_handle, UUID_TYPE_128, HW4_XYZ_UUID, 6,
                          CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
                          10, 0, &xyz_handle);
  (void)aci_gatt_add_char(service_handle, UUID_TYPE_128, HW4_FREQ_UUID, 2,
                          CHAR_PROP_WRITE, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
                          10, 0, &frequency_handle);
  (void)aci_gatt_add_char(service_handle, UUID_TYPE_128, HW4_MOTION_UUID, 1,
                          CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE, GATT_NOTIFY_ATTRIBUTE_WRITE,
                          10, 0, &motion_handle);
}

void HW4Svc_NotifyXYZ(const int16_t xyz_mg[3])
{
  uint8_t payload[6];
  tBleStatus status;
  for (uint8_t i = 0; i < 3; ++i) { payload[2*i] = (uint8_t)xyz_mg[i]; payload[2*i+1] = (uint8_t)(xyz_mg[i] >> 8); }
  if (xyz_notify_enabled) {
    ++hw4_diag_xyz_notify_attempts;
    status = aci_gatt_update_char_value(service_handle, xyz_handle, 0, sizeof(payload), payload);
    hw4_diag_xyz_last_status = status;
    if (status == BLE_STATUS_SUCCESS) ++hw4_diag_xyz_notify_successes;
  }
}

void HW4Svc_NotifyMotion(void)
{
  const uint8_t payload = 1;
  tBleStatus status;
  if (motion_notify_enabled) {
    ++hw4_diag_motion_notify_attempts;
    status = aci_gatt_update_char_value(service_handle, motion_handle, 0, 1, &payload);
    hw4_diag_motion_last_status = status;
    if (status == BLE_STATUS_SUCCESS) ++hw4_diag_motion_notify_successes;
  }
}

uint16_t HW4Svc_GetFrequencyHz(void) { return frequency_hz; }
uint8_t HW4Svc_XYZNotifyEnabled(void) { return xyz_notify_enabled; }
uint8_t HW4Svc_MotionNotifyEnabled(void) { return motion_notify_enabled; }
