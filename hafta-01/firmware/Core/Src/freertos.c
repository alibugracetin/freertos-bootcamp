/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_tasks.h"
#include "app_diag.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
/**
 * @brief   Stack taşması kancası (`configCHECK_FOR_STACK_OVERFLOW = 2`).
 * @param   xTask       Taşan görev.
 * @param   pcTaskName  Taşan görevin adı.
 * @ingroup diag
 *
 * Taşma sonrası bellek bozuk kabul edilir; sistem durdurulur. Kırmızı LED yanar
 * ve `g_diag.fault = 1` yazılır — sessiz bozulma yerine görünür durma. Bir ölçüm
 * koşusu bu durumda **geçersizdir**.
 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
  g_diag.fault = 1u;
  taskDISABLE_INTERRUPTS();
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  for (;;) { }
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
/**
 * @brief   Heap tükenme kancası (`configUSE_MALLOC_FAILED_HOOK = 1`).
 * @ingroup diag
 *
 * Görev veya kuyruk oluşturulamadı demektir. Kırmızı LED yanar,
 * `g_diag.fault = 2` yazılır ve sistem durur.
 */
void vApplicationMallocFailedHook(void)
{
  g_diag.fault = 2u;
  taskDISABLE_INTERRUPTS();
  HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
  for (;;) { }
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* defaultTask burada SİLİNMEZ; ilk koşusunda kendini siler (bkz. StartDefaultTask
   * ve tasarım §13.3). Scheduler öncesi silmek onu "zombi" olarak çalıştırıyordu. */

  /* Kuyruklar ve üç uygulama görevi: TelemetryTask (3), ButtonTask (2),
   * UartTxTask (1) — native xTaskCreate ile (T-07). */
  app_create_rtos_objects();
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /*
   * defaultTask kaldırma (FR-01: tam olarak 3 uygulama görevi).
   *
   * CubeMX, CMSIS-RTOS v2'de görev listesi boş olsa bile bu görevi öncelik 24
   * (osPriorityNormal) ile üretiyor; oluşturma satırı USER CODE dışında.
   *
   * Scheduler başlamadan önce dışarıdan silmek HATALIYDI (T-07'de g_diag
   * görev sayısını 5 gösterdi): FreeRTOS, scheduler öncesi pxCurrentTCB'yi en
   * yüksek öncelikli göreve, yani buna ayarlar. vTaskDelete() bunu "kendini
   * silen görev" sanıp yalnızca bekleme listesine koyar; bizim görevlerimiz
   * (öncelik 1-3) pxCurrentTCB'yi değiştirmediği için scheduler silinmiş görevi
   * başlatır ve osDelay(1) ile her 1 ms'de diğer görevleri kesen bir zombi olur.
   *
   * Doğrusu: scheduler başladığında en yüksek öncelikli olduğu için ilk bu görev
   * koşar ve burada kendini siler. Normal kendi-silme yolu işler; TCB ve stack'i
   * Idle görevi geri verir. Uygulama görevleri henüz hiç koşmamış olur.
   */
  vTaskDelete(NULL);
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

