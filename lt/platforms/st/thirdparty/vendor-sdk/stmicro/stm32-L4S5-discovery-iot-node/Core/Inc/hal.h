
#ifndef _HAL_H_
#define _HAL_H_

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef void (HalRxUARTCallback)(char ch);

void HalInit(void);
void HalRegisterRxUARTCallback(HalRxUARTCallback * rx_callback);
void HalSendToTxUART(char ch);
void HalEnableRxUARTIRQ(bool bEnable);

u32 HalGetRandomU32(void);
void HalSetGpio(u32 num, bool value);

#endif
