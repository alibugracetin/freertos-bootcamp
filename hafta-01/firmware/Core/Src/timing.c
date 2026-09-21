/**
 * @file    timing.c
 * @brief   Zaman kaynağının başlatılması.
 * @ingroup timing
 */

#include "timing.h"
#include "tim.h"

void timing_init(void)
{
    (void)HAL_TIM_Base_Start(&htim2);
}
