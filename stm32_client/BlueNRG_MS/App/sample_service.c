/*
 * ESLAB HW4 BLE Central client for B-L475E-IOT01A1 / BlueNRG-MS.
 * Based on ST's X-CUBE-BLE1 v7.0.0 SampleApp event plumbing.
 * The peer server is eslab_hw4-main (device name "ESLAB_HW4").
 * No BLE HCI/GATT calls are made from the button interrupt handler.
 *
 * Custom UUIDs:
 * service 12345678-1234-5678-1234-56789abcdef0
 * xyz     12345678-1234-5678-1234-56789abcdef1 (NOTIFY; 3 x int16 LE, mg)
 * freq    12345678-1234-5678-1234-56789abcdef2 (WRITE; uint16 LE, Hz)
 * Motion characteristic def3 is optional and not required for Option 1.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "sample_service.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_hal_aci.h"
#include "b_l475e_iot01a1.h"

extern uint8_t bnrg_expansion_board;
extern BLE_RoleTypeDef BLE_Role;
volatile int connected = FALSE;
volatile uint8_t set_connectable = TRUE;
volatile uint16_t connection_handle = 0;
volatile uint8_t notification_enabled = FALSE;

#define SCAN_INTERVAL       0x00A0U  /* 100 ms */
#define SCAN_WINDOW         0x0050U  /*  50 ms */
#define RETRY_DELAY_MS      2000U
#define CONN_TIMEOUT_MS     15000U
#define DISCOVERY_TIMEOUT_MS 12000U
#define TARGET_NAME        "ESLAB_HW4"

/* Auto-demo sequence from the class RPi demo.py: 5 Hz x 15 s, then 2 Hz x 15 s.
 * Durations begin after a successful GATT write response (not at request time).
 * Demo completion leaves 2 Hz notifications streaming for the tilt test.
 */
#define DEMO_STAGE_MS      10000U

/* BlueNRG API expects 128-bit UUIDs in little-endian wire order. */
static const uint8_t UUID_SERVICE[16] = {0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
static const uint8_t UUID_XYZ[16]     = {0xf1,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};
static const uint8_t UUID_FREQ[16]    = {0xf2,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12};

typedef enum {
  S_IDLE, S_SCAN_START, S_SCANNING, S_SCAN_STOP_REQUEST, S_SCAN_STOPPING, S_CONNECT_START,
  S_CONNECTING, S_FIND_SERVICE, S_WAIT_SERVICE, S_FIND_XYZ, S_WAIT_XYZ,
  S_FIND_FREQ, S_WAIT_FREQ, S_FIND_CCCD, S_SUBSCRIBE,
  S_WAIT_SUBSCRIBE, S_WRITE_FREQ, S_WAIT_FREQ_WRITE, S_READY,
  S_RETRY, S_UNSUPPORTED
} ClientState;

static volatile ClientState state = S_IDLE;
static uint32_t since_ms = 0;
static uint32_t retry_ms = 0;
static uint8_t peer_addr[6] = {0};
static uint8_t peer_type = PUBLIC_ADDR;
static uint16_t service_start = 0, service_end = 0;
static uint16_t xyz_decl = 0, xyz_val = 0;
static uint16_t freq_decl = 0, freq_val = 0;
static uint16_t cccd = 0;
static uint16_t requested_hz = 5;
static uint16_t configured_hz = 0;
static uint32_t received_samples = 0;

/* Keep demo accounting separate from the BLE/GATT state machine. */
typedef enum {
  DEMO_AWAIT_5HZ,    /* connection starts with requested_hz = 5 */
  DEMO_5HZ_RUNNING,
  DEMO_AWAIT_2HZ,    /* 5-Hz summary shown, awaiting 2-Hz write response */
  DEMO_2HZ_RUNNING,
  DEMO_FINISHED,     /* leave 2-Hz stream running, invite tilt test */
  DEMO_MANUAL        /* user button/API overrides the timed sequence */
} DemoPhase;

static DemoPhase demo_phase = DEMO_AWAIT_5HZ;
static uint32_t connected_at_ms = 0;
static uint32_t stage_at_ms = 0;
static uint32_t stage_samples = 0;

static void demo_start_stage(DemoPhase next_phase, uint16_t hz)
{
  demo_phase = next_phase;
  stage_at_ms = HAL_GetTick();
  stage_samples = 0;
  printf("\r\n========== DEMO: %u Hz for 10 seconds ==========\r\n", (unsigned)hz);
  printf("DEMO: move the Server board; observe XYZ and timestamps.\r\n");
}

static void demo_summary(uint16_t hz)
{
  uint32_t duration_ms = (uint32_t)(HAL_GetTick() - stage_at_ms);
  /* Rate is per entire stage window, not between the first/last packets. */
  uint32_t rate_tenths = duration_ms ?
      (uint32_t)(((uint64_t)stage_samples * 10000ULL + duration_ms/2U) / duration_ms) : 0;
  printf("\r\n=== %u-Hz stage summary ===\r\n", (unsigned)hz);
  printf("Elapsed: %lu.%03lu s; notifications: %lu; measured rate: %lu.%01lu Hz\r\n",
         (unsigned long)(duration_ms/1000U), (unsigned long)(duration_ms%1000U),
         (unsigned long)stage_samples, (unsigned long)(rate_tenths/10U),
         (unsigned long)(rate_tenths%10U));
  printf("Note: rate is estimated from Client notification arrivals, not sensor hardware timing.\r\n");
}

static void demo_process(void)
{
  uint32_t duration_ms;
  if (state != S_READY) return;
  if (demo_phase != DEMO_5HZ_RUNNING && demo_phase != DEMO_2HZ_RUNNING) return;
  duration_ms = (uint32_t)(HAL_GetTick() - stage_at_ms);
  if (duration_ms < DEMO_STAGE_MS) return;

  if (demo_phase == DEMO_5HZ_RUNNING) {
    demo_summary(5U);
    demo_phase = DEMO_AWAIT_2HZ;
    requested_hz = 2U;
    printf("DEMO: switching to 2 Hz ...\r\n");
    /* Existing S_READY path will issue the asynchronous GATT write. */
  } else {
    demo_summary(2U);
    demo_phase = DEMO_FINISHED;
    printf("\r\n========== TIMED DEMO COMPLETE ==========\r\n");
    //printf("TILT TEST: rotate or tilt the Server board; XYZ continues at 2 Hz.\r\n");
    printf("USER button: switch to manual frequency control (5 Hz / 20 Hz).\r\n");
  }
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }
static void change_state(ClientState s) { state = s; since_ms = HAL_GetTick(); }
static uint8_t elapsed(uint32_t ms) { return (uint32_t)(HAL_GetTick()-since_ms)>=ms; }
static void reset_handles(void)
{
  service_start=service_end=xyz_decl=xyz_val=freq_decl=freq_val=cccd=0;
  connection_handle=0; connected=FALSE; notification_enabled=FALSE;
  configured_hz=0; received_samples=0;
  requested_hz=5U;
  demo_phase=DEMO_AWAIT_5HZ;
  connected_at_ms=0; stage_at_ms=0; stage_samples=0;
}
static void delay_retry(const char *why)
{
  printf("CLIENT: %s; retry in %lu ms\r\n",why,(unsigned long)RETRY_DELAY_MS);
  retry_ms=HAL_GetTick(); change_state(S_RETRY);
}
static void failed_gatt(const char *why)
{
  /* Preserve the connection for debugging: wrong service / outdated firmware. */
  printf("CLIENT GATT: %s. Check ESLAB_HW4 firmware/UUIDs.\r\n",why);
  change_state(S_UNSUPPORTED);
}
static uint8_t contains_name(const uint8_t *data, uint8_t n)
{
  const size_t target_len = sizeof(TARGET_NAME)-1U;
  unsigned i=0;
  while (i<n) {
    unsigned len=data[i];
    if (!len || len>n-i-1U) break;
    if ((data[i+1]==AD_TYPE_COMPLETE_LOCAL_NAME || data[i+1]==AD_TYPE_SHORTENED_LOCAL_NAME) &&
        len-1U==target_len && memcmp(&data[i+2],TARGET_NAME,target_len)==0) return 1;
    i+=len+1U;
  }
  return 0;
}
static void found_advertisement(const uint8_t *addr,uint8_t addr_type,uint8_t adv_type,
                                const uint8_t *data,uint8_t len,int8_t rssi)
{
  if (state!=S_SCANNING || !contains_name(data,len)) return;
  /* Scan responses can contain the name; prior connectable ADV_IND may have no name. */
  memcpy(peer_addr,addr,6); peer_type=addr_type;
  printf("CLIENT: found " TARGET_NAME " %02X:%02X:%02X:%02X:%02X:%02X type=%u evt=%u RSSI=%d\r\n",
         addr[5],addr[4],addr[3],addr[2],addr[1],addr[0],(unsigned)addr_type,
         (unsigned)adv_type,(int)rssi);
  /* Defer the ACI command to BLE_Client_Process(), outside the HCI event callback. */
  change_state(S_SCAN_STOP_REQUEST);
}

/* Called once by app_bluenrg_ms.c after its BLE stack is ready. */
void Make_Connection(void) { change_state(S_SCAN_START); }
/* Kept solely so the existing SampleApp glue still links. Client does not add a local GATT service. */
tBleStatus Add_Sample_Service(void) { return BLE_STATUS_ERROR; }

static void start_gatt(uint8_t next,tBleStatus rc,const char *description)
{
  if (rc==BLE_STATUS_SUCCESS) change_state((ClientState)next);
  else {
    printf("CLIENT: %s failed 0x%02X\r\n",description,(unsigned)rc);
    failed_gatt(description);
  }
}

/* All BLE requests occur in the normal main-loop context, never inside EXTI. */
void BLE_Client_Process(void)
{
  tBleStatus rc;
  switch (state) {
  case S_SCAN_START:
    printf("CLIENT: scanning for " TARGET_NAME " ...\r\n");
    rc=aci_gap_start_general_discovery_proc(SCAN_INTERVAL,SCAN_WINDOW,PUBLIC_ADDR,0);
    if (rc==BLE_STATUS_SUCCESS) change_state(S_SCANNING);
    else { printf("CLIENT: scan start = 0x%02X\r\n",(unsigned)rc); delay_retry("scan failed"); }
    break;
  case S_SCAN_STOP_REQUEST:
    rc=aci_gap_terminate_gap_procedure(GAP_GENERAL_DISCOVERY_PROC);
    printf("CLIENT: stop scan = 0x%02X\r\n",(unsigned)rc);
    if (rc==BLE_STATUS_SUCCESS) change_state(S_SCAN_STOPPING);
    else delay_retry("cannot stop scan");
    break;
  case S_SCAN_STOPPING:
    if (elapsed(4000U)) delay_retry("scan stop timed out");
    break;
  case S_CONNECT_START:
    printf("CLIENT: connecting ...\r\n");
    rc=aci_gap_create_connection(SCAN_INTERVAL,SCAN_WINDOW,peer_type,peer_addr,
                                 PUBLIC_ADDR,CONN_P1,CONN_P2,0,SUPERV_TIMEOUT,CONN_L1,CONN_L2);
    if (rc==BLE_STATUS_SUCCESS) change_state(S_CONNECTING);
    else {printf("CLIENT: connect start failed 0x%02X\r\n",(unsigned)rc);delay_retry("connect start failed");}
    break;
  case S_CONNECTING:
    if(elapsed(CONN_TIMEOUT_MS)) { /* Controller may still be connecting; don't start another. */
      printf("CLIENT: connection pending for >15s; waiting for controller event\r\n");
      since_ms=HAL_GetTick();
    }
    break;
  case S_FIND_SERVICE:
    printf("GATT: discovering ESLAB HW4 service\r\n");
    start_gatt(S_WAIT_SERVICE,aci_gatt_disc_prim_service_by_uuid(connection_handle,UUID_TYPE_128,(uint8_t*)UUID_SERVICE),"service discovery");
    break;
  case S_FIND_XYZ:
    printf("GATT: discovering XYZ characteristic\r\n");
    start_gatt(S_WAIT_XYZ,aci_gatt_disc_charac_by_uuid(connection_handle,service_start,service_end,UUID_TYPE_128,UUID_XYZ),"XYZ discovery");
    break;
  case S_FIND_FREQ:
    printf("GATT: discovering FREQ characteristic\r\n");
    start_gatt(S_WAIT_FREQ,aci_gatt_disc_charac_by_uuid(connection_handle,service_start,service_end,UUID_TYPE_128,UUID_FREQ),"frequency discovery");
    break;
  case S_FIND_CCCD:
    /* ESLAB_HW4 layout confirmed on the peer by GATT discovery:
     *   XYZ declaration 0x0018 / value 0x0019 / CCCD 0x001A
     *   Frequency declaration 0x001B / value 0x001C
     * For this server, a NOTIFY characteristic has its CCCD immediately
     * after its value. BlueNRG-MS returns 0x42 for descriptor discovery
     * over this one-handle span, so write the verified CCCD directly.
     * Do not reuse this assumption for unrelated BLE servers.
     */
    if (!xyz_val || !freq_decl || freq_decl != (uint16_t)(xyz_val + 2U)) {
      failed_gatt("unexpected XYZ/CCCD/FREQ handle layout");
      break;
    }
    cccd = (uint16_t)(xyz_val + 1U);
    printf("GATT: XYZ CCCD inferred @0x%04X (XYZ=0x%04X, FREQ decl=0x%04X)\r\n",
           (unsigned)cccd, (unsigned)xyz_val, (unsigned)freq_decl);
    change_state(S_SUBSCRIBE);
    break;
  case S_SUBSCRIBE: {
    uint8_t value[2]={1,0};
    printf("GATT: enable XYZ notifications @0x%04X\r\n",cccd);
    start_gatt(S_WAIT_SUBSCRIBE,aci_gatt_write_charac_descriptor(connection_handle,cccd,2,value),"CCCD write");
    break;
  }
  case S_WRITE_FREQ: {
    uint8_t value[2]={(uint8_t)requested_hz,(uint8_t)(requested_hz>>8)};
    printf("GATT: write frequency = %u Hz\r\n",(unsigned)requested_hz);
    start_gatt(S_WAIT_FREQ_WRITE,aci_gatt_write_charac_value(connection_handle,freq_val,2,value),"frequency write");
    break;
  }
  case S_READY:
    demo_process();
    if (requested_hz!=configured_hz) change_state(S_WRITE_FREQ);
    break;
  case S_RETRY:
    if ((uint32_t)(HAL_GetTick()-retry_ms)>=RETRY_DELAY_MS) {reset_handles();change_state(S_SCAN_START);}
    break;
  case S_WAIT_SERVICE: case S_WAIT_XYZ: case S_WAIT_FREQ:
  case S_WAIT_SUBSCRIBE: case S_WAIT_FREQ_WRITE:
    if (elapsed(DISCOVERY_TIMEOUT_MS)) failed_gatt("GATT procedure timed out");
    break;
  default: break;
  }
}

void BLE_Client_SetFrequency(uint16_t hz)
{
  if(hz<1U || hz>100U) {printf("CLIENT: frequency must be 1..100 Hz\r\n");return;}
  if (demo_phase != DEMO_MANUAL) {
    if (demo_phase == DEMO_FINISHED)
      printf("DEMO: manual frequency control enabled.\r\n");
    else
      printf("DEMO: timed sequence cancelled; switching to manual mode.\r\n");
    demo_phase=DEMO_MANUAL;
  }
  requested_hz=hz;
  printf("CLIENT: requested new frequency %u Hz\r\n",(unsigned)requested_hz);
}
void BLE_Client_ToggleFrequency(void)
{
  if (state!=S_READY) {printf("CLIENT: button ignored until GATT ready\r\n");return;}
  BLE_Client_SetFrequency((requested_hz==5U)?20U:5U);
}

static void on_connect(const evt_le_connection_complete *event)
{
  if (event->status!=BLE_STATUS_SUCCESS) {
    printf("CLIENT: connection failed status=0x%02X\r\n",(unsigned)event->status);
    delay_retry("connection failed");return;
  }
  connected=TRUE; connection_handle=event->handle;
  /* Restart the 5 -> 2 Hz demo after a new BLE connection. */
  requested_hz=5U;
  configured_hz=0;
  received_samples=0;
  connected_at_ms=HAL_GetTick();
  demo_phase=DEMO_AWAIT_5HZ;
  stage_at_ms=0; stage_samples=0;
  printf("CLIENT: connected, conn=0x%04X\r\n",connection_handle);
  change_state(S_FIND_SERVICE);
}
static void on_disconnect(void)
{
  printf("CLIENT: disconnected\r\n");
  reset_handles();
  delay_retry("link lost");
}
static void on_service(const evt_att_find_by_type_val_resp *e)
{
  if (state!=S_WAIT_SERVICE || e->conn_handle!=connection_handle) return;
  for (unsigned i=0;i+4U<=e->event_data_length;i+=4U) {
    uint16_t start=le16(&e->handles_info_list[i]), end=le16(&e->handles_info_list[i+2U]);
    if (start>0 && end>=start) {service_start=start;service_end=end;}
  }
}
static void on_characteristic(const evt_gatt_disc_read_char_by_uuid_resp *e)
{
  if (e->conn_handle!=connection_handle || e->event_data_length<5U) return;
  /* attr_handle is the characteristic DECLARATION, value handle is in attr_value[1:3]. */
  const uint16_t decl=e->attr_handle,value=le16(&e->attr_value[1]);
  if (state==S_WAIT_XYZ) {xyz_decl=decl;xyz_val=value;printf("GATT: XYZ decl=0x%04X val=0x%04X\r\n",decl,value);}
  else if (state==S_WAIT_FREQ) {freq_decl=decl;freq_val=value;printf("GATT: FREQ decl=0x%04X val=0x%04X\r\n",decl,value);}
}
static void on_gatt_complete(const evt_gatt_procedure_complete *e)
{
  if (e->conn_handle!=connection_handle) return;
  ClientState s=state;
  if (s!=S_WAIT_SERVICE && s!=S_WAIT_XYZ && s!=S_WAIT_FREQ &&
      s!=S_WAIT_SUBSCRIBE && s!=S_WAIT_FREQ_WRITE) return;
  /* Characteristic discovery may end with ATT Attribute Not Found on some FW;
     the received handle is more definitive than this completion error alone. */
  if (s==S_WAIT_SERVICE) {
    if(service_start && service_end) change_state(S_FIND_XYZ);
    else failed_gatt("service UUID def0 not found");
  } else if(s==S_WAIT_XYZ) {
    if(xyz_val) change_state(S_FIND_FREQ);
    else failed_gatt("XYZ UUID def1 not found");
  } else if(s==S_WAIT_FREQ) {
    if(freq_val) change_state(S_FIND_CCCD);
    else failed_gatt("FREQ UUID def2 not found");
  } else if(s==S_WAIT_SUBSCRIBE) {
    if(e->error_code==BLE_STATUS_SUCCESS) {notification_enabled=TRUE;change_state(S_WRITE_FREQ);}
    else failed_gatt("CCCD write failed");
  } else if(s==S_WAIT_FREQ_WRITE) {
    if(e->error_code==BLE_STATUS_SUCCESS) {
      configured_hz=requested_hz;
      printf("GATT: frequency accepted (%u Hz); streaming XYZ\r\n",(unsigned)configured_hz);
      change_state(S_READY);
      if (demo_phase==DEMO_AWAIT_5HZ && configured_hz==5U)
        demo_start_stage(DEMO_5HZ_RUNNING,5U);
      else if (demo_phase==DEMO_AWAIT_2HZ && configured_hz==2U)
        demo_start_stage(DEMO_2HZ_RUNNING,2U);
    } else failed_gatt("frequency write failed");
  }
}
static void on_notification(const evt_gatt_attr_notification *e)
{
  if(!connected || e->conn_handle!=connection_handle || e->attr_handle!=xyz_val) return;
  if(e->event_data_length<2U || e->event_data_length-2U!=6U) {
    printf("XYZ: unexpected length %u\r\n",(unsigned)(e->event_data_length>=2U?e->event_data_length-2U:0U));return;
  }
  int16_t x=(int16_t)le16(e->attr_value),y=(int16_t)le16(e->attr_value+2),z=(int16_t)le16(e->attr_value+4);
  ++received_samples;
  if (state==S_READY && (demo_phase==DEMO_5HZ_RUNNING || demo_phase==DEMO_2HZ_RUNNING))
    ++stage_samples;
  /* Relative to this BLE connection, like time.monotonic() in the RPi demo. */
  uint32_t time_ms = (uint32_t)(HAL_GetTick() - connected_at_ms);
  printf("[%4lu.%03lu s] XYZ[%lu] = (%6d, %6d, %6d) mg\r\n",
         (unsigned long)(time_ms/1000U), (unsigned long)(time_ms%1000U),
         (unsigned long)received_samples,(int)x,(int)y,(int)z);
}
void user_notify(void *data)
{
  const hci_uart_pckt *packet=(const hci_uart_pckt *)data;
  if(!packet || packet->type!=HCI_EVENT_PKT) return;
  const hci_event_pckt *event=(const hci_event_pckt *)packet->data;
  switch(event->evt) {
  case EVT_DISCONN_COMPLETE:
    on_disconnect();break;
  case EVT_LE_META_EVENT: {
    if (event->plen<1U) break;
    const evt_le_meta_event *le=(const evt_le_meta_event *)event->data;
    if(le->subevent==EVT_LE_CONN_COMPLETE) {
      if(event->plen>=1U+EVT_LE_CONN_COMPLETE_SIZE) on_connect((const evt_le_connection_complete *)le->data);
    } else if(le->subevent==EVT_LE_ADVERTISING_REPORT) {
      /* IDB05A1/BlueNRG-MS, first byte after subevent is number of reports. */
      const uint8_t *p=le->data;
      unsigned remain=event->plen-1U;
      if(!remain) break;
      uint8_t count=*p++;--remain;
      for (unsigned i=0;i<count && remain>=LE_ADVERTISING_INFO_SIZE+1U;i++) {
        const le_advertising_info *r=(const le_advertising_info *)p;
        unsigned total=LE_ADVERTISING_INFO_SIZE+(unsigned)r->data_length+1U;
        if(total>remain) break;
        found_advertisement(r->bdaddr,r->bdaddr_type,r->evt_type,r->data_RSSI,r->data_length,
                            (int8_t)r->data_RSSI[r->data_length]);
        p+=total;remain-=total;
      }
    }
    break;
  }
  case EVT_VENDOR: {
    if (event->plen<2U) break;
    const evt_blue_aci *blue=(const evt_blue_aci *)event->data;
    switch(blue->ecode) {
    case EVT_BLUE_GAP_DEVICE_FOUND: {
      if (event->plen<2U+9U) break;
      const evt_gap_device_found *r=(const evt_gap_device_found *)blue->data;
      if (r->data_length+10U > (unsigned)(event->plen-2U)) break;
      found_advertisement(r->bdaddr,r->bdaddr_type,r->evt_type,r->data_RSSI,r->data_length,
                          (int8_t)r->data_RSSI[r->data_length]);
      break;
    }
    case EVT_BLUE_GAP_PROCEDURE_COMPLETE: {
      const evt_gap_procedure_complete *e=(const evt_gap_procedure_complete *)blue->data;
      if (state==S_SCAN_STOPPING && e->procedure_code==GAP_GENERAL_DISCOVERY_PROC) {
        printf("CLIENT: discovery stopped; connecting\r\n");
        change_state(S_CONNECT_START);
      } else if(state==S_SCANNING && e->procedure_code==GAP_GENERAL_DISCOVERY_PROC) {
        delay_retry("scan finished without target");
      }
      break;
    }
    case EVT_BLUE_ATT_FIND_BY_TYPE_VAL_RESP:
      on_service((const evt_att_find_by_type_val_resp *)blue->data);break;
    case EVT_BLUE_GATT_DISC_READ_CHAR_BY_UUID_RESP:
      on_characteristic((const evt_gatt_disc_read_char_by_uuid_resp *)blue->data);break;
    case EVT_BLUE_GATT_PROCEDURE_COMPLETE:
      on_gatt_complete((const evt_gatt_procedure_complete *)blue->data);break;
    case EVT_BLUE_GATT_NOTIFICATION:
      on_notification((const evt_gatt_attr_notification *)blue->data);break;
    default: break;
    }
    break;
  }
  default: break;
  }
}
