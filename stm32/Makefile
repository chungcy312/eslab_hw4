# Standalone build for the STM32CubeL4 v1.18.2 HeartRate BLE example.
# Run ../setup.sh stm32 to install/find the toolchain and fetch the pinned SDK.
SDK_ROOT ?= $(abspath ../.deps/STM32Cube_FW_L4_V1.18.2)
TOOLCHAIN ?=
CC := $(if $(strip $(TOOLCHAIN)),$(TOOLCHAIN)/arm-none-eabi-gcc,arm-none-eabi-gcc)
OBJCOPY := $(if $(strip $(TOOLCHAIN)),$(TOOLCHAIN)/arm-none-eabi-objcopy,arm-none-eabi-objcopy)
BUILD := build

INC := -IInc -I. -I$(SDK_ROOT)/Drivers/CMSIS/Include -I$(SDK_ROOT)/Drivers/CMSIS/Device/ST/STM32L4xx/Include \
       -I$(SDK_ROOT)/Drivers/STM32L4xx_HAL_Driver/Inc \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/ble_core \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/ble_services \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/debug \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/hw \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/tl \
       -I$(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/utilities \
       -I$(SDK_ROOT)/Drivers/BSP/B-L475E-IOT01 -I$(SDK_ROOT)/Drivers/BSP/Components/Common \
       -I$(SDK_ROOT)/Drivers/BSP/Components/lsm6dsl \
       -I$(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/include -I$(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Og -g3 -ffunction-sections -fdata-sections -Wall -Wextra $(INC) \
          -DUSE_HAL_DRIVER -DUSE_STM32L475E_IOT01 -DENABLE_SPI_FIX -DBLUENRG_MS -DSTM32L475xx -DSET_L2CAP_CONN_UPD_REQ_COMMAND
LDFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -TSTM32CubeIDE/STM32L475VGTX_FLASH.ld -Wl,--gc-sections -Wl,-Map,$(BUILD)/eslab_hw4.map -specs=nano.specs -specs=nosys.specs

APP := Src/main.c Src/system_stm32l4xx.c Src/stm32l4xx_it.c Src/hr.c Src/dis_app.c Src/hrs_app.c Src/hw4_app.c Src/hw4_service.c
COMMON := $(addprefix $(SDK_ROOT)/Projects/B-L475E-IOT01A/Applications/BLE/Common/,$(addsuffix .c,\
ble_core/bluenrg_gap_aci ble_core/bluenrg_gatt_aci ble_core/bluenrg_hal_aci ble_core/osal \
ble_services/dis ble_services/hrs ble_services/svc_ctl hw/hw_lpm hw/hw_spi hw/hw_timerserver \
tl/tl_ble_hci tl/tl_ble_io tl/tl_ble_reassembly tl/tl_ble_spi \
utilities/list utilities/lpm utilities/memory_manager utilities/scheduler))
HAL := $(addprefix $(SDK_ROOT)/Drivers/STM32L4xx_HAL_Driver/Src/,$(addsuffix .c,stm32l4xx_hal stm32l4xx_hal_cortex stm32l4xx_hal_dma stm32l4xx_hal_flash stm32l4xx_hal_flash_ex stm32l4xx_hal_gpio stm32l4xx_hal_i2c stm32l4xx_hal_i2c_ex stm32l4xx_hal_pwr stm32l4xx_hal_pwr_ex stm32l4xx_hal_rcc stm32l4xx_hal_rcc_ex stm32l4xx_hal_rtc stm32l4xx_hal_rtc_ex stm32l4xx_hal_spi stm32l4xx_hal_uart))
BSP := $(SDK_ROOT)/Drivers/BSP/B-L475E-IOT01/stm32l475e_iot01.c $(SDK_ROOT)/Drivers/BSP/B-L475E-IOT01/stm32l475e_iot01_accelero.c $(SDK_ROOT)/Drivers/BSP/Components/lsm6dsl/lsm6dsl.c
RTOS := $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/tasks.c $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/queue.c $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/list.c $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
SOURCES := $(APP) $(COMMON) $(HAL) $(BSP)
RTOS_OBJECTS := $(BUILD)/rtos/tasks.o $(BUILD)/rtos/queue.o $(BUILD)/rtos/list.o $(BUILD)/rtos/heap_4.o $(BUILD)/rtos/port.o
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES)) $(RTOS_OBJECTS) $(BUILD)/startup_stm32l475vgtx.o

all: $(BUILD)/eslab_hw4.elf $(BUILD)/eslab_hw4.bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ST's BLE utilities and FreeRTOS both provide list.h/queue.h.  Prefer the
# RTOS headers only while compiling our application source that uses queues.
$(BUILD)/Src/hw4_app.o: CFLAGS += -iquote$(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/include

$(BUILD)/rtos/tasks.o: $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/tasks.c
$(BUILD)/rtos/queue.o: $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/queue.c
$(BUILD)/rtos/list.o: $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/list.c
$(BUILD)/rtos/heap_4.o: $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
$(BUILD)/rtos/port.o: $(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
$(RTOS_OBJECTS):
	@mkdir -p $(dir $@)
	$(CC) -I$(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/include -I$(SDK_ROOT)/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F $(CFLAGS) -c $< -o $@

$(BUILD)/startup_stm32l475vgtx.o: STM32CubeIDE/Application/Startup/startup_stm32l475vgtx.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/eslab_hw4.elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD)/eslab_hw4.bin: $(BUILD)/eslab_hw4.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
